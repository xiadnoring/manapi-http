/**
 * @file crypto/ManapiCryptoUtils.hpp
 * @brief Provides base crypto utilities
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <random>

#include "../ManapiUtils.hpp"
#include "../fs/ManapiFileStream.hpp"

namespace manapi::crypto {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    struct evp_cipher_deleter {
        void operator()(void *ptr);
    };
#endif
#if MANAPIHTTP_WOLFSSL_DEPENDENCY
    struct wolfssl_evp_cipher_deleter {
        void operator()(void *ptr);
    };
#endif
    /**
     * Provides cipher algorithms
     */
    enum ciphers {
        AES_256_GCM,
        AES_128_GCM,
        AES_128_CBC,
        AES_256_CBC,
        AES_128_ECB,
        AES_256_ECB
    };

    /**
     * Provides hash algorithms
     */
    enum hashes {
        SHA_256,
        SHA_128,
        SHA_512
    };

    /**
     * Get randomly generate string
     *
     * @param buff the buffer
     * @param len the size of the buffer
     * @param cancellation the cancellation token
     * @return Ok if there's no error, otherwise it returns Internal, ResourceExhausted, Cancelled
     */
    manapi::future<manapi::status> async_random_string (char *buff, size_t len, ctoken cancellation = nullptr);

    /**
     * Get randomly generated string
     *
     * @param len the size of the output string
     * @return the randomly generated string, but otherwise, it returns Internal, ResourceExhausted, InvalidArgument
     */
    manapi::status_or<std::string> random_string (std::size_t len);

    /**
     * convert the decimal string to the heximal string
     *
     * @param input the decimal string
     * @return the heximal string, on error it returns ResourceExhausted
     */
    manapi::status_or<std::string> strdec2strhex(std::string_view input);

    /**
     * convert the heximal string to the decimal string
     *
     * @param hex the heximal string
     * @return the decimal string, on error it returns ResourceExhausted
     */
    manapi::status_or<std::string> strhex2strdec (std::string_view hex);


    /**
     * print integer into the bytes
     *
     * @tparam T integer type
     * @param n the source integer
     * @return the output string, on error it returns ResourceExhausted, InternalError
     */
    template <typename T>
    manapi::status_or<std::string> number2bytes (T n) {
        try {
            std::string result;
            result.reserve(sizeof (n));
            for (int i = sizeof (n) - 1; i >= 0; --i) {
                result += static_cast<char> ((n >> i * 8) & 0xFF);
            }
            return std::move(result);
        }
        catch (std::bad_alloc const &) {
            return status_resource_exhausted();
        }
        catch (std::exception const &) {
            return status_internal();
        }
    }
}
