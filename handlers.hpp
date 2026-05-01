#pragma once

#include "ManapiHttp.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "include/ManapiFetch2.hpp"
#include "std/ManapiRef.hpp"

void init_http_server (std::shared_ptr<manapi::net::http::server> router, std::string const &folder);