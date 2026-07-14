#include <cstring>

#include "ManapiAsync.hpp"
#include "ManapiMemoryPool.hpp"
#include "std/ManapiSlice.hpp"
#include "std/ManapiAsyncContext.hpp"

std::size_t summary_size_buffs (manapi::slice_part_t *first, manapi::slice_part_t *last) {
    std::size_t res = 0;

    if (!last)
        return 0;

    for (; first != last->next; first = first->next) {
        //assert(first->buff.len);
        res += first->buff.len;
    }

    if (res > 100000) {
        manapi_log_warn("slice:size is too big for summary_size_buffs()");
    }

    return res;
}

manapi::slice_base::slice_base(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, slice_part_t*last, uint32_t nbuff) {
    this->last = last;
    this->size_ = summary_size_buffs(buffs.get(), last);
    this->first = buffs.release();
    this->count = nbuff;
    this->rshift_ = 0;
    this->shift_ = 0;
}

manapi::slice_base::slice_base(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, slice_part_t*last, uint32_t nbuff,
    std::size_t rshift) {
    this->last = last;
    this->rshift_ = rshift;
    this->size_ = summary_size_buffs(buffs.get(), last);
    this->first = buffs.release();
    this->count = nbuff;
    this->shift_ = 0;
}

manapi::slice_base::slice_base(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, slice_part_t*last, uint32_t nbuff, std::size_t shift, std::size_t rshift, std::size_t size) {
    this->last = last;
    this->rshift_ = rshift;
    // this->size_ = summary_size_buffs(buffs, &this->last);
    this->size_ = size + shift + rshift;
    this->first = buffs.release();
    this->count = nbuff;
    this->shift_ = 0;

    assert(this->size() == size);
}

