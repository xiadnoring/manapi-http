/**
 * @file crypto/ManapiBase64.hpp
 * @brief Provides base64 encoders and decoders
 *
 * @author Timur Zajnullin
 * @author tobiaslocker from github
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <string>
#include <string_view>

#if defined(__cpp_lib_bit_cast)
#include <bit>  // For std::bit_cast.
#endif

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::crypto {
    /**
     * base64 string encoder
     *
     * @param data the source string
     * @return the base64 encoded output string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted
     */
    DLLExportImport manapi::error::status_or<std::string> base64_encode (std::string_view data);

    /**
     * base64 string decoder
     *
     * @param data the base64 encoded string
     * @return the decoded output string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted
     */
    DLLExportImport manapi::error::status_or<std::string> base64_decode (std::string_view data);
}