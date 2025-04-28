#pragma once

#include <string>
#include <random>
#include <filesystem>
#include <fstream>

#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncFileStream.hpp"

namespace manapi::crypto {
    enum ciphers {
        AES_256_GCM,
        AES_128_GCM,
        AES_128_CBC,
        AES_256_CBC,
        AES_128_ECB,
        AES_256_ECB
    };
    enum hashes {
        SHA_256,
        SHA_128,
        SHA_512
    };

    manapi::future<void> async_random_string (std::shared_ptr<async::context> ctx, char *buff, size_t len, async::cancellation_action cancellation = nullptr);

    std::string random_string (const size_t &len);

    std::string strdec2strhex(std::string_view input);

    std::string strdec2strhex(const std::string &input);

    std::string strhex2strdec (std::string_view hex);

    std::string strhex2strdec (const std::string &hex);

    template <typename T>
    std::string number2bytes (T n) {
        std::string result;
        result.reserve(sizeof (n));
        for (int i = sizeof (n) - 1; i >= 0; --i) {
            result += static_cast<char> ((n >> i * 8) & 0xFF);
        }
        return std::move(result);
    }

    int binpow(int a, int n);
}