manapi::slice_base::slice_base(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size) {
    this->last = last;
    this->first = first;
    this->count = count;
    // this->size_ = summary_size_buffs (first, last);
    this->size_ = size + shift + rshift;
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

std::string_view manapi::slice_iterator::operator*() {
    return std::string_view (static_cast<const char *> (this->buffer()), this->size ());
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

manapi::slice_const_iterator::slice_const_iterator(slice_part_t *part, slice_base const *base) {
    this->part = part;
    this->base_ = base;
}

manapi::slice_const_iterator::slice_const_iterator(const slice_const_iterator &n) = default;

manapi::slice_const_iterator & manapi::slice_const_iterator::operator=(const slice_const_iterator &n) = default;

manapi::slice_const_iterator::~slice_const_iterator() = default;

std::string_view manapi::slice_const_iterator::operator*() {
    return std::string_view (static_cast<const char *> (this->buffer()), this->size ());
}

manapi::slice_const_iterator & manapi::slice_const_iterator::operator++() {
    this->part = this->part->next;
    return *this;
}

manapi::slice_const_iterator & manapi::slice_const_iterator::operator++(int) {
    this->part = this->part->next;
    return *this;
}

bool manapi::slice_const_iterator::operator==(const slice_const_iterator &n) const {
    return this->part == n.part;
}

const void * manapi::slice_const_iterator::buffer() {
    if (this->base_->slices_begin() == this->part)
        return this->part->buff.base + this->base_->shift();

    return this->part->buff.base;
}

std::size_t manapi::slice_const_iterator::size() const {
    std::size_t s = this->part->buff.len;
    if (this->base_->slices_begin() == this->part)
        s -= this->base_->shift();
    if (this->is_last())
        s -= this->base_->rshift();
    return s;
}

bool manapi::slice_const_iterator::is_last() const {
    return this->base_->slices_end() == this->part->next;
}

void manapi::slice_base::slice_part_deleter::operator()(slice_part_t *ptr) {
    if (ptr) {
        manapi::slice_base::slice_part_deleter::operator()(ptr->next);
    }
    delete ptr;
}

manapi::slice_base::~slice_base() = default;

manapi::slice_base::slice_base(slice_base &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::slice_base & manapi::slice_base::operator=(slice_base &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::slice_base::slice_base(const slice_base &n) = default;

manapi::slice_base & manapi::slice_base::operator=(const slice_base &n) = default;

manapi::status manapi::slice_base::shift_add(std::size_t shift) MANAPIHTTP_NOEXCEPT {
    if (this->size() < shift)
        return status_out_of_range("slice: shift is too large");

    if (this->last) {
        this->shift_ += shift;
        assert(this->first != this->last->next);
        while (this->first && this->first != this->last->next
                && this->first->buff.len <= this->shift_) {
            this->shift_ -= this->first->buff.len;
            this->size_ -= this->first->buff.len;
            //manapi::async::current()->memory_fabric().free(this->first->buff.base, this->first->buff.len);
            // auto const ptr = this->first;
            this->first = this->first->next;
            this->count -= 1;
            // delete ptr;

            if (!this->first || this->first == this->last->next) {
                this->last = nullptr;
                this->first = nullptr;
                break;
            }
        }
    }

    return status_ok();
}

manapi::status manapi::slice_base::copy_from(const void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!size)
        return manapi::status_ok();

    if (shift + size > this->size())
        return status_out_of_range("shift and size is too large");

    assert(!!this->last);

    auto current = this->first;

    shift += this->shift_;
    while (current && current != this->last->next
        && current->buff.len < shift) {
        shift -= current->buff.len;
        current = current->next;
    }

    return this->copy_from(buffer, current, shift, size);
}

manapi::status manapi::slice_base::copy_from(const void *buffer, const slice_part_t *current, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!current || current->buff.len < shift)
        return status_invalid_argument("copy_from:current is invalid");

    auto buffer_casted = static_cast<const char *>(buffer);

    while (size) {
        if (current == this->last->next)
            return manapi::status_out_of_range("shift and size is too large");

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

    return manapi::status_ok();
}

manapi::status manapi::slice_base::copy_to(void *buffer, std::size_t shift, std::size_t size) MANAPIHTTP_NOEXCEPT {
    auto buffer_casted = static_cast<char *>(buffer);

    if (size) {
        if (!this->last)
            return status_out_of_range("shift and size is too large");

        auto current = this->first;
        shift += this->shift_;
        while (current != this->last
                && current->buff.len <= shift) {
            shift -= current->buff.len;
            current=current->next;
        }
        while (size) {
            if (current == this->last->next)
                return manapi::status_out_of_range("shift and size is too large");

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
    return status_ok();
}

manapi::status manapi::slice_base::copy_from(const slice_base &n, std::size_t shift, std::size_t shift_n, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!size)
        return status_ok();

    if (!this->last)
        return status_out_of_range("shift and size is too large");

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
            return manapi::status_out_of_range("shift and size is too large");

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

    return manapi::status_ok();
}

manapi::status_or<manapi::slice_base> manapi::slice_base::subslice(std::size_t pos, std::size_t size) const MANAPIHTTP_NOEXCEPT {
    pos += this->shift_;
    auto const size_ = this->size_ - this->rshift_;

    if (!size)
        return slice_base(nullptr, nullptr, 0, 0, 0, 0);

    if (!this->last)
        return status_out_of_range("shift and size is too large");

    if (pos == this->shift_ && size == size_) {
        return *this;
    }

    if (pos + size > size_) {
        return manapi::status_out_of_range("subslice size is too large");
    }

    if (pos + size >= this->size_ - static_cast<std::size_t>(this->last->buff.len)) {
        auto z = manapi::slice_base (this->first, this->last, this->count, 0, 0, this->size_);
        z.shift_add(pos).unwrap();
        z.rshift_add_(this->size_ - pos - size, false).unwrap();
        assert(!!z.last);
        return std::move(z);
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
        return status_out_of_range("pos and size too large");

    std::size_t rshift;

    if (size) {
        assert(scurrent->buff.len >= size);
        rshift = scurrent->buff.len - size;
        cnt++;
    }
    else {
        rshift = 0;
    }

    return manapi::slice_base{current, scurrent, cnt, pos, rshift, tmp_size};
}

manapi::status_or<manapi::slice_base> manapi::slice_base::subslice(std::size_t pos) const MANAPIHTTP_NOEXCEPT {
    if (this->size() == pos)
        return manapi::slice_base(nullptr, nullptr, 0, 0, 0, 0);

    auto z = *this;
    z.shift_add(pos).unwrap();

    return std::move(z);
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

const manapi::slice_part_t * manapi::slice_base::slices_rbegin() const {
    return this->last;
}

manapi::slice_iterator manapi::slice_base::begin() {
    return slice_iterator{this->first, this};
}

manapi::slice_iterator manapi::slice_base::end() {
    return slice_iterator{(this->last) ? (this->last->next) : nullptr, this};
}

manapi::slice_const_iterator manapi::slice_base::begin() const {
    return slice_const_iterator{this->first, this};
}

manapi::slice_const_iterator manapi::slice_base::end() const {
    return slice_const_iterator{(this->last) ? (this->last->next) : nullptr, this};
}

int manapi::slice_base::cmp(const manapi::slice_base &n) const MANAPIHTTP_NOEXCEPT {
    auto size = n.size();

    auto buffptr1 = this->first;
    auto buffptr2 = n.first;

    std::size_t cursor1 = this->shift();
    std::size_t cursor2 = n.shift();

    if (buffptr1 && buffptr2) {
        std::size_t size1 = buffptr1->buff.len;
        std::size_t size2 = buffptr2->buff.len;

        if (!buffptr1->next)
            size1 -= this->rshift_;

        if (!buffptr2->next)
            size2 -= n.rshift_;

        while (size && buffptr1 && buffptr2) {
            auto cmp_size = std::min<std::size_t>(size1 - cursor1,
                size2 - cursor2);

            if (cmp_size > size)
                cmp_size = size;

            if (auto const rhs = memcmp(buffptr1->buff.base + cursor1, buffptr2->buff.base + cursor2, cmp_size))
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

            size -= cmp_size;
        }
    }

    size = n.size();

    if (this->size() == size)
        return 0;

    return this->size() > size ? 1 : -1;
}

int manapi::slice_base::cmp(const void *data, std::size_t size) const MANAPIHTTP_NOEXCEPT {
    int rhs;
    size_t ss;

    for (auto it = this->begin(); it != this->end(); it++) {
        ss = std::min<std::size_t>(it.size(), size);
        rhs = memcmp (static_cast<const char *>(it.buffer()), static_cast<const char *>(data), ss);
        if (rhs)
            return rhs;
        size -= ss;
        data = static_cast<const char*>(data) + ss;
        if (it.size() != ss)
            return 1;
    }

    if (!size)
        return 0;

    return -1;
}

void manapi::slice_base::slices_buffs(ev::buff_t *buffs, std::size_t cnt, std::size_t *sz) const {
    auto buffsptr = buffs;
    auto cur = this->first;

    if (sz) *sz = 0;

    while (this->last && cur && cur != this->last->next && cnt) {
        *buffsptr = cur->buff;
        if (sz) *sz += static_cast<std::size_t>(cur->buff.len);
        buffsptr++;
        cur = cur->next;
        cnt--;
    }

    if (buffs != buffsptr) {
        assert(!!this->last);
        buffsptr--;

        buffs->base += this->shift_;
        buffs->len -= static_cast<decltype(buffs->len)>(this->shift_);
        if (sz) *sz -= this->shift_;

        if (cur == this->last->next) {
            if (sz) *sz -= this->rshift_;
            buffsptr->len -= static_cast<decltype(buffsptr->len)>(this->rshift_);
        }
    }
}

std::unique_ptr<manapi::ev::buff_t, manapi::ev::buffer_deleter> manapi::slice_base::slices_buffs(std::size_t max_cnt, std::size_t *cnt, std::size_t *sz) const {
    std::unique_ptr<manapi::ev::buff_t, manapi::ev::buffer_deleter> buffs;
    auto buffs_cnt = std::min<std::size_t>(this->count, max_cnt);
    buffs.reset(new ev::buff_t[buffs_cnt]);
    if (cnt) *cnt = buffs_cnt;
    auto buffsptr = buffs.get();
    auto cur = this->first;

    if (sz) *sz = 0;

    while (this->last && cur && cur != this->last->next && max_cnt) {
        *buffsptr = cur->buff;
        if (sz) *sz += static_cast<std::size_t>(cur->buff.len);
        buffsptr++;
        cur = cur->next;
        max_cnt--;
    }

    if (buffs.get() != buffsptr) {
        assert(!!this->last);
        buffsptr--;
        buffs->base += this->shift_;
        if (sz) *sz -= this->shift_;
        buffs->len -= static_cast<decltype(buffs->len)>(this->shift_);
        if (cur == this->last->next) {
            if (sz) *sz -= this->rshift_;
            buffsptr->len -= static_cast<decltype(buffsptr->len)>(this->rshift_);
        }
    }

    return std::move(buffs);
}

uint32_t manapi::slice_base::slices_size() const {
    return this->count;
}

std::size_t manapi::slice_base::size() const {
    return this->size_ - this->shift_ - this->rshift_;
}

std::string manapi::slice_base::to_string() const {
    std::string z;
    z.reserve(this->size());
    std::size_t i = 0;
    for (const auto b : *this) {
        ::memcpy(z.data() + i, b.data(), b.size());
        i += b.size();
    }
    return std::move(z);
}

manapi::status manapi::slice_base::rshift_add_(std::size_t s, bool can_free) MANAPIHTTP_NOEXCEPT {
    if (s > this->size())
        return status_out_of_range("s is too large");

    assert(!!this->last);
    this->rshift_ += s;

    if (this->rshift_ < static_cast<std::size_t>(this->last->buff.len)) {
        return manapi::status_ok();
    }

    auto &mem = manapi::async::current()->memory_fabric();

    if (this->rshift_ == static_cast<std::size_t>(this->last->buff.len)) {
        if (can_free) {
            mem.free(this->last->buff.base, this->last->buff.len);
            this->last->buff.base = nullptr;
            this->last->buff.len = 0;
            this->size_ -= this->rshift_;
            this->rshift_ = 0;
        }
        return manapi::status_ok();
    }

#ifdef _MSC_VER
    slice_part_t** parts = static_cast<slice_part_t**>(alloca(sizeof (slice_part_t*)*this->count));
#else
    slice_part_t *parts[this->count];
#endif

    auto current = this->first;
    for (uint32_t i = 0; i < this->count; i++) {
        parts[i] = current;
        current = current->next;
    }

    while (this->count && this->rshift_ >= this->last->buff.len) {
        this->rshift_ -= this->last->buff.len;
        this->size_ -= this->last->buff.len;

        if (can_free) {
            mem.free(this->last->buff.base, this->last->buff.len);
            delete this->last;
        }

        this->count --;

        if (this->count) {
            this->last = parts[this->count - 1];
            this->last->next = nullptr;
        }
        else {
            assert(this->shift_ == 0);

            this->last = nullptr;
            this->first = nullptr;

            assert(this->size_ == 0);
        }
    }

    if (this->count == 1 && this->shift_ + this->rshift_ == this->last->buff.len) {
        if (can_free) {
            mem.free(this->last->buff.base, this->last->buff.len);
            delete this->last;
        }

        this->first = nullptr;
        this->last = nullptr;
        this->size_ = 0;
        this->rshift_ = 0;
        this->shift_ = 0;
        this->count--;
    }

    return status_ok();

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

manapi::slice_ref::slice_ref(slice_ref &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::slice_ref & manapi::slice_ref::operator=(slice_ref &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::slice_ref::~slice_ref() {
    this->clear();
}

manapi::status manapi::slice_ref::push_back(const void *buffer, std::size_t size) {
    auto t = std::make_unique<slice_part_t>();
    t->buff.base = (char*)(buffer);
    t->buff.len = static_cast<decltype(t->buff.len)>(size);
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
    return status_ok();
}

void manapi::slice_ref::clear() MANAPIHTTP_NOEXCEPT {
    auto cur = this->first;
    if (this->last) {
        while (cur && cur != this->last->next) {
            delete std::exchange(cur, cur->next);
        }
    }

    this->first = nullptr;
    this->last = nullptr;
    this->size_ = 0;
    this->shift_ = 0;
    this->count = 0;
}


manapi::slice::slice() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {
}

manapi::status_or<manapi::slice> manapi::slice::create(std::size_t n) MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->memory_fabric().slice(n);
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, slice_part_t*last, uint32_t nbuff)
    : slice_base(std::move(buffs), last, nbuff){
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs,slice_part_t*last, uint32_t nbuff, std::size_t rshift)
    : slice_base(std::move(buffs), last, nbuff, rshift){
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs,slice_part_t*last, uint32_t nbuff, std::size_t shift,
    std::size_t rshift, std::size_t size) : slice_base(std::move(buffs), last, nbuff, shift, rshift, size) {

}

manapi::slice::slice(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift,
                     std::size_t size) : slice_base(first, last, count, shift, rshift, size) {
}

manapi::slice::slice(std::string_view n) : slice(n.data(), n.size()) {
}

manapi::slice::slice(const char *n) : slice(std::string_view(n)) {
}

manapi::slice::slice(const char *buffer, std::size_t sz) : slice() {
    this->resize(sz);
    this->copy_from(buffer, 0, sz);
}

//
// manapi::slice::slice(slice_base n) : slice_base(std::move(n)) {
//
// }

manapi::slice::slice(slice &&n) MANAPIHTTP_NOEXCEPT : slice() {
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

manapi::slice & manapi::slice::operator=(slice &&n) MANAPIHTTP_NOEXCEPT {
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

manapi::status manapi::slice::resize(std::size_t size) MANAPIHTTP_NOEXCEPT {
    auto const cur = this->size();

    if (size == cur)
        return status_ok();

    if (size < cur) {
        auto res = this->rshift_add_(cur - size, true).ok();
        assert(res);
    }
    else {
        size -= cur;

        auto rhs = std::min<size_t>(this->rshift_, size);
        const auto prev_rshift = this->rshift_;
        this->rshift_ -= rhs;
        size -= rhs;

        if (size) {
            if (this->last && this->last->buff.len < manapi::object_pool::area_size()) {
                auto sz = static_cast<std::size_t>(this->last->buff.len) + size;

                auto bf = manapi::async::memory_fabric()->buffer(std::max(sz, manapi::object_pool::area_size())).unwrap();

                if (bf.size() >= sz) {
                    this->rshift_ = bf.size() - sz;
                    sz = 0;
                }
                else {
                    sz -= bf.size();
                }

                ::memcpy(bf.data(), this->last->buff.base, static_cast<std::size_t>(this->last->buff.len) - prev_rshift);
                this->size_ -= static_cast<std::size_t>(this->last->buff.len);
                manapi::async::memory_fabric()->free(this->last->buff.base, this->last->buff.len);

                this->size_ += bf.size();
                
                this->last->buff.len = static_cast<decltype(this->last->buff.len)>(bf.size());
                this->last->buff.base = static_cast<char *>(bf.release());

                if (sz) {
                    auto slice = manapi::async::memory_fabric()->slice(sz);
                    if (!slice.ok())
                        return slice.err();

                    this->push_back(slice.unwrap());
                }
            }
            else {
                auto slice = manapi::async::memory_fabric()->slice(size);
                if (!slice.ok())
                    return slice.err();
                this->push_back(slice.unwrap());
            }
        }
    }

    return status_ok();
}

manapi::status manapi::slice::push_back(bytebuffer buffer) MANAPIHTTP_NOEXCEPT {
    if (buffer.empty())
        return status_ok();

    if (buffer.flags() & bytebuffer::BYTEBUFFER_FLAG_OBJECT_POOL) {
        std::unique_ptr<slice_part_t> t (new (std::nothrow) slice_part_t{});
        if (!t)
            return status_resource_exhausted();

        auto const shift = buffer.shift();

        t->buff.len = static_cast<decltype(t->buff.len)>(buffer.realsize());
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


        return status_ok();
    }

    return this->push_back(buffer.data(), buffer.size());
}

manapi::status manapi::slice::push_back(slice s) MANAPIHTTP_NOEXCEPT {
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
#ifdef _MSC_VER
                char *data = static_cast<char*>(alloca(datasize));
#else
                char data[datasize];
#endif
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

                    auto err = s.rshift_add_(s_shift_used, true);
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

    return status_ok();
}

manapi::status manapi::slice::push_back(const void *buffer, std::size_t size) MANAPIHTTP_NOEXCEPT {
    try {
        if (!size)
            return status_ok();

        auto last = this->last;
        std::size_t shift;
        if (last) shift = static_cast<std::size_t>(last->buff.len) - this->rshift_;
        this->resize(size + this->size());
        if (!last) {
            shift = 0;
            last = this->first;
        }
        this->copy_from(buffer, last, shift, size);

        // if (this->rshift_) {
        //     auto const copy = std::min<std::size_t>(size, this->rshift_);
        //     memcpy (this->last->buff.base + this->last->buff.len - this->rshift_, buffer, copy);
        //
        //     this->rshift_ -= copy;
        //     size -= copy;
        //
        //     buffer = static_cast<const char *>(buffer) + copy;
        //
        //     if (!size)
        //         return status_ok();
        // }
        //
        // auto slice_res = manapi::async::current()->memory_fabric().slice(size);
        // if (!slice_res.ok())
        //     return slice_res.err();
        //
        // auto slice = slice_res.unwrap();
        //
        // auto res = slice.copy_from(buffer, 0, size);
        // if (!res.ok())
        //     return res;
        //
        // assert(!this->rshift_ && !slice.shift_);
        //
        // if (this->last) {
        //     this->rshift_ = slice.rshift_;
        //     this->last->next = slice.first;
        //     this->last = slice.last;
        //     this->count += slice.count;
        //     this->size_ += slice.size_;
        //
        //     slice.first = nullptr;
        //     slice.last = nullptr;
        //     slice.count = 0;
        //     slice.size_ = 0;
        //     slice.rshift_ = 0;
        //     slice.shift_ = 0;
        // }
        // else {
        //     *this = std::move(slice);
        // }

        return status_ok();
    }
    catch (std::bad_alloc const &) {

    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return status_resource_exhausted();
}

manapi::status manapi::slice::push_back(std::string_view buffer) MANAPIHTTP_NOEXCEPT {
    return this->push_back(buffer.data(), buffer.size());
}

manapi::status manapi::slice::shift_add(std::size_t shift) MANAPIHTTP_NOEXCEPT {
    if (this->size() < shift)
        return status_out_of_range("slice: shift is too large");

    if (this->last) {
        this->shift_ += shift;
        assert(this->first != this->last->next);
        while (this->first && this->first != this->last->next
                && this->first->buff.len <= this->shift_) {
            this->shift_ -= this->first->buff.len;
            this->size_ -= this->first->buff.len;
            manapi::async::current()->memory_fabric().free(this->first->buff.base, this->first->buff.len);
            auto const ptr = this->first;
            this->first = this->first->next;
            this->count -= 1;
            delete ptr;

            if (!this->first || this->first == this->last->next) {
                this->last = nullptr;
                this->first = nullptr;
                break;
            }
        }
    }

    return status_ok();
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

manapi::slice manapi::slice::copy() const {
    auto nsv = manapi::async::memory_fabric()->slice(this->size()).unwrap();
    nsv.copy_from(*this, 0, 0, this->size()).unwrap();
    return std::move(nsv);
}

manapi::slice_view::slice_view() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {

}

manapi::slice_view::slice_view(slice_part_t *part) : slice_base(part, part, 1, 0, 0, part->buff.len) {

}

manapi::slice_view::slice_view(slice_part_t *first, slice_part_t *last, uint32_t count, std::size_t shift, std::size_t rshift, std::size_t size) : slice_base(first, last, count, shift, rshift, size) {

}
