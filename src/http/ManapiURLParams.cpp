#include "ManapiDebug.hpp"
#include "ManapiErrors.hpp"
#include "http/ManapiURLParams.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "encoding/ManapiURL.hpp"
#include "../include/ManapiUtils.hpp"

std::map<std::string, std::string, std::less<>> manapi::net::http::parse_get_params(std::string_view params) {
    std::map<std::string, std::string, std::less<>> result;

    bool flg = false;
    std::string key;

    std::size_t i = 0;

    if (params[i] == '?') {
        /** skip */
        i++;
    }

    std::size_t j = i;

    for (; i < params.size(); ++i) {
        if (params[i] == '#') {
            break;
        }

        if (params[i] == '=') {
            if (flg) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "key has already been defined");
            }
            key = encoding::decode_url (std::string_view{params.data() + j, params.data() + i});
            j = i + 1;
            flg = true;
        }
        else if (params[i] == '&') {
            if (!flg) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT, "key hasn't been defined yet");
            }
            result.insert({std::move(key), encoding::decode_url(std::string_view{params.data() + j, params.data() + i})});
            j = i + 1;
            flg = false;
        }
    }

    if (flg) {
        result.insert({std::move(key), encoding::decode_url(std::string_view{params.data() + j, params.data() + i})});
        j = i + 1;
        flg = false;
    }

    return std::move(result);
}
