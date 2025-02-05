#pragma once

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <wincrypt.h>
#endif

#include <string>
#include <random>
#include <filesystem>
#include <fstream>

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
    inline std::string _random_string (std::string &rnd, const size_t &len) {
#ifdef _WIN32
        HCRYPTPROV h_crypt_prov;
        if (CryptAcquireContext(&h_crypt_prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
                PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            if (CryptGenRandom(h_crypt_prov, (DWORD) len, (BYTE *) rnd.data()))
            {
                if (!CryptReleaseContext(h_crypt_prov, 0))
                {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_ALGORITHM_INIT_FAIL, "Error during CryptReleaseContext.");
                }
            }
            else
            {
                if (CryptReleaseContext(h_crypt_prov, 0))
                {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_ALGORITHM_INIT_FAIL, "Error during CryptGenRandom.");
                }
                else
                {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_ALGORITHM_INIT_FAIL, "Error during CryptReleaseContext.");
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
        return std::move(rnd);
    }
    inline manapi::future<std::string> random_string_async (const std::shared_ptr<async::context> &ctx, const size_t &len) {
        std::string rnd;
        rnd.resize(len);
#ifdef _WIN32

#else
        if (std::filesystem::exists("/dev/urandom")) {
            filesystem::async::fstream input (ctx, "/dev/urandom");
            co_await input.open(manapi::filesystem::async::fstream::FILE_READ);

            if (!input.is_open()) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_FILE_IO, "failed to get the random string from the /dev/urandom");
            }

            co_await input.fread(rnd.data(), static_cast<std::streamsize> (rnd.size()));
            co_await input.close();

            co_return std::move(rnd);
        }

        co_return manapi::crypto::_random_string(rnd, len);
#endif
    }

    inline std::string random_string (const size_t &len) {
        std::string rnd;
        rnd.resize(len);

        return manapi::crypto::_random_string(rnd, len);
    }

    inline std::string strdec2strhex(std::string_view input)
    {
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

    inline std::string strdec2strhex(const std::string &input) {
        return std::move(strdec2strhex(std::string_view{input}));
    }

    inline std::string strhex2strdec (std::string_view hex) {
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

    inline std::string strhex2strdec (const std::string &hex) {
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

    template <typename T>
    inline std::string number2bytes (T n) {
        std::string result;
        result.reserve(sizeof (n));
        for (int i = sizeof (n) - 1; i >= 0; --i) {
            result += static_cast<char> ((n >> i * 8) & 0xFF);
        }
        return std::move(result);
    }

    inline int binpow(int a, int n) {
        int res = 1;
        while (n != 0) {
            if (n & 1)
                res = res * a;
            a = a * a;
            n >>= 1;
        }
        return res;
    }
}
