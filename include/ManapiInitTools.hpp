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
     * Initialize grpc library
     */
    void grpc_library_init ();

    /**
     * Enable Log Trace
     */
    void log_trace_init (manapi::debug::trace_level lvl);

    /**
     * Enable Log
     */
    void log_name_enable (const char *name, bool enable);

    /**
     * Max Coroutines Stack
     */
    void max_coro_stack (std::size_t sz) MANAPIHTTP_NOEXCEPT;
}

namespace manapi::clear_tools {
    void clear_all () MANAPIHTTP_NOEXCEPT;

    void clear_thread_all () MANAPIHTTP_NOEXCEPT;

    void grpc_clear () MANAPIHTTP_NOEXCEPT;

    void grpc_thread_clear () MANAPIHTTP_NOEXCEPT;

    void ssl_library_thread_clear () MANAPIHTTP_NOEXCEPT;

    void ssl_library_clear () MANAPIHTTP_NOEXCEPT;

    void ev_library_clear () MANAPIHTTP_NOEXCEPT;

    void curl_library_clear () MANAPIHTTP_NOEXCEPT;
}