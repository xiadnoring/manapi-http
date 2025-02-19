#pragma once

#include <string>

#include "../ManapiUtils.hpp"
#include "./ManapiCryptoUtils.hpp"
#include "../ManapiUtils.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#endif

namespace manapi::crypto {
    inline std::string aes_encrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
        const EVP_CIPHER *algorithm_cb;
        switch (algorithm) {
            case AES_128_CBC:
                algorithm_cb = EVP_aes_128_cbc();
            break;
            case AES_128_GCM:
                algorithm_cb = EVP_aes_128_gcm();
            break;
            case AES_128_ECB:
                algorithm_cb = EVP_aes_128_ecb();
            break;
            case AES_256_CBC:
                algorithm_cb = EVP_aes_256_cbc();
            break;
            case AES_256_GCM:
                algorithm_cb = EVP_aes_256_gcm();
            break;
            case AES_256_ECB:
                algorithm_cb = EVP_aes_256_ecb();
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "Algorithm is invalid. "
                                                                      "Available: [AES_128_CBC, AES_128_GBC]");
        }
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        before_delete ctx_cleanup ([ctx] () -> void {
            EVP_CIPHER_CTX_free(ctx);
        });

        if (!EVP_EncryptInit_ex(ctx, algorithm_cb, nullptr, nullptr, nullptr)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_EncryptInit_ex(...) #1");
        }

        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_SET_KEY_LENGTH, static_cast<int>(key.size()), nullptr)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_CIPHER_CTX_set_key_length(...)");
        }

        if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast <const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(iv.data()))) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_EncryptInit_ex(...) #2");
        }

        EVP_CIPHER_CTX_set_padding(ctx, 1);

        std::string out;
        int out_len = 0;
        out.resize(EVP_CIPHER_CTX_block_size(ctx) + data.size());

        int len;
        if (1==EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()+out_len), &len, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()))) {
            out_len += len;
        }

        std::string last;
        last.resize(EVP_CIPHER_CTX_block_size(ctx));
        if (!EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(last.data()), &len)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_EncryptFinal_ex(...)");
        }
        last.resize(len);
        out.resize(out_len);
        out += last;

        return std::move(out);
#else
        THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "openssl or wolfssl is required");
#endif
    }

    inline std::string aes_decrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC) {
#if MANAPIHTTP_OPENSSL_DEPENDENCY
        const EVP_CIPHER *algorithm_cb;
        switch (algorithm) {
            case AES_128_CBC:
                algorithm_cb = EVP_aes_128_cbc();
            break;
            case AES_128_GCM:
                algorithm_cb = EVP_aes_128_gcm();
            break;
            case AES_128_ECB:
                algorithm_cb = EVP_aes_128_ecb();
            break;
            case AES_256_CBC:
                algorithm_cb = EVP_aes_256_cbc();
            break;
            case AES_256_GCM:
                algorithm_cb = EVP_aes_256_gcm();
            break;
            case AES_256_ECB:
                algorithm_cb = EVP_aes_256_ecb();
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "Algorithm is invalid. "
                                                                      "Available: [AES_128_CBC, AES_128_GBC]");
        }

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        before_delete ctx_cleanup ([ctx] () -> void {
            EVP_CIPHER_CTX_free(ctx);
        });

        if (!EVP_DecryptInit_ex(ctx, algorithm_cb, nullptr, nullptr, nullptr)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_DecryptInit_ex(...) #1");
        }

        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_SET_KEY_LENGTH, static_cast<int>(key.size()), nullptr)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_CIPHER_CTX_ctrl(...)");
        }

        if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast <const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(iv.data()))) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_DecryptInit_ex(...) #2");
        }

        EVP_CIPHER_CTX_set_padding(ctx, 1);

        std::string out;
        int out_len = 0;
        out.resize(data.size() + EVP_CIPHER_CTX_block_size(ctx));

        int len;
        if (1 == EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()+out_len), &len, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()))) {
            out_len += len;
        }

        std::string last;
        last.resize(EVP_CIPHER_CTX_block_size(ctx));
        if (!EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(last.data()), &len)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_INIT_FAIL, "EVP_DecryptFinal_ex(...)");
        }

        last.resize(len);
        out.resize(out_len);

        out += last;

        return std::move(out);
#else
        THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "openssl or wolfssl is required");
#endif
    }
}