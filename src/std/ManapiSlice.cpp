#include <cstring>

#include "ManapiAsync.hpp"
#include "ManapiMemoryPool.hpp"
#include "std/ManapiSlice.hpp"
#include "std/ManapiAsyncContext.hpp"

std::size_t summary_size_buffs (std::unique_ptr<manapi::slice_part_t, manapi::slice_base::slice_part_deleter> &buffs, manapi::slice_part_t **last) {
    std::size_t res = 0;
    manapi::slice_part_t *current = buffs.get();
    *last = current;

    while (current) {
        assert(current->buff.len);
        res += current->buff.len;
        *last = current;
        current = current->next;
    }

    return res;
}

std::size_t summary_size_buffs (manapi::slice_part_t *first, manapi::slice_part_t *last) {
    std::size_t res = 0;

    if (!last)
        return 0;

    for (; first != last->next; first = first->next) {
        assert(first->buff.len);
        res += first->buff.len;
    }

    return res;
}

manapi::slice_base::slice_base(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff) {
    this->last = nullptr;
    this->size_ = summary_size_buffs(buffs, &this->last);
    this->first = buffs.release();
    this->count = nbuff;
    this->rshift_ = 0;
    this->shift_ = 0;
}

manapi::slice_base::slice_base(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff,
    uint32_t rshift) {
    this->last = nullptr;
    this->rshift_ = rshift;
    this->size_ = summary_size_buffs(buffs, &this->last);
    this->first = buffs.release();
    this->count = nbuff;
    this->shift_ = 0;
}

manapi::slice_base::slice_base(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size) {
    this->last = last;
    this->first = first;
    this->count = count;
    this->size_ = summary_size_buffs (first, last);
    this->shift_ = shift;
    this->rshift_ = rshift;

    assert(this->size() == size);
}

manapi::slice_iterator::slice_iterator(slice_part_t *part, slice_base *base) {
    this->part = part;
    this->base_ = base;
}

manapi::slice_iterator::slice_iterator(const slice_iterator &n) = default;

manapi::slice_iterator & manapi::slice_iterator::operator=(const slice_iterator &n) = default;

manapi::slice_iterator::~slice_iterator() = default;

manapi::slice_iterator & manapi::slice_iterator::operator++() {
    this->part = this->part->next;
    return *this;
}

manapi::slice_iterator & manapi::slice_iterator::operator++(int) {
    this->part = this->part->next;
    return *this;
}

bool manapi::slice_iterator::operator==(const slice_iterator &n) const {
    return this->part == n.part;
}

void * manapi::slice_iterator::buffer() {
    if (this->base_->slices_begin() == this->part)
        return this->part->buff.base + this->base_->shift();

    return this->part->buff.base;
}

std::size_t manapi::slice_iterator::size() const {
    std::size_t s = this->part->buff.len;
    if (this->base_->slices_begin() == this->part)
        s -= this->base_->shift();
    if (this->is_last())
        s -= this->base_->rshift();
    return s;
}

bool manapi::slice_iterator::is_last() const {
    return this->base_->slices_end() == this->part->next;
}

void manapi::slice_base::slice_part_deleter::operator()(slice_part_t *ptr) {
    if (ptr) {
        manapi::slice_base::slice_part_deleter::operator()(ptr->next);
    }
    delete ptr;
}

manapi::slice_base::~slice_base() = default;

manapi::slice_base::slice_base(slice_base &&n) noexcept = default;

manapi::slice_base & manapi::slice_base::operator=(slice_base &&n) noexcept = default;

manapi::slice_base::slice_base(const slice_base &n) = default;

manapi::slice_base & manapi::slice_base::operator=(const slice_base &n) = default;

