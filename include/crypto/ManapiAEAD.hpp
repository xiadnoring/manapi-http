#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "./ManapiCryptoUtils.hpp"
#include "../ManapiUtils.hpp"



namespace manapi::crypto {
    std::string aead_decrypt (std::string_view cipher_password, std::string_view aad, std::string_view key, std::string_view iv, std::string_view tag, ciphers algorithm = ciphers::AES_256_GCM);

    std::string aead_encrypt (std::string_view data, std::string_view aad, std::string_view key, std::string_view iv, std::string &tag, ciphers algorithm = ciphers::AES_256_GCM);
}