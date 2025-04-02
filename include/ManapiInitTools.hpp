#pragma once



#include <atomic>
#include <mutex>
#include "ManapiUtils.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
# include <openssl/ssl.h>
#endif
#include "ManapiUtils.hpp"

namespace manapi::init_tools {
    void ssl_library_init ();
    void ev_library_init ();
    void curl_library_init ();
}
