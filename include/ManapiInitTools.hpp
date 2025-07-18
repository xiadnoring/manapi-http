/**
 * @file ManapiInitTools.hpp
 * @brief Provides initial utilities
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <atomic>
#include <mutex>

#include "ManapiUtils.hpp"

/**
 * Namespace with initial utilities
 */
namespace manapi::init_tools {
    /**
     * Initialize all ssl libraries
     */
    void ssl_library_init ();

    /**
     * Initialize libuv library
     */
    void ev_library_init ();

    /**
     * Initialize curl library
     */
    void curl_library_init ();
}

namespace manapi::clear_tools {
    void ssl_library_thread_clear () MANAPIHTTP_NOEXPECT;

    void ssl_library_clear () MANAPIHTTP_NOEXPECT;

    void ev_library_clear () MANAPIHTTP_NOEXPECT;

    void curl_library_clear () MANAPIHTTP_NOEXPECT;
}