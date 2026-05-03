#include <filesystem>

#include "crypto/ManapiCryptoUtils.hpp"
#include "../include/ManapiWindows.hpp"
#include "../include/ManapiDefaultErrors.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#   include <openssl/ssl.h>
#   include <openssl/evp.h>
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY && !MANAPIHTTP_OPENSSL_DEPENDENCY
#   include <wolfssl/openssl/ssl.h>
#   include <wolfssl/openssl/evp.h>
#endif

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include "Wincrypt.h"
#endif

manapi::status random_string_ (char *rnd, std::size_t len) {
    try {
#ifdef _WIN32
        HCRYPTPROV h_crypt_prov;
        if (CryptAcquireContext(&h_crypt_prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
                PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            if (CryptGenRandom(h_crypt_prov, (DWORD) len, (BYTE *) rnd))
            {
                if (!CryptReleaseContext(h_crypt_prov, 0))
                    return manapi::status_failed_precondition ("CryptReleaseContext() failed");
            }
            else
            {
                if (CryptReleaseContext(h_crypt_prov, 0))
                    return manapi::status_failed_precondition ("CryptGenRandom() failed");
                else
                    return manapi::status_failed_precondition ("CryptReleaseContext() failed");
            }
        }
#else
        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist256(0,255);

        for (size_t i = 0; i < len; i++)
            rnd[i] = static_cast <char> (dist256(rng));
#endif
        return manapi::status_ok();
    }
    catch (std::bad_alloc const &) {
        return manapi::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "random_string_:Failed", e.what());
    }
    return manapi::status_internal("random_string_:Failed");
}

#if MANAPIHTTP_OPENSSL_DEPENDENCY
void manapi::crypto::evp_cipher_deleter::operator()(void *ptr) {
    EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(ptr));
}
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY && !MANAPIHTTP_OPENSSL_DEPENDENCY
void manapi::crypto::wolfssl_evp_cipher_deleter::operator()(void *ptr) {
    wolfSSL_EVP_CIPHER_CTX_free(static_cast<WOLFSSL_EVP_CIPHER_CTX *>(ptr));
}
#endif

manapi::future<manapi::status> manapi::crypto::async_random_string(char *buff, std::size_t len, ctoken cancellation) {
    if (!len)
        co_return status_ok();

    typedef manapi::async::promise_sync<manapi::status> promise;
    manapi::status res;
    try {
        res = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) mutable
            -> void {
            try {
                auto w = manapi::async::current()->eventloop()->create_watcher_random(buff, len, [resolve, reject] (const std::shared_ptr<ev::random> &w, int status, void *buff, std::size_t size) mutable
                    -> void {
                        if (status) {
                            resolve(manapi::status_internal("random() failed"));
                            return;
                        }
                        resolve(manapi::status_ok());
                }).unwrap();
                if (cancellation.contains_cancel_callback()) {
                    cancellation.cancel_callback([w, resolve = std::move(resolve)] () mutable
                        -> void {
                        manapi::async::current()->eventloop()->stop_watcher(std::move(w));
                        resolve(manapi::status_cancelled());
                    });
                }
            }
            catch (...) {
                reject(std::current_exception());
            }

            cancellation.disable();
        });
    }
    catch (std::bad_alloc const &) {
        res = status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "async_random_string:Failed", e.what());
        res = status_internal("async_random_string:Failed");
    }
    co_return std::move(res);
}

manapi::status_or<std::string> manapi::crypto::random_string(std::size_t len) {
    try {
        std::string rnd;
        rnd.resize(len);

        auto err = random_string_(rnd.data(), rnd.size());
        if (err.ok())
            return std::move(rnd);

        return std::move(err);
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "random_string:Failed", e.what());
    }
    return status_internal("random_string:Failed");
}

manapi::status_or<std::string> manapi::crypto::strdec2strhex(std::string_view input) {
    try {
        static const char hex_digits[] = "0123456789ABCDEF";
        std::string output;
        output.reserve(input.length() * 2);
        for (unsigned char c : input)
        {
            output.push_back(hex_digits[c >> 4]);
            output.push_back(hex_digits[c & 15]);
        }
        return std::move(output);
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
}

static int chrhex2chrdec (char &a) {
    if (isdigit(a)) {
        a -= '0';
    }
    else if (a >= 'A' && a <= 'F') {
        a -= 'A'-10;
    }
    else if (a >= 'a' && a <= 'f') {
        a -= 'a'-10;
    }
    else {
        return 1;
    }
    return 0;
}

manapi::status_or<std::string> manapi::crypto::strhex2strdec(std::string_view hex) {
    try {
        auto len = hex.length();
        if (len % 2 != 0) {
            goto err;
        }
        std::string newString;
        newString.reserve(len / 2);
        for (std::size_t i = 0; i < hex.length(); i+=2) {
            char a = hex[i];
            char b = hex[i + 1];

            if (chrhex2chrdec(a) || chrhex2chrdec(b))
                goto err;

            newString.push_back(static_cast<char>((static_cast<uint8_t>(a) << 4) | static_cast<uint8_t>(b)));
        }
        return std::move(newString);
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
err:
    return status_invalid_argument("hex string invalid");
}

// int manapi::crypto::binpow(int a, int n) {
//     int res = 1;
//     while (n != 0) {
//         if (n & 1)
//             res = res * a;
//         a = a * a;
//         n >>= 1;
//     }
//     return res;
// }

