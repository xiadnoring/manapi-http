#include <any>
#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

#include "std/ManapiBuffer.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "../include/ManapiUtils.hpp"

manapi::bytebuffer::bytebuffer() {
    this->src = nullptr;
    this->s = 0;
    this->shift_ = 0;
    this->reserved = 0;
    this->flags_ = 0;
}

bool manapi::bytebuffer::operator==(const std::nullptr_t &) const {
    return this->src == nullptr;
}

manapi::bytebuffer::bytebuffer(void *src, std::size_t size) {
    this->src = static_cast <uint8_t *> (src);
    this->s = static_cast<int>(size);
    this->reserved = static_cast<int>(size);
    this->shift_ = 0;
    this->flags_ = 0;
}

manapi::bytebuffer::bytebuffer(void *src, std::size_t size, char flags_) {
    this->src = static_cast <uint8_t *> (src);
    this->s = static_cast<int>(size);
    this->reserved = static_cast<int>(size);
    this->flags_ = flags_;
    this->shift_ = 0;
}

manapi::error::status_or<manapi::bytebuffer> manapi::bytebuffer::create(std::size_t size) {
    auto src = manapi::memory::alloc<uint8_t>(size);
    if (!src)
        return error::status_resource_exhausted();
    return bytebuffer (src, size);
}


manapi::bytebuffer::~bytebuffer() {
    this->clear();
}

manapi::bytebuffer::bytebuffer(bytebuffer &&n) noexcept {
    this->s = std::exchange(n.s, 0);
    this->reserved = std::exchange(n.reserved, 0);
    this->src = std::exchange(n.src, nullptr);
    this->shift_ = std::exchange(n.shift_, 0);
    this->flags_ = std::exchange(n.flags_, 0);
}

manapi::bytebuffer & manapi::bytebuffer::operator=(bytebuffer &&n) noexcept {
    this->clear();

    this->s = std::exchange(n.s, 0);
    this->reserved = std::exchange(n.reserved, 0);
    this->src = std::exchange(n.src, nullptr);
    this->shift_ = std::exchange(n.shift_, 0);
    this->flags_ = std::exchange(n.flags_, 0);
    return *this;
}

char * manapi::bytebuffer::c_str() {
    return reinterpret_cast<char *> (this->src) + this->shift_;
}

char * manapi::bytebuffer::data() {
    return reinterpret_cast<char *> (this->src) + this->shift_;
}

char &manapi::bytebuffer::operator[](std::size_t i_) {
    return this->at(i_);
}

char & manapi::bytebuffer::at(std::size_t i_) {
    return *(this->data() + i_);
}

const char * manapi::bytebuffer::c_str() const {
    return reinterpret_cast<char *> (this->src) + this->shift_;
}

const char * manapi::bytebuffer::data() const {
    return reinterpret_cast<char *> (this->src) + this->shift_;
}

manapi::bytebuffer::operator bool() const {
    return !!this->src;
}

std::size_t manapi::bytebuffer::size() const {
    return this->s - this->shift_;
}

std::size_t manapi::bytebuffer::realsize() const {
    return this->reserved < 0 ? this->s : this->reserved;
}

manapi::error::status manapi::bytebuffer::realresize(std::size_t s) MANAPIHTTP_NOEXCEPT {
    if (this->s == s)
        return error::status_ok();

    if (this->reserved >= s) {
        this->s = static_cast<int>(s);
        this->shift_ = std::min(this->shift_, this->s);
        return error::status_ok();
    }

    if (this->flags_ & BYTEBUFFER_FLAG_OBJECT_POOL) {

        /* is slice */
        auto nm = manapi::memory::alloc<uint8_t>(s);
        if (!nm) {
            return error::status_resource_exhausted();
        }

        this->flags_ ^= BYTEBUFFER_FLAG_OBJECT_POOL;

        memcpy(nm, this->src, this->s);

        std::swap(this->src, nm);

        auto nsize = this->reserved;
        this->s = static_cast<int>(s);

        this->reserved = this->s;

        manapi::async::current()->memory_fabric().free(nm, nsize);
    }
    else {
        if (this->src) {
            this->src = manapi::memory::realloc(this->src, s);
            if (!this->src) {
                this->clear();
                return error::status_resource_exhausted();
            }
        }
        else {
            this->src = manapi::memory::alloc<uint8_t>(s);
            if (!this->src) {
                this->clear();
                return error::status_resource_exhausted();
            }
        }

        this->reserved = static_cast<int>(s);
        this->s = static_cast<int>(s);
    }

    this->shift_ = std::min(this->shift_, this->s);
    return error::status_ok();
}

manapi::error::status manapi::bytebuffer::resize(std::size_t s) MANAPIHTTP_NOEXCEPT {
    return this->realresize(s + this->shift_);
}

manapi::error::status manapi::bytebuffer::resize_max(std::size_t s) MANAPIHTTP_NOEXCEPT {
    return this->resize(std::max(s, this->realsize()));
}

void manapi::bytebuffer::remove_shift() MANAPIHTTP_NOEXCEPT {
    this->shift_ = 0;
}

void manapi::bytebuffer::clear() MANAPIHTTP_NOEXCEPT {
    if (this->flags_ & BYTEBUFFER_FLAG_OBJECT_POOL) {
        if (this->src && this->reserved)
            manapi::async::current()->memory_fabric().free(reinterpret_cast<void*>(this->src), this->reserved);

    }
    else {
        if (this->src)
            manapi::memory::free(std::exchange(this->src, nullptr));
    }

    this->src = nullptr;
    this->flags_ =0;
    this->s = 0;
    this->reserved = 0;
    this->shift_ = 0;
}

void manapi::bytebuffer::reinit() {
    this->resize(0);
}

void * manapi::bytebuffer::release() {
    this->s = 0;
    this->reserved = 0;
    this->shift_ = 0;
    this->flags_ = 0;
    return std::exchange(this->src, nullptr);
}

std::size_t manapi::bytebuffer::shift() const {
    return this->shift_;
}

void manapi::bytebuffer::shift(std::size_t n) {
    this->shift_ = n;
}

void manapi::bytebuffer::shift_add(std::size_t n) {
    this->shift_ += n;
}

uint8_t manapi::bytebuffer::flags() {
    return this->flags_;
}

bool manapi::bytebuffer::empty() const {
    return !this->size();
}