manapi::error::status manapi::slice_base::shift_add(std::size_t shift) MANAPIHTTP_NOEXCEPT {
    if (this->size() < shift)
        return error::status_out_of_range("slice: shift is too large");

    if (this->last) {
        this->shift_ += shift;
        assert(this->first != this->last->next);
        while (this->first != this->last->next
                && this->first->buff.len <= this->shift_) {
            this->shift_ -= this->first->buff.len;
            this->size_ -= this->first->buff.len;
            manapi::async::current()->memory_fabric().free(this->first->buff.base, this->first->buff.len);
            auto const ptr = this->first;
            this->first = this->first->next;
            this->count -= 1;
            delete ptr;

            if (this->first == this->last->next) {
                this->last = nullptr;
                this->first = nullptr;
                break;
            }
        }
    }

    return error::status_ok();
}

manapi::error::status manapi::slice_base::copy_from(const void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!size)
        return manapi::error::status_ok();

    if (!this->last)
        return error::status_out_of_range("shift and size is too large");

    auto buffer_casted = static_cast<const char *>(buffer);

    auto current = this->first;
    shift += this->shift_;
    while (current != this->last->next
        && current->buff.len < shift) {
        shift -= current->buff.len;
        current = current->next;
    }

    while (size) {
        if (current == this->last->next)
            return manapi::error::status_out_of_range("shift and size is too large");

        std::size_t copy;
        if (current == this->last)
            copy = std::min(size, current->buff.len - shift - this->rshift_);
        else
            copy = std::min(size, current->buff.len - shift);

        memcpy (current->buff.base + shift, buffer_casted, copy);

        buffer_casted += copy;
        size -= copy;
        current = current->next;
        shift = 0;
    }

    return manapi::error::status_ok();
}

manapi::error::status manapi::slice_base::copy_to(void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT {
    auto buffer_casted = static_cast<char *>(buffer);

    if (size) {
        if (!this->last)
            return error::status_out_of_range("shift and size is too large");

        auto current = this->first;
        shift += this->shift_;
        while (current != this->last
                && current->buff.len <= shift) {
            shift -= current->buff.len;
            current=current->next;
        }
        while (size) {
            if (current == this->last->next)
                return manapi::error::status_out_of_range("shift and size is too large");

            std::size_t copy;
            if (current == this->last)
                copy = std::min(size, current->buff.len - shift - this->rshift_);
            else
                copy = std::min(size, current->buff.len - shift);

            memcpy(buffer_casted, current->buff.base + shift, copy);

            size -= copy;
            shift = 0;
            current=current->next;
            buffer_casted+=copy;
        }
    }
    return error::status_ok();
}

manapi::error::status manapi::slice_base::copy_from(slice_base &n, std::size_t shift, std::size_t shift_n, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!size)
        return error::status_ok();

    if (!this->last)
        return error::status_out_of_range("shift and size is too large");

    auto current = this->first,
         current_n = n.first;

    shift += this->shift_;
    while (current != this->last
        && current->buff.len <= shift) {
        shift -= current->buff.len;
        current=current->next;
    }

    shift_n += n.shift();

    while (current_n != n.last
        && current_n->buff.len <= shift_n) {
        shift_n -= current_n->buff.len;
        current_n=current_n->next;
    }

    while (size) {
        if (current == this->last->next
            || current_n == n.last->next)
            return manapi::error::status_out_of_range("shift and size is too large");

        std::size_t fcopy;
        if (current->next == this->last->next)
            fcopy = current->buff.len - shift - this->rshift_;
        else
            fcopy = current->buff.len - shift;

        std::size_t scopy;
        if (current_n->next == n.last->next)
            scopy = current_n->buff.len - shift_n - n.rshift_;
        else
            scopy = current_n->buff.len - shift_n;

        auto const copy = std::min(size, std::min(fcopy, scopy));

        memcpy (current->buff.base + shift, current_n->buff.base + shift_n, copy);

        shift += copy;
        shift_n += copy;

        size -= copy;

        if (shift == current->buff.len) {
            shift = 0;
            current=current->next;
        }

        if (shift_n == current_n->buff.len) {
            shift_n = 0;
            current_n=current_n->next;
        }
    }

    return manapi::error::status_ok();
}

