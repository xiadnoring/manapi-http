/**
 * @file ManapiInitTools.hpp
 * @brief Provides initial utilities
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <atomic>
#include <mutex>

#include "./ManapiDebug.hpp"
#include "./ManapiUtils.hpp"

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

    /**
     * Enable Log Trace
     */
    void log_trace_init (manapi::debug::trace_level lvl);
}

namespace manapi::clear_tools {
    void grpc_clear () MANAPIHTTP_NOEXCEPT;

    void ssl_library_thread_clear () MANAPIHTTP_NOEXCEPT;

    void ssl_library_clear () MANAPIHTTP_NOEXCEPT;

    void ev_library_clear () MANAPIHTTP_NOEXCEPT;

    void curl_library_clear () MANAPIHTTP_NOEXCEPT;
}