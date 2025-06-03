#include <iostream>
#include <memory>

#include "ManapiInitTools.hpp"
#include "services/ManapiEventLoop.hpp"

static std::atomic<bool> openssl_gl_init = false;

#if MANAPIHTTP_OPENSSL_DEPENDENCY
namespace manapi::init_tools::openssl {
#   include <openssl/ssl.h>
}
#endif

void manapi::init_tools::ssl_library_init() {
    auto value = openssl_gl_init.exchange(true);

    if (!value) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
        openssl::OpenSSL_add_ssl_algorithms();
        openssl::SSL_load_error_strings();
        openssl::OpenSSL_add_all_algorithms();
#endif
    }
}
void manapi::init_tools::ev_library_init() {
    // ev::set_allocator([] (void *ptr, long size) noexcept
    //     -> void * {
    //     if (ptr) {
    //         return ::realloc(ptr, size);
    //     }
    //
    //     return ::malloc(size);
    // });
}

void manapi::init_tools::curl_library_init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}