manapi::error::status_or<manapi::slice_base> manapi::slice_base::subslice(std::size_t pos, std::size_t size) const MANAPIHTTP_NOEXCEPT {
    pos += this->shift_;
    auto const size_ = this->size_ - this->rshift_;
    if (!size)
        size = size_ - pos;

    if (!size)
        return slice_base(nullptr, nullptr, 0, 0, 0, 0);

    if (!this->last)
        return error::status_out_of_range("shift and size is too large");

    if (pos == this->shift_ && size == size_) {
        return *this;
    }

    if (pos + size > size_) {
        return manapi::error::status_out_of_range("subslice size is too large");
    }

    auto tmp_size = size;
    uint32_t cnt = 0;
    auto current = this->first;
    while (this->last->next != current
        && current->buff.len <= pos) {
        pos -= current->buff.len;
        current = current->next;
    }
    if (current == this->last->next)
        return manapi::slice_base{nullptr, nullptr, 0, 0, 0, 0};

    size += pos;
    auto scurrent = current;
    while (this->last->next != scurrent
        && scurrent->buff.len < size) {
        cnt++;
        size -= scurrent->buff.len;
        scurrent=scurrent->next;
    }
    if (this->last->next == scurrent)
        return error::status_out_of_range("pos and size too large");

    ssize_t rshift;

    if (size) {
        rshift = scurrent->buff.len - size;
        cnt++;
    }
    else {
        rshift = 0;
    }

    return manapi::slice_base{current, scurrent, cnt, pos, static_cast<std::size_t>(rshift), tmp_size};
}

std::size_t manapi::slice_base::shift() const {
    return this->shift_;
}

std::size_t manapi::slice_base::rshift() const {
    return this->rshift_;
}

bool manapi::slice_base::empty() const {
    return this->size_ == 0;
}

const manapi::slice_part_t * manapi::slice_base::slices_begin() const {
    return this->first;
}

const manapi::slice_part_t * manapi::slice_base::slices_end() const {
    return (this->last) ? (this->last->next) : nullptr;
}

manapi::slice_iterator manapi::slice_base::begin() {
    return slice_iterator{this->first, this};
}

manapi::slice_iterator manapi::slice_base::end() {
    return slice_iterator{(this->last) ? (this->last->next) : nullptr, this};
}

int manapi::slice_base::cmp(const manapi::slice_base &n) const {
    auto size = n.size();
    if (this->size() != size)
        return this->size() > size ? -1 : 1;

    auto buffptr1 = this->first;
    auto buffptr2 = n.first;

    std::size_t cursor1 = this->shift();
    std::size_t cursor2 = n.shift();

    std::size_t size1 = buffptr1->buff.len;
    std::size_t size2 = buffptr2->buff.len;

    if (!buffptr1->next)
        size1 -= this->rshift_;

    if (!buffptr2->next)
        size2 -= n.rshift_;

    while (size) {
        assert(buffptr1 && buffptr2);

        auto const cmp_size = std::min<std::size_t>(size1 - cursor1,
            size2 - cursor2);

        if (auto const rhs = strncmp(buffptr1->buff.base + cursor1, buffptr2->buff.base + cursor2, cmp_size))
            return rhs > 0 ? 1 : -1;

        cursor1 += cmp_size;
        cursor2 += cmp_size;

        if (cursor1 == buffptr1->buff.len) {
            buffptr1 = buffptr1->next;
            cursor1 = 0;

            if (buffptr1) {
                size1 = buffptr1->buff.len;
                if (!buffptr1->next)
                    size1 -= this->rshift_;
            }
        }

        if (cursor2 == buffptr2->buff.len) {
            buffptr2 = buffptr2->next;
            cursor2 = 0;

            if (buffptr2) {
                size2 = buffptr2->buff.len;
                if (!buffptr2->next)
                    size2 -= n.rshift_;
            }
        }

        assert(size >= cmp_size);
        size -= cmp_size;
    };


    return 0;
}

void manapi::slice_base::slices_buffs(ev::buff_t *buffs) const {
    auto buffsptr = buffs;
    auto cur = this->first;
    while (this->last && cur != this->last->next) {
        *buffsptr = cur->buff;
        buffsptr++;
        cur = cur->next;
    }

    if (this->count) {
        buffsptr--;

        buffs->base += this->shift_;
        buffs->len -= this->shift_;
        buffsptr->len -= this->rshift_;
    }
}

