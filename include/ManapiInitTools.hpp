#pragma once



#include <atomic>
#include <mutex>
#include "ManapiUtils.hpp"

namespace manapi::init_tools {
    void ssl_library_init ();
    void ev_library_init ();
    void curl_library_init ();
}
