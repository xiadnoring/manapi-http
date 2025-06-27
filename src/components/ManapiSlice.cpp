#include "components/ManapiSlice.hpp"

#include <cstring>

#include "ManapiAsync.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "components/ManapiObjectPool.hpp"

std::size_t summary_size_buffs (std::unique_ptr<manapi::slice_part_t, manapi::slice_base::slice_part_deleter> &buffs, manapi::slice_part_t **last) {
    std::size_t res = 0;
    manapi::slice_part_t *current = buffs.get();
    *last = current;

    while (current) {
        res += current->buff.len;
        current = current->next;
        *last = current;
    }

    return res;
}

std::size_t summary_size_buffs (manapi::slice_part_t *first, manapi::slice_part_t *last) {
    std::size_t res = 0;

    for (; first != last; first = first->next) {
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

void manapi::slice_base::shift_add(std::size_t shift) {
    this->shift_ += shift;
    while (this->first != this->last
        && this->first->buff.len <= this->shift_) {
        this->shift_ -= this->first->buff.len;
        this->size_ -= this->first->buff.len;
        manapi::async::current()->memory_fabric().free(this->first->buff.base, this->first->buff.len);
        auto const ptr = this->first->next;
        this->first = this->first->next;
        this->size_ -= 1;
        delete ptr;
    }
}

manapi::error::status manapi::slice_base::copy_from(const void *buffer, std::size_t shift, std::size_t size) {
    auto buffer_casted = static_cast<const char *>(buffer);

    auto current = this->first;
    shift += this->shift_;
    while (current != this->last
        && current->buff.len <= shift) {
        shift -= current->buff.len;
        current = current->next;
    }

    while (size) {
        if (current == this->last)
            return manapi::error::status_out_of_range("shift and size is too large");

        std::size_t copy;
        if (current->next == this->last)
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

manapi::error::status manapi::slice_base::copy_to(void *buffer, std::size_t shift, std::size_t size) {
    auto buffer_casted = static_cast<char *>(buffer);

    auto current = this->first;
    shift += this->shift_;
    while (current != this->last
        && current->buff.len <= shift) {
        shift -= current->buff.len;
        current=current->next;
    }
    while (size) {
        if (current == this->last)
            return manapi::error::status_out_of_range("shift and size is too large");

        std::size_t copy;
        if (current->next == this->last)
            copy = std::min(size, current->buff.len - shift - this->rshift_);
        else
            copy = std::min(size, current->buff.len - shift);

        memcpy(buffer_casted, current->buff.base + shift, copy);

        size -= copy;
        shift = 0;
        current=current->next;
        buffer_casted+=copy;
    }
    return error::status_ok();
}

manapi::error::status manapi::slice_base::copy_from(slice_base &n, std::size_t shift, std::size_t shift_n, std::size_t size) {
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
        if (current == this->last
            || current_n == n.last)
            return manapi::error::status_out_of_range("shift and size is too large");

        std::size_t fcopy;
        if (current->next == this->last)
            fcopy = current->buff.len - shift - this->rshift_;
        else
            fcopy = current->buff.len - shift;

        std::size_t scopy;
        if (current_n->next == n.last)
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

manapi::error::status_or<manapi::slice_base> manapi::slice_base::subslice(std::size_t pos, std::size_t size) const {
    pos += this->shift_;
    auto const size_ = this->size_ - this->rshift_;
    if (!size)
        size = size_ - pos;

    if (pos == this->shift_ && size == size_) {
        return *this;
    }

    if (pos + size > size_) {
        return manapi::error::status_out_of_range("subslice size is too large");
    }

    auto tmp_size = size;
    uint32_t cnt = 0;
    auto current = this->first;
    while (this->last != current
        && current->buff.len <= pos) {
        pos -= current->buff.len;
        current = current->next;
    }
    if (current == this->last) {
        return manapi::slice_base{nullptr, nullptr, 0, 0, 0, 0};
    }
    size += pos;
    auto scurrent = current;
    while (this->last != scurrent
        && scurrent->buff.len <= size) {
        cnt++;
        size -= scurrent->buff.len;
        scurrent=scurrent->next;
    }
    if (size && this->last == scurrent)
        return error::status_out_of_range("pos and size too large");

    ssize_t rshift;

    if (size) {
        rshift = scurrent->buff.len - size;
        cnt++;
        scurrent=scurrent->next;
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

void manapi::slice_base::resize(std::size_t size) {
    size += this->shift_;

    if (size == this->size_)
        return;

    if (size > this->size_) {

    }
    else {

    }
}

bool manapi::slice_base::empty() const {
    return this->size_ == 0;
}

const manapi::slice_part_t * manapi::slice_base::slices_begin() const {
    return this->first;
}

const manapi::slice_part_t * manapi::slice_base::slices_end() const {
    return this->last;
}

manapi::slice_iterator manapi::slice_base::begin() {
    return slice_iterator{this->first, this};
}

manapi::slice_iterator manapi::slice_base::end() {
    return slice_iterator{this->last, this};
}

void manapi::slice_base::slices_buffs(ev::buff_t *buffs) const {
    auto buffsptr = buffs;
    auto cur = this->first;
    while (cur != this->last) {
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
    while (cur != this->last) {
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

manapi::slice_view::slice_view(slice_base n) : slice_base(std::move(n)) {

}

manapi::slice_view::slice_view(const slice &n) : slice_base(n) {
}

manapi::slice_view::slice_view(const slice_view &n) = default;

manapi::slice_view & manapi::slice_view::operator=(const slice_view &n) = default;

manapi::slice_view::~slice_view() = default;

manapi::slice::slice() : slice_base(nullptr, nullptr, 0, 0, 0, 0) {
}

manapi::slice::slice(std::unique_ptr<slice_part_t, slice_part_deleter> buffs, uint32_t nbuff)
    : slice_base(std::move(buffs), nbuff){
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
    this->shift_ = n.shift_;

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

manapi::error::status manapi::slice::append(const void *buffer, ssize_t size) {
    return error::status_unimplemented("slice: append unimplemented");
}


void manapi::slice::clear() noexcept(true) {
    // switch (this->state) {
    //     case SLICE_STATE_MALLOC: {
    //         auto cur = this->first;
    //         while (cur != this->last) {
    //             manapi::memory::free(cur->buff.base);
    //             cur = cur->next;
    //         }
    //         break;
    //     }
    //     case SLICE_STATE_REF:
    //         /* is ref */
    //             break;
    //     default: {
    //         auto cur = this->first;
    //         while (cur != this->last) {
    //             manapi::async::current()->memory_fabric().free(cur->buff.base, cur->buff.len);
    //             cur = cur->next;
    //         }
    //         break;
    //     }
    // }

    auto cur = this->first;
    while (cur != this->last) {
        manapi::async::current()->memory_fabric().free(cur->buff.base, cur->buff.len);
        cur = cur->next;
    }

    this->first = nullptr;
    this->last = nullptr;
    this->size_ = 0;
    this->shift_ = 0;
}
