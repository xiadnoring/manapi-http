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
    DLLExportImport void ssl_library_init ();

    /**
     * Initialize libuv library
     */
    DLLExportImport void ev_library_init ();

    /**
     * Initialize curl library
     */
    DLLExportImport void curl_library_init ();

    /**
     * Enable Log Trace
     */
    DLLExportImport void log_trace_init (manapi::debug::trace_level lvl);
}

namespace manapi::clear_tools {
    DLLExportImport void ssl_library_thread_clear () MANAPIHTTP_NOEXCEPT;

    DLLExportImport void ssl_library_clear () MANAPIHTTP_NOEXCEPT;

    DLLExportImport void ev_library_clear () MANAPIHTTP_NOEXCEPT;

    DLLExportImport void curl_library_clear () MANAPIHTTP_NOEXCEPT;
}