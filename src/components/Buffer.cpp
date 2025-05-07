#include "./components/Buffer.hpp"

#include <format>
#include <stdexcept>
#include <utility>

#include "ManapiUtils.hpp"

manapi::bytebuffer::bytebuffer() {
    this->src = nullptr;
    this->s = 0;
    this->shift_ = 0;
    this->reserved = 0;
}

manapi::bytebuffer::bytebuffer(void *src, std::size_t size) {
    this->src = static_cast <uint8_t *> (src);
    this->s = static_cast<int>(size);
    this->reserved = static_cast<int>(size);
    this->shift_ = 0;
}

manapi::bytebuffer::bytebuffer(std::size_t size) {
    this->src = size ? manapi::memory::alloc<uint8_t>(size) : nullptr;
    this->s = static_cast<int>(size);
    this->reserved = static_cast<int>(size);
    this->shift_ = 0;
}

manapi::bytebuffer::~bytebuffer() {
    this->clear();
}

manapi::bytebuffer::bytebuffer(bytebuffer &&n) noexcept {
    this->s = std::exchange(n.s, 0);
    this->reserved = std::exchange(n.reserved, 0);
    this->src = std::exchange(n.src, nullptr);
    this->shift_ = std::exchange(n.shift_, 0);
}

manapi::bytebuffer & manapi::bytebuffer::operator=(bytebuffer &&n) noexcept {
    this->s = std::exchange(n.s, 0);
    this->reserved = std::exchange(n.reserved, 0);
    this->src = std::exchange(n.src, nullptr);
    this->shift_ = std::exchange(n.shift_, 0);
    return *this;
}

char * manapi::bytebuffer::c_str() {
    return reinterpret_cast<char *> (this->src);
}

char * manapi::bytebuffer::data() {
    return reinterpret_cast<char *> (this->src);
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

std::size_t manapi::bytebuffer::size() const {
    return this->s - this->shift_;
}

std::size_t manapi::bytebuffer::realsize() const {
    return this->reserved;
}

void manapi::bytebuffer::resize(std::size_t s) {
    if (this->reserved >= s) {
        this->s = static_cast<int>(s);
        this->shift_ = std::min(this->shift_, this->s);
        return;
    }

    if (this->src) {
        this->src = manapi::memory::realloc(this->src, s);
    }
    else {
        this->src = manapi::memory::alloc<uint8_t>(s);
    }

    this->reserved = static_cast<int>(s);
    this->s = static_cast<int>(s);
    this->shift_ = std::min(this->shift_, this->s);
}

void manapi::bytebuffer::resize_max(std::size_t s) {
    this->resize(std::max(s, this->realsize()));
}

void manapi::bytebuffer::clear() {
    manapi::memory::free(std::exchange(this->src, nullptr));
    this->s = 0;
    this->reserved = 0;
}

void manapi::bytebuffer::reinit() {
    this->resize(0);
}

void * manapi::bytebuffer::release() {
    this->s = 0;
    this->reserved = 0;
    this->shift_ = 0;
    return std::exchange(this->src, nullptr);
}

int manapi::bytebuffer::shift() const {
    return this->shift_;
}

void manapi::bytebuffer::shift(int n) {
    this->shift_ = n;
}

void manapi::bytebuffer::shift_add(int n) {
    this->shift_ += n;
}

bool manapi::bytebuffer::empty() const {
    return !this->size();
}
