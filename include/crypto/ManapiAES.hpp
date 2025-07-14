/**
 * @file crypto/ManapiAES.hpp
 * @brief Provides AES encoders and decoders
 *
 * @author Timur Zajnullin
 * @author OpenSSL team
 * @author WolfSSL team
 */

#pragma once

#include <string>

#include "./ManapiCryptoUtils.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::crypto {
    /**
     * AES string encoder
     *
     * @param data the source string
     * @param key the key string
     * @param iv the iv string
     * @param algorithm the encoding algorithm
     * @return the encoded output string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted, Unimplemented
     */
    manapi::error::status_or<std::string> aes_encrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC);

    /**
     * AES string decoder
     *
     * @param data the encoded string
     * @param key the key string
     * @param iv the iv string
     * @param algorithm the encoding algorithm
     * @return the decoded string, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted, Unimplemented
     */
    manapi::error::status_or<std::string> aes_decrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC);
}