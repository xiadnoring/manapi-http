/**
 * @file crypto/ManapiBase64.hpp
 * @brief Provides base64 encoders and decoders
 *
 * @author Timur Zajnullin
 * @author tobiaslocker from github
 */

#pragma once

#include <string>
#include <string_view>

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::crypto {
    /**
     * base64 string encoder
     *
     * @param data the source string
     * @return the base64 encoded output string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted
     */
    manapi::status_or<std::string> base64_encode (std::string_view data);

    /**
     * base64 string decoder
     *
     * @param data the base64 encoded string
     * @return the decoded output string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted
     */
    manapi::status_or<std::string> base64_decode (std::string_view data);
}