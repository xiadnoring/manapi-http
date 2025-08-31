#pragma once

#include <string>
#include <fstream>
#include <iomanip>
#include <set>

#include "../ManapiErrors.hpp"
#include "../ManapiDebug.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::encoding {
    extern const std::set <char> url_allowed_symbols;

    DLLExportImport void encode_url(std::string &dest, std::string_view str);

    DLLExportImport std::string encode_url(std::string_view str);

    DLLExportImport void decode_url(std::string &dest, std::string_view str);

    DLLExportImport std::string decode_url(std::string_view str);

    DLLExportImport bool url_allowed_symbol(const char &c);
}