std::unique_ptr<manapi::ev::buff_t, manapi::ev::buffer_deleter> manapi::slice_base::slices_buffs() const {
    std::unique_ptr<manapi::ev::buff_t, manapi::ev::buffer_deleter> buffs;
    buffs.reset(new ev::buff_t[this->count]);
    auto buffsptr = buffs.get();
    auto cur = this->first;
    while (this->last && cur != this->last->next) {
        *buffsptr = cur->buff;
        buffsptr++;
        cur = cur->next;
    }

    if (this->count) {
        buffsptr--;

        buffs->base += this->shift_;
        buffs->len -= this->shift_;
        buffsptr->len -= this->rshift_;
    }

    return std::move(buffs);
}

std::size_t manapi::slice_base::slices_size() const {
    return this->count;
}

std::size_t manapi::slice_base::size() const {
    return this->size_ - this->shift_ - this->rshift_;
}

manapi::error::status manapi::slice_base::rshift_add_(std::size_t s) noexcept(true) {
    if (s > this->size())
        return error::status_out_of_range("s is too large");

    this->rshift_ += s;

    auto &mem = manapi::async::current()->memory_fabric();

    slice_part_t *parts[this->count];

    auto current = this->first;
    for (uint32_t i = 0; i < this->count; i++) {
        parts[i] = current;
        current = current->next;
    }

    while (this->count && this->rshift_ >= this->last->buff.len) {
        this->rshift_ -= this->last->buff.len;
        this->size_ -= this->last->buff.len;

        mem.free(this->last->buff.base, this->last->buff.len);
        delete this->last;

        this->count --;

        if (this->count) {
            this->last = parts[this->count - 1];
        }
        else {
            assert(this->shift_ == 0);

            this->last = nullptr;
            this->first = nullptr;

            assert(this->size_ == 0);
        }
    }

    if (this->count == 1 && this->shift_ + this->rshift_ == this->last->buff.len) {
        mem.free(this->last->buff.base, this->last->buff.len);
        delete this->last;

        this->first = nullptr;
        this->last = nullptr;
        this->size_ = 0;
        this->rshift_ = 0;
        this->shift_ = 0;
        this->count--;
    }

    return error::status_ok();

}

manapi::slice_view::slice_view(const slice_base &n) : slice_base(n) {

}

manapi::slice_view::slice_view(const slice &n) : slice_base(n) {
}

manapi::slice_view::slice_view(const slice_view &n) = default;

manapi::slice_view & manapi::slice_view::operator=(const slice_view &n) = default;

manapi::slice_view::~slice_view() = default;

manapi::slice_ref::slice_ref() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {

}

manapi::slice_ref::slice_ref(const slice_ref &n) : slice_ref() {
    assert(!n.rshift_);
    assert(!n.shift_);

    for (auto it = n.slices_begin(); it != n.slices_end(); ++it) {
        auto t = std::make_unique<slice_part_t>();
        t->buff = it->buff;
        if (this->last) {
            this->last->next = t.release();
            this->last = this->last->next;
        }
        else {
            this->first = t.release();
            this->last = this->first;
        }
    }

    this->count = n.count;
    this->size_ = n.size_;
}

manapi::slice_ref & manapi::slice_ref::operator=(const slice_ref &n) {
    this->clear();

    assert(!n.rshift_);
    assert(!n.shift_);

    for (auto it = n.slices_begin(); it != n.slices_end(); ++it) {
        auto t = std::make_unique<slice_part_t>();
        t->buff = it->buff;
        if (this->last) {
            this->last->next = t.release();
            this->last = this->last->next;
        }
        else {
            this->first = t.release();
            this->last = this->first;
        }
    }

    this->count = n.count;
    this->size_ = n.size_;
    return *this;
}

manapi::slice_ref::slice_ref(slice_ref &&n) noexcept = default;

manapi::slice_ref & manapi::slice_ref::operator=(slice_ref &&n) noexcept = default;

manapi::slice_ref::~slice_ref() {
    this->clear();
}

