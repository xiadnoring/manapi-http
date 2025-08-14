#include <mutex>
#include <stack>

#include "./include/ManapiUtils.hpp"

void manapi::memory::free(void *p) MANAPIHTTP_NOEXCEPT {
    ::free(p);
}

void *manapi::memory::internal::alloc(std::size_t size) MANAPIHTTP_NOEXCEPT {
    return ::malloc(size);
}

void *manapi::memory::internal::realloc(void *n,std::size_t size) MANAPIHTTP_NOEXCEPT {
    return ::realloc(n, size);
}

