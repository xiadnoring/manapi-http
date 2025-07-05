#pragma once

#include <array>
#include <stdexcept>
#include <string>

#include "../ManapiUtils.hpp"
#include "./ManapiCryptoUtils.hpp"
#include "../ManapiHttpTypes.hpp"


namespace manapi::crypto {
    std::string hmac_digest (std::string_view key, std::string_view data, hashes hash_algorithm = hashes::SHA_256);

    std::string hkdf_extract (std::string_view salt, std::string_view ikm, hashes hash = hashes::SHA_256);

    std::string hkdf_expand (std::string_view prk, std::string_view info, int length, hashes algorithm = hashes::SHA_256);

    std::string hkdf (std::string_view salt, std::string_view ikm, std::string_view info, int length, hashes hash = hashes::SHA_256);
}