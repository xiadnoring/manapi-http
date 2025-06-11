#include <mutex>
#include <stack>

#include "ManapiUtils.hpp"

void manapi::memory::free(void *p) {
    ::free(p);
}

void *manapi::memory::internal::alloc(std::size_t size) {
    return ::malloc(size);
}

void *manapi::memory::internal::realloc(void *n,std::size_t size) {
    return ::realloc(n, size);
}

