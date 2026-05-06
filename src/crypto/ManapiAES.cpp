#include "crypto/ManapiAES.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "std/ManapiBeforeDelete.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY
# include <openssl/evp.h>
# include <openssl/err.h>
# include <openssl/crypto.h>
#endif


manapi::status_or<std::string> manapi::crypto::aes_encrypt(std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm) {
    try {
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
                return manapi::status_invalid_argument("Algorithm invalid");
        }
        std::unique_ptr<EVP_CIPHER_CTX, evp_cipher_deleter> ctx (EVP_CIPHER_CTX_new());
        auto n = ctx.get();

        if (!EVP_EncryptInit_ex(n, algorithm_cb, nullptr, nullptr, nullptr))
            return status_invalid_argument("EVP_EncryptInit_ex() failed");

        if (!EVP_CIPHER_CTX_ctrl(n, EVP_CTRL_SET_KEY_LENGTH, static_cast<int>(key.size()), nullptr))
            return status_invalid_argument("EVP_CIPHER_CTX_set_key_length() failed");

        if (!EVP_EncryptInit_ex(n, nullptr, nullptr,
                           reinterpret_cast <const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(iv.data())))
            return status_invalid_argument("EVP_EncryptInit_ex() failed");

        EVP_CIPHER_CTX_set_padding(n, 1);

        std::string out;
        int out_len = 0;
        out.resize(static_cast<std::size_t>(EVP_CIPHER_CTX_block_size(n)) + data.size());

        int len;
        if (1==EVP_EncryptUpdate(n, reinterpret_cast<unsigned char*>(out.data()+out_len), &len, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()))) {
            out_len += len;
        }

        std::string last;
        last.resize(static_cast<std::size_t>(EVP_CIPHER_CTX_block_size(n)));
        if (!EVP_EncryptFinal_ex(n, reinterpret_cast<unsigned char*>(last.data()), &len))
            return status_invalid_argument("EVP_EncryptFinal_ex() failed");

        last.resize(static_cast<std::size_t>(len));
        out.resize(static_cast<std::size_t>(out_len));
        out += last;

        return std::move(out);
#endif
        return manapi::status_unimplemented("openssl or wolfssl is required");
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "aes:Failed", e.what());
    }
    return manapi::status_internal("aes:Failed");
}

manapi::status_or<std::string> manapi::crypto::aes_decrypt(std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm) {
    try {
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
                return status_invalid_argument("Algorithm invalid");
        }

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        std::unique_ptr<EVP_CIPHER_CTX, evp_cipher_deleter> ctx_cleanup (ctx);

        if (!EVP_DecryptInit_ex(ctx, algorithm_cb, nullptr, nullptr, nullptr))
            return status_invalid_argument("EVP_DecryptInit_ex() failed");

        if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_SET_KEY_LENGTH, static_cast<int>(key.size()), nullptr))
            return status_invalid_argument("EVP_CIPHER_CTX_ctrl() failed");

        if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast <const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(iv.data())))
            return status_invalid_argument("EVP_DecryptInit_ex() failed");

        EVP_CIPHER_CTX_set_padding(ctx, 1);

        std::string out;
        int out_len = 0;
        out.resize(data.size() + static_cast<std::size_t>(EVP_CIPHER_CTX_block_size(ctx)));

        int len;
        if (1 == EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()+out_len), &len, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()))) {
            out_len += len;
        }

        std::string last;
        last.resize(static_cast<std::size_t>(EVP_CIPHER_CTX_block_size(ctx)));
        if (!EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(last.data()), &len))
            return status_invalid_argument("EVP_DecryptFinal_ex() failed");

        last.resize(static_cast<std::size_t>(len));
        out.resize(static_cast<std::size_t>(out_len));

        out += last;

        return std::move(out);
#endif
        return manapi::status_unimplemented("openssl or wolfssl is required");
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "aes:Failed", e.what());
    }
    return manapi::status_internal("aes:Failed");
}


