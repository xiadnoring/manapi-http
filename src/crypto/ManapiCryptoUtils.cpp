#include "crypto/ManapiCryptoUtils.hpp"
#include "../include/ManapiWindows.hpp"
#include "../include/ManapiDefaultErrors.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#   include <openssl/ssl.h>
#   include <openssl/evp.h>
#endif

manapi::error::status random_string_ (char *rnd, std::size_t len) {
    try {
#ifdef _WIN32
        HCRYPTPROV h_crypt_prov;
        if (CryptAcquireContext(&h_crypt_prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
                PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            if (CryptGenRandom(h_crypt_prov, (DWORD) len, (BYTE *) rnd))
            {
                if (!CryptReleaseContext(h_crypt_prov, 0))
                    return manapi::error::status_failed_precondition ("CryptReleaseContext() failed");
            }
            else
            {
                if (CryptReleaseContext(h_crypt_prov, 0))
                    return manapi::error::status_failed_precondition ("CryptGenRandom() failed");
                else
                    return manapi::error::status_failed_precondition ("CryptReleaseContext() failed");
            }
        }
#else
        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist256(0,255);

        for (size_t i = 0; i < len; i++)
            rnd[i] = static_cast <char> (dist256(rng));
#endif
        return manapi::error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return manapi::error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "random_string_:Failed", e.what());
    }
    return manapi::error::status_internal("random_string_:Failed");
}

#if MANAPIHTTP_OPENSSL_DEPENDENCY
void manapi::crypto::evp_cipher_deleter::operator()(void *ptr) {
    EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(ptr));
}
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY
void manapi::crypto::wolfssl_evp_cipher_deleter::operator()(void *ptr) {
    wolfSSL_EVP_CIPHER_CTX_free(static_cast<WOLFSSL_EVP_CIPHER_CTX *>(ptr));
}
#endif

manapi::future<manapi::error::status> manapi::crypto::async_random_string(char *buff, std::size_t len, async::cancellation_action cancellation) {
    if (!len)
        co_return error::status_ok();

    using promise = manapi::async::promise_sync<manapi::error::status>;
    manapi::error::status res;
    try {
        res = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) mutable
            -> void {
            try {
                auto w = manapi::async::current()->eventloop()->create_watcher_random(buff, len, [resolve, reject] (const std::shared_ptr<ev::random> &w, int status, void *buff, std::size_t size) mutable
                    -> void {
                        if (status) {
                            resolve(manapi::error::status_internal("random() failed"));
                            return;
                        }
                        resolve(manapi::error::status_ok());
                });
                if (cancellation.contains_cancel_callback()) {
                    cancellation.cancel_callback([w, resolve = std::move(resolve)] () mutable
                        -> void {
                        manapi::async::current()->eventloop()->stop_watcher(std::move(w));
                        resolve(manapi::error::status_cancelled());
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
        res = error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "async_random_string:Failed", e.what());
        res = error::status_internal("async_random_string:Failed");
    }
    co_return std::move(res);
}

manapi::error::status_or<std::string> manapi::crypto::random_string(std::size_t len) {
    try {
        std::string rnd;
        rnd.resize(len);

        auto err = random_string_(rnd.data(), rnd.size());
        if (err.ok())
            return std::move(rnd);

        return std::move(err);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "random_string:Failed", e.what());
    }
    return error::status_internal("random_string:Failed");
}

manapi::error::status_or<std::string> manapi::crypto::strdec2strhex(std::string_view input) {
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
        return error::status_resource_exhausted();
    }
}


manapi::error::status_or<std::string> manapi::crypto::strhex2strdec(std::string_view hex) {
    try {
    auto len = hex.length();
    std::string newString;
    for(size_t i=0; i< len; i+=2)
    {
        std::string_view byte = hex.substr(i,2);
        char chr = (char) (int)strtol(static_cast<const char*>(byte.data()), nullptr, 16);
        newString.push_back(chr);
    }
        return std::move(newString);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
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

