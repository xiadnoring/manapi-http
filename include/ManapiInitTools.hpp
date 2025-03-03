#pragma once



#include <atomic>
#include <mutex>

#if MANAPIHTTP_OPENSSL_DEPENDENCY
# include <openssl/ssl.h>
#endif
#include "ManapiUtils.hpp"

namespace manapi::init_tools {
    void ssl_library_init ();
    void ev_library_init ();
    void curl_library_init ();
}
