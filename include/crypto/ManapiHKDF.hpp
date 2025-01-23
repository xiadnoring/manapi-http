#pragma once

#include <array>
#include <stdexcept>
#include <string>

#include "ManapiCryptoUtils.hpp"
#include "ManapiHttpTypes.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#endif

namespace manapi::crypto {
    inline std::string hmac_digest (std::string_view key, std::string_view data, hashes hash_algorithm = hashes::SHA_256) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
        std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
        unsigned int hashLen;

        const EVP_MD *algorithm;
        switch (hash_algorithm) {
            case SHA_256:
                algorithm = EVP_sha256();
            break;
            case SHA_512:
                algorithm = EVP_sha512();
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "Available: [SHA256, SHA512]");
        }

        HMAC(
            algorithm,
            key.data(),
            static_cast<int>(key.size()),
            reinterpret_cast<unsigned char const *> (data.data()),
            static_cast<int>(data.size()),
            hash.data(),
            &hashLen
        );

        return std::move(std::string{reinterpret_cast<char const*>(hash.data()), hashLen});
#else
        throw std::runtime_error("openssl or wolfssl needed for crypto_hmac_diggest(...)");
#endif
    }

    inline std::string hkdf_extract (std::string_view salt, std::string_view ikm, hashes hash = hashes::SHA_256) {
        if (salt.empty()) {
            std::string nsalt = manapi::crypto::random_string(8);
            return std::move(hmac_digest(nsalt, ikm, hash));
        }
        return std::move(hmac_digest(salt, ikm, hash));
    }

    inline std::string hkdf_expand (std::string_view prk, std::string_view info, int length, hashes algorithm = hashes::SHA_256) {
        std::string t;
        std::string okm;
        int i = 0;
        std::string ninfo;
        while (okm.size() < length) {
            i ++;
            ninfo = t;
            ninfo += info;
            ninfo += static_cast<char> (i);
            t = hmac_digest(prk, ninfo, algorithm);
            okm += t;
        }
        return std::move(okm.substr(0, length));
    }

    inline std::string hkdf (std::string_view salt, std::string_view ikm, std::string_view info, int length, hashes hash = hashes::SHA_256) {
        auto prk = hkdf_extract(salt, ikm);
        return std::move(hkdf_expand(prk, info, length));
    }
}