#pragma once

#include <string>

#include "./ManapiCryptoUtils.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::crypto {
    std::string aes_encrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC);

    std::string aes_decrypt (std::string_view data, std::string_view key, std::string_view iv, ciphers algorithm = ciphers::AES_128_CBC);
}