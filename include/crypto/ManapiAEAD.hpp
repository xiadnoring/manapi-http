#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "ManapiCryptoUtils.hpp"
#include "ManapiUtils.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#endif

namespace manapi::crypto {
    inline std::string aead_decrypt (std::string_view cipher_password, std::string_view aad, std::string_view key, std::string_view iv, std::string_view tag, ciphers algorithm = ciphers::AES_256_GCM) {
        std::string plaintext;
#if MANAPIHTTP_OPENSSL_DEPENDENCY

        plaintext.resize(cipher_password.size());

        const EVP_CIPHER *cipher;

        switch (algorithm) {
            case AES_256_GCM:
                cipher = EVP_aes_256_gcm();
            break;
            case AES_128_GCM:
                cipher = EVP_aes_128_gcm();
            break;
            case AES_128_CBC:
                cipher = EVP_aes_128_cbc();
            break;
            case AES_256_CBC:
                cipher = EVP_aes_256_cbc();
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "Available: [AES_256_GCM, AES_128_GCM]");
        }

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        before_delete ctx_clean ([ctx] () -> void {
            EVP_CIPHER_CTX_free(ctx);
        });

        if (!ctx) {
            throw std::runtime_error ("ctx = EVP_CIPHER_CTX_new() failure");
        }

        if (!EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr)) {
            throw std::runtime_error ("EVP_DecryptInit_ex(...) failure #1");
        }

        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr)) {
            throw std::runtime_error ("EVP_DecryptInit_ex(...) failure #1");
        }

        if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast <const unsigned char *>(key.data()), reinterpret_cast< const unsigned char *>(iv.data()))) {
            throw std::runtime_error("EVP_DecryptInit_ex(...) failure #2");
        }

        int outlen = 0;
        int len;
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, reinterpret_cast<const uint8_t *>(aad.data()), static_cast<int>(aad.size()))) {
            throw std::runtime_error("EVP_DecryptUpdate(...) failure, set aad failed");
        }

        if (!EVP_DecryptUpdate(ctx, reinterpret_cast<uint8_t *>(plaintext.data()), &len, reinterpret_cast<const uint8_t *>(cipher_password.data()), static_cast<int>(cipher_password.size()))) {
            throw std::runtime_error("EVP_DecryptUpdate(...) failure, decrypt data");
        }

        outlen += len;

        int taglen = static_cast<int>(tag.size());
        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, taglen, (void*)(tag.data()) )) {
            throw std::runtime_error("EVP_CIPHER_CTX_ctrl(...) failure");
        }

        std::string last;
        int last_len = EVP_CIPHER_CTX_block_size(ctx);
        last.resize(last_len);
        int rhs = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(last.data()), &len);
        if (1 != rhs) {
            throw std::runtime_error("EVP_DecryptFinal_ex(...) failure");
        }

        last.resize(len);
        plaintext.resize(outlen);

        plaintext += last;

#else
        throw std::runtime_error("openssl or wolfssl needed for crypto_aead_decrypt(...)");
#endif
        return std::move(plaintext);
    }

    inline std::string aead_encrypt (std::string_view data, std::string_view aad, std::string_view key, std::string_view iv, std::string &tag, ciphers algorithm = ciphers::AES_256_GCM) {
        std::string plaintext;
#if MANAPIHTTP_OPENSSL_DEPENDENCY

        plaintext.resize(data.size());

        const EVP_CIPHER *cipher;

        switch (algorithm) {
            case AES_256_GCM:
                cipher = EVP_aes_256_gcm();
            break;
            case AES_128_GCM:
                cipher = EVP_aes_128_gcm();
            break;
            case AES_128_CBC:
                cipher = EVP_aes_128_cbc();
            break;
            case AES_256_CBC:
                cipher = EVP_aes_256_cbc();
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION2(ERR_ALGORITHM_NO_SUPPORT, "Available: [AES_256_GCM, AES_128_GCM, AES_128_CBC, AES_256_CBC]");
        }

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        before_delete ctx_clean ([ctx] () -> void {
            EVP_CIPHER_CTX_free(ctx);
        });

        if (!ctx) {
            throw std::runtime_error ("ctx = EVP_CIPHER_CTX_new() failure");
        }

        if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr)) {
            throw std::runtime_error ("EVP_EncryptInit_ex(...) failure #1");
        }

        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr)) {
            throw std::runtime_error ("EVP_EncryptInit_ex(...) failure #1");
        }

        if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast <const unsigned char *>(key.data()), reinterpret_cast< const unsigned char *>(iv.data()))) {
            throw std::runtime_error("EVP_EncryptInit_ex(...) failure #2");
        }

        int outlen = 0;
        int len;
        if (!EVP_EncryptUpdate(ctx, nullptr, &len, reinterpret_cast<const uint8_t *>(aad.data()), static_cast<int>(aad.size()))) {
            throw std::runtime_error("EVP_EncryptUpdate(...) failure, set aad failed");
        }

        if (!EVP_EncryptUpdate(ctx, reinterpret_cast<uint8_t *>(plaintext.data()+outlen), &len, reinterpret_cast<const uint8_t *>(data.data()), static_cast<int>(data.size()))) {
            throw std::runtime_error("EVP_EncryptUpdate(...) failure, encrypt data");
        }

        outlen += len;

        std::string last;
        last.resize(EVP_CIPHER_CTX_block_size(ctx));
        int rhs = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(last.data()), &len);
        if (1 != rhs) {
            throw std::runtime_error("EVP_EncryptFinal_ex(...) failure");
        }

        last.resize(len);
        plaintext.resize(outlen);

        const int taglen = 16;
        tag.resize(taglen);
        if(1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, taglen, tag.data())) {
            throw std::runtime_error("EVP_CIPHER_CTX_ctrl(...) failure. Failed to get the tag");
        }

        plaintext += last;

#else
        throw std::runtime_error("openssl or wolfssl needed for crypto_aead_decrypt(...)");
#endif
        return std::move(plaintext);
    }
}