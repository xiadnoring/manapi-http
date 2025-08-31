/**
 * @file ManapiProcess.hpp
 * @brief Provides os utilities
 *
 */

#pragma once

#include <string>

#include "./ManapiErrors.hpp"
#include "./ManapiEventStructures.hpp"

namespace manapi::process {
    /**
     * Set a process environment
     *
     * @param name name of the environment
     * @param value value of the environment
     * @return
     */
    DLLExportImport manapi::sys_error::status set_env (std::string_view name, std::string_view value);

    /**
     * Get a process environment
     *
     * @param name name of the environment
     * @return value of the environment
     */
    DLLExportImport manapi::sys_error::status_or<std::string> get_env (std::string_view name);
}
