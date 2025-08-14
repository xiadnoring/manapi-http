#pragma once

#include "ManapiHttp.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "include/ManapiFetch2.hpp"

void init_http_server (manapi::net::http::server &router, std::string const &folder);