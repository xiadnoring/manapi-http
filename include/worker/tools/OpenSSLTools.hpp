#pragma once



#include <atomic>
#include <mutex>

#if MANAPIHTTP_OPENSSL_DEPENDENCY
# include <openssl/ssl.h>
#endif
#include "../../ManapiUtils.hpp"

namespace manapi::net::worker::tools {
    static std::atomic<bool> openssl_gl_init = false;

    inline void ssl_library_init () {
        auto value = openssl_gl_init.exchange(true);

        if (!value) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
            OpenSSL_add_ssl_algorithms();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            openssl_gl_init.store(true);
#endif
        }
    }
}
