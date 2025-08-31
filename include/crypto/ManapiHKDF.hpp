/**
 * @file crypto/ManapiHKDF.hpp
 * @brief Provides HKDF Utilities
 *
 * @author Timur Zajnullin
 * @author OpenSSL team
 */

#pragma once

#include <string>

#include "../ManapiUtils.hpp"
#include "./ManapiCryptoUtils.hpp"
#include "../http/ManapiHttpTypes.hpp"

namespace manapi::crypto {
    /**
     * HMAC string encoder
     *
     * @param key the key string
     * @param data the source string
     * @param hash_algorithm the hash algorithm
     * @return the encoded string, on error it returns InternalError, ResourceExhausted, InvalidArgument
     */
    DLLExportImport manapi::error::status_or<std::string> hmac_digest (std::string_view key, std::string_view data, hashes hash_algorithm = hashes::SHA_256);

    /**
     * HKDF string extract
     * @param salt the salt string
     * @param ikm the ikm string
     * @param hash the hash algorithm
     * @return the extracted string, on error it returns InternalError, ResourceExhausted, InvalidArgument
     */
    DLLExportImport manapi::error::status_or<std::string> hkdf_extract (std::string_view salt, std::string_view ikm, hashes hash = hashes::SHA_256);

    /**
     * HKDF string expand
     * @param prk the prk string
     * @param info the info string
     * @param length the size of the output
     * @param algorithm the hash algorithm
     * @return the expanded string, on error it returns InternalError, ResourceExhausted, InvalidArgument
     */
    DLLExportImport manapi::error::status_or<std::string> hkdf_expand (std::string_view prk, std::string_view info, int length, hashes algorithm = hashes::SHA_256);

    /**
     * HKDF string encoder
     * @param salt the salt string
     * @param ikm the ikm string
     * @param info the source string
     * @param length the size of the output
     * @param hash the hash algorithm
     * @return the generated string, on error it returns InternalError, ResourceExhausted, InvalidArgument
     */
    DLLExportImport manapi::error::status_or<std::string> hkdf (std::string_view salt, std::string_view ikm, std::string_view info, int length, hashes hash = hashes::SHA_256);
}