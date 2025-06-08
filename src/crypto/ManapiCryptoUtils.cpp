#include "crypto/ManapiCryptoUtils.hpp"
#include "../include/ManapiWindows.hpp"
#include "../include/ManapiDefaultErrors.hpp"

void random_string_ (char *rnd, std::size_t len) {
#ifdef _WIN32
    HCRYPTPROV h_crypt_prov;
    if (CryptAcquireContext(&h_crypt_prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
            PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        if (CryptGenRandom(h_crypt_prov, (DWORD) len, (BYTE *) rnd))
        {
            if (!CryptReleaseContext(h_crypt_prov, 0))
            {
                THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_ALGORITHM_INIT_FAIL, "Error during CryptReleaseContext.");
            }
        }
        else
        {
            if (CryptReleaseContext(h_crypt_prov, 0))
            {
                THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_ALGORITHM_INIT_FAIL, "Error during CryptGenRandom.");
            }
            else
            {
                THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_ALGORITHM_INIT_FAIL, "Error during CryptReleaseContext.");
            }
        }
    }
#else
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist256(0,255);

    for (size_t i = 0; i < len; i++)
    {
        rnd[i] = static_cast <char> (dist256(rng));
    }
#endif
}

manapi::future<void> manapi::crypto::async_random_string(async::shared_cthread ctx, char *buff, size_t len, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void, std::false_type>;
    co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) mutable
        -> void {
        try {
            auto w = manapi::async::current()->eventloop()->create_watcher_random(
                [resolve, reject] (std::shared_ptr<ev::random> &w, int status, void *buff, std::size_t size) mutable
                -> void {
                    if (status) {
                        reject(std::make_exception_ptr(manapi::exception(manapi::ERR_INTERNAL,
                           manapi::error::default_msgs[error::ERRMSG_RANDOM_STRING_FAILED], std::make_unique<json>(manapi::json{{"rhs", status}}))));
                        return;
                    }
                    resolve();
            }, buff, len);
            if (cancellation.contains_cancel_callback()) {
                cancellation.cancel_callback([w, resolve = std::move(resolve)] () mutable
                    -> void {
                    manapi::async::current()->eventloop()->stop_watcher(std::move(w));
                    resolve();
                });
            }
        }
        catch (...) {
            reject(std::current_exception());
        }
    });
    cancellation.disable_cancellation();
}

std::string manapi::crypto::random_string(const std::size_t &len) {
    std::string rnd;
    rnd.resize(len);

    random_string_(rnd.data(), rnd.size());
    return std::move(rnd);
}

std::string manapi::crypto::strdec2strhex(std::string_view input) {
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

std::string manapi::crypto::strdec2strhex(const std::string &input) {
    return std::move(strdec2strhex(std::string_view{input}));
}

std::string manapi::crypto::strhex2strdec(std::string_view hex) {
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

std::string manapi::crypto::strhex2strdec(const std::string &hex) {
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

int manapi::crypto::binpow(int a, int n) {
    int res = 1;
    while (n != 0) {
        if (n & 1)
            res = res * a;
        a = a * a;
        n >>= 1;
    }
    return res;
}

