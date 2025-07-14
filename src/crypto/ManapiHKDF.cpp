#include "crypto/ManapiHKDF.hpp"
#include "../include/ManapiUtils.hpp"

#include <array>
#include <stdexcept>

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#endif
manapi::error::status_or<std::string> manapi::crypto::hmac_digest(std::string_view key, std::string_view data, hashes hash_algorithm) {
    try {
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
                return error::status_invalid_argument("algorithm invalid");
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
        return error::status_unimplemented("openssl or wolfssl is required");
#endif
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "hmac:Failed", e.what());
    }
    return error::status_internal("hmac:Failed");
}

manapi::error::status_or<std::string> manapi::crypto::hkdf_extract(std::string_view salt, std::string_view ikm, hashes hash) {
    if (salt.empty()) {
        auto res = manapi::crypto::random_string(8);
        if (!res.ok())
            return res.err();
        std::string nsalt = res.unwrap();
        return std::move(hmac_digest(nsalt, ikm, hash));
    }
    return std::move(hmac_digest(salt, ikm, hash));
}

manapi::error::status_or<std::string> manapi::crypto::hkdf_expand(std::string_view prk, std::string_view info, int length, hashes algorithm) {
    try {
        std::string t;
        std::string okm;
        int i = 0;
        std::string ninfo;
        while (okm.size() < length) {
            i ++;
            ninfo = t;
            ninfo += info;
            ninfo += static_cast<char> (i);
            auto res = hmac_digest(prk, ninfo, algorithm);
            if (!res.ok())
                return std::move(res.err());
            t = res.unwrap();
            okm += t;
        }
        okm.resize(length);
        return std::move(okm);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        return error::status_internal();
    }
}

manapi::error::status_or<std::string> manapi::crypto::hkdf(std::string_view salt, std::string_view ikm, std::string_view info, int length, hashes hash) {
    auto res = hkdf_extract(salt, ikm);
    if (!res.ok())
        return res.err();
    auto prk = res.unwrap();
    return std::move(hkdf_expand(prk, info, length));
}