manapi::error::status manapi::slice_ref::push_back(const void *buffer, std::size_t size) {
    auto t = std::make_unique<slice_part_t>();
    t->buff.base = (char*)(buffer);
    t->buff.len = size;
    this->size_ += size;
    if (this->last) {
        this->last->next = t.release();
        this->last = this->last->next;
    }
    else {
        assert(!this->first);
        this->first = t.release();
        this->last = this->first;
    }
    this->count++;
    return error::status_ok();
}

void manapi::slice_ref::clear() noexcept(true) {
    auto cur = this->first;
    if (this->last) {
        while (cur != this->last->next) {
            delete std::exchange(cur, cur->next);
        }
    }

    this->first = nullptr;
    this->last = nullptr;
    this->size_ = 0;
    this->shift_ = 0;
}


manapi::slice::slice() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {
}

manapi::error::status_or<manapi::slice> manapi::slice::create(std::size_t n) noexcept(true) {
    return manapi::async::current()->memory_fabric().slice(n);
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff)
    : slice_base(std::move(buffs), nbuff){
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff, uint32_t rshift)
    : slice_base(std::move(buffs), nbuff, rshift){
}

manapi::slice::slice(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift,
    std::size_t size) : slice_base(first, last, count, shift, rshift, size) {
}

//
// manapi::slice::slice(slice_base n) : slice_base(std::move(n)) {
//
// }

manapi::slice::slice(slice &&n) noexcept : slice() {
    this->first = n.first;
    this->last = n.last;
    this->count = n.count;
    this->rshift_ = n.rshift_;
    this->size_ = n.size_;
    this->shift_ = n.shift_;

    n.size_ = 0;
    n.shift_ = 0;
    n.rshift_ = 0;
    n.count = 0;
    n.first = nullptr;
    n.last = nullptr;
}

manapi::slice & manapi::slice::operator=(slice &&n) noexcept {
    this->first = n.first;
    this->last = n.last;
    this->count = n.count;
    this->rshift_ = n.rshift_;
    this->shift_ = n.shift_;
    this->size_ = n.size_;

    n.size_ = 0;
    n.shift_ = 0;
    n.rshift_ = 0;
    n.count = 0;
    n.first = nullptr;
    n.last = nullptr;
    return *this;
}

manapi::slice::~slice() {
    this->clear();
}

manapi::error::status manapi::slice::resize(std::size_t size) MANAPIHTTP_NOEXCEPT {
    auto const cur = this->size();

    if (size == cur)
        return error::status_ok();

    if (size < cur) {
        assert(this->rshift_add_(cur - size).ok());
    }
    else {
        size -= cur;

        auto rhs = std::min<size_t>(this->rshift_, size);
        this->rshift_ -= rhs;
        size -= rhs;

        if (size) {
            auto slice = manapi::async::current()->memory_fabric().slice(size);
            if (!slice.ok())
                return slice.err();
            this->push_back(slice.unwrap());
        }
    }

    return error::status_ok();
}

manapi::error::status manapi::slice::push_back(bytebuffer buffer) MANAPIHTTP_NOEXCEPT {
    if (buffer.empty())
        return error::status_ok();

    if (buffer.flags() & bytebuffer::BYTEBUFFER_FLAG_OBJECT_POOL) {
        std::unique_ptr<slice_part_t> t (new (std::nothrow) slice_part_t{});
        if (!t)
            return error::status_resource_exhausted();

        auto const shift = buffer.shift();

        t->buff.len = buffer.realsize();
        this->size_ += buffer.realsize();
        t->buff.base = static_cast<char*>(buffer.release());

        if (shift)
            memmove(t->buff.base, t->buff.base + shift, t->buff.len - shift);

        if (this->last) {
            this->last->next = t.release();
            this->last = this->last->next;
        }
        else {
            assert(!this->first);
            this->first = t.release();
            this->last = this->first;
        }

        this->count++;


        return error::status_ok();
    }

    return this->push_back(buffer.data(), buffer.size());
}

