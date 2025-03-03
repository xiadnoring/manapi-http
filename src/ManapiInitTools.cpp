#include <iostream>
#include <memory>

#include "ManapiInitTools.hpp"
#include "extensions/ev++.h"
#include "curl/curl.h"

static std::atomic<bool> openssl_gl_init = false;

void manapi::init_tools::ssl_library_init() {
    auto value = openssl_gl_init.exchange(true);

    if (!value) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
        OpenSSL_add_ssl_algorithms();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
#endif
    }
}
void manapi::init_tools::ev_library_init() {
    ev::set_allocator([] (void *ptr, long size) noexcept
        -> void * {
        if (ptr) {
            return ::realloc(ptr, size);
        }

        return ::malloc(size);
    });
}

void manapi::init_tools::curl_library_init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}
