#pragma once

#include <string>
#include <vector>

#include "./ManapiJson.hpp"
#include "./ManapiErrors.hpp"
#include "./ManapiDebug.hpp"
#include "./ManapiTime.hpp"

#define REQ(_x) manapi::net::http_request &_x
#define RESP(_x) manapi::net::http_response &_x

#define HANDLER(_req, _resp) (REQ(_req), RESP(_resp))