manapi::error::status manapi::slice::push_back(slice s) MANAPIHTTP_NOEXCEPT {
    if (!s.empty()) {
        if (this->count) {
            assert(this->first && this->last);

            if (!this->rshift_ && !s.shift_) {
                this->rshift_ = s.rshift_;
                this->last->next = s.first;
                this->last = s.last;
                this->count += s.count;
                this->size_ += s.size_;

                s.first = nullptr;
                s.last = nullptr;
                s.count = 0;
                s.size_ = 0;
                s.rshift_ = 0;
                s.shift_ = 0;
            }
            else {
                auto const datasize = s.size();
                char data[datasize];
                std::size_t i = 0;
                auto res = s.copy_to(data, 0, datasize);
                if (!res)
                    return std::move(res);

                if (i != datasize && this->rshift_) {
                    assert(this->last->buff.len >= this->rshift_);
                    auto rhs = std::min<std::size_t>(this->rshift_, datasize - i);
                    memcpy (this->last->buff.base + this->last->buff.len - this->rshift_,
                        data + i, rhs);

                    this->rshift_ -= rhs;
                    i += rhs;
                }

                std::size_t s_shift_used = 0;

                if (i != datasize) {
                    assert(s.first->buff.len >= s.shift_);
                    s_shift_used = std::min<std::size_t>(s.shift_, datasize - i);
                    memcpy (s.first->buff.base + s.first->buff.len - s_shift_used,
                        data + i, s_shift_used);

                    auto err = s.rshift_add_(s_shift_used);
                    if (!err)
                        return std::move(err);

                    i += s_shift_used;
                }

                if (i != datasize) {
                    assert(!s.shift_ && !this->rshift_);
                    auto rhs = s.copy_from(data + i, 0, datasize - i);
                    assert(rhs.ok());
                }

                s.shift_ -= s_shift_used;

                return this->push_back(std::move(s));
            }
        }
        else {
            *this = std::move(s);
        }
    }

    return error::status_ok();
}

manapi::error::status manapi::slice::push_back(const void *buffer, ssize_t size) MANAPIHTTP_NOEXCEPT {
    try {
        if (!size)
            return error::status_ok();

        if (this->rshift_) {
            auto const copy = std::min<ssize_t>(size, this->rshift_);
            memcpy (this->last->buff.base + this->last->buff.len - this->rshift_, buffer, copy);

            this->rshift_ -= copy;
            size -= copy;

            buffer = static_cast<const char *>(buffer) + copy;

            if (!size)
                return error::status_ok();
        }

        auto slice_res = manapi::async::current()->memory_fabric().slice(size);
        if (!slice_res.ok())
            return slice_res.err();

        auto slice = slice_res.unwrap();

        auto res = slice.copy_from(buffer, 0, size);
        if (!res.ok())
            return res;

        assert(!this->rshift_ && !slice.shift_);

        if (this->last) {
            this->rshift_ = slice.rshift_;
            this->last->next = slice.first;
            this->last = slice.last;
            this->count += slice.count;
            this->size_ += slice.size_;

            slice.first = nullptr;
            slice.last = nullptr;
            slice.count = 0;
            slice.size_ = 0;
            slice.rshift_ = 0;
            slice.shift_ = 0;
        }
        else {
            *this = std::move(slice);
        }

        return error::status_ok();
    }
    catch (std::bad_alloc const &) {

    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return error::status_resource_exhausted();
}


void manapi::slice::clear() MANAPIHTTP_NOEXCEPT {
    auto cur = this->first;

    while (cur&&cur != this->last->next) {
        manapi::async::current()->memory_fabric().free(cur->buff.base, cur->buff.len);
        delete std::exchange(cur, cur->next);
    }

    this->first = nullptr;
    this->last = nullptr;
    this->size_ = 0;
    this->shift_ = 0;
}

void manapi::slice::remove_shift() MANAPIHTTP_NOEXCEPT {
    this->shift_ = 0;
}

manapi::slice_view::slice_view() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {

}

manapi::slice_view::slice_view(slice_part_t *part) : slice_base(part, part, 1, 0, 0, part->buff.len) {

}

manapi::slice_view::slice_view(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size) : slice_base(first, last, count, shift, rshift, size) {

}
