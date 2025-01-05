#pragma once

#if _WIN32
#   include <windows.h>
#   include <wincrypt.h>
#endif

#include <string>
#include <random>
#include <filesystem>
#include <fstream>

namespace manapi::net::crypto {
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
    inline std::string random_string (const size_t &len) {
        std::string rnd;
        rnd.resize(len);
#ifdef _WIN32
        HCRYPTPROV h_crypt_prov;
        if (CryptAcquireContext(&h_crypt_prov, nullptr, "Microsoft Base Cryptographic Provider v1.0",
                PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            if (CryptGenRandom(h_crypt_prov, (DWORD) len, (BYTE *) rnd.data()))
            {
                if (!CryptReleaseContext(h_crypt_prov, 0))
                {
                    throw bcrypt::exception::gensalt ("{}(): Error during CryptReleaseContext.", __FUNCTION__);
                }
            }
            else
            {
                if (CryptReleaseContext(h_crypt_prov, 0))
                {
                    throw bcrypt::exception::gensalt ("{}(): Error during CryptGenRandom.", __FUNCTION__);
                }
                else
                {
                    throw bcrypt::exception::gensalt ("{}(): Error during CryptReleaseContext.", __FUNCTION__);
                }
            }
        }
#else
        if (std::filesystem::exists("/dev/urandom"))
        {
            std::ifstream input ("/dev/urandom", std::ios::binary | std::ios::in);
            if (!input.is_open()) {
                throw std::runtime_error (std::format("{}(): failed to get random string from /dev/urandom", __FUNCTION__));
            }
            input.read(rnd.data(), static_cast<std::streamsize> (rnd.size()));
            input.close();
        }
        else
        {
            std::random_device dev;
            std::mt19937 rng(dev());
            std::uniform_int_distribution<std::mt19937::result_type> dist256(0,255);

            for (size_t i = 0; i < len; i++)
            {
                rnd[i] = static_cast <char> (dist256(rng));
            }
        }
#endif
        return std::move(rnd);
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