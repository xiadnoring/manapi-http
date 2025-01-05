#pragma once

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#include <atomic>
#include <mutex>

#include <openssl/ssl.h>

namespace manapi::net::worker::tools {
    static std::atomic<bool> openssl_gl_init = false;

    inline void ssl_library_init () {
        auto value = openssl_gl_init.exchange(true);

        if (!value) {
            OpenSSL_add_ssl_algorithms();
            SSL_load_error_strings();
            SSLeay_add_ssl_algorithms();
            openssl_gl_init.store(true);
        }
    }
}

#endif