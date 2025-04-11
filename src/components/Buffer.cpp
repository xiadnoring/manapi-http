#include "./components/Buffer.hpp"

#include <format>
#include <stdexcept>
#include <utility>

#include "ManapiUtils.hpp"

manapi::bytebuffer::bytebuffer() = default;

manapi::bytebuffer::bytebuffer(void *src, std::size_t size) {
    this->src = static_cast <uint8_t *> (src);
    this->s = size;
    this->reserved = size;
}

manapi::bytebuffer::bytebuffer(std::size_t size) {
    this->src = size ? manapi::memory::alloc<uint8_t>(size) : nullptr;
    this->s = size;
    this->reserved = size;
}

manapi::bytebuffer::~bytebuffer() {
    this->clear();
}

manapi::bytebuffer::bytebuffer(bytebuffer &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::bytebuffer & manapi::bytebuffer::operator=(bytebuffer &&n) noexcept {
    this->s = std::exchange(n.s, 0);
    this->reserved = std::exchange(n.reserved, 0);
    this->src = std::exchange(n.src, nullptr);
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
    return reinterpret_cast<char *> (this->src);
}

const char * manapi::bytebuffer::data() const {
    return reinterpret_cast<char *> (this->src);
}

std::size_t manapi::bytebuffer::size() const {
    return this->s;
}

void manapi::bytebuffer::resize(std::size_t s) {
    if (this->reserved >= s) {
        this->s = s;
        return;
    }

    if (this->src) {
        this->src = manapi::memory::realloc(this->src, s);
    }
    else {
        this->src = manapi::memory::alloc<uint8_t>(s);
    }
    this->reserved = s;
    this->s = s;
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
    return std::exchange(this->src, nullptr);
}

bool manapi::bytebuffer::empty() const {
    return !this->size();
}
