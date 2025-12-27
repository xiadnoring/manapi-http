/**
 * @file crypto/ManapiAEAD.hpp
 * @brief Provides AEAD encoders and decoders
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
     * AEAS string encoder
     *
     * @param cipher_password the password string
     * @param aad the aad string
     * @param key the key string
     * @param iv the iv string
     * @param tag the tag string
     * @param algorithm the encoding algorithm
     * @return the encoded string if there's no error, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted, Unimplemented
     */
    manapi::status_or<std::string> aead_decrypt (std::string_view cipher_password, std::string_view aad, std::string_view key, std::string_view iv, std::string_view tag, ciphers algorithm = ciphers::AES_256_GCM);

    /**
     * AEAS string decoder
     *
     * @param data the encoded string
     * @param aad the aad string
     * @param key the key string
     * @param iv the iv string
     * @param tag the tag string
     * @param algorithm the encoding algorithm
     * @return the decoded string if there's no error, but otherwise, it returns InternalError, InvalidArgument, ResourceExhausted, Unimplemented
     */
    manapi::status_or<std::string> aead_encrypt (std::string_view data, std::string_view aad, std::string_view key, std::string_view iv, std::string &tag, ciphers algorithm = ciphers::AES_256_GCM);
}