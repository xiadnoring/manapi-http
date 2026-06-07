#include <sstream>

#include "encoding/ManapiUnicode.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiJsonMaskUtils.hpp"

enum json_builder_callbacks {
    JSON_CALLBACK_CHECK_TYPE,
    JSON_CALLBACK_BUILD_STRING,
    JSON_CALLBACK_BUILD_NUMERIC,
    JSON_CALLBACK_BUILD_NUMERIC_STRING,
    JSON_CALLBACK_BUILD_OBJECT,
    JSON_CALLBACK_BUILD_ARRAY,
    JSON_CALLBACK_CHECK_END
};

enum json_builder_flags {
    JSON_FLAG_FIN = 1<<0,
    JSON_FLAG_OPERATE_ALREADY = 1<<2,
    JSON_FLAG_EXP_ALREADY = 1<<3,
    JSON_FLAG_OPENED_QUOTE = 1<<4,
    JSON_FLAG_ESCAPED = 1<<5,
    JSON_FLAG_IS_KEY = 1<<6,
    JSON_FLAG_GO_TO_DELIM = 1<<7,
    JSON_FLAG_RESERVED = 1<<8,
    JSON_FLAG_IS_DECIMAL = 1<<9,
    JSON_FLAG_SKIP_TYPE = 1<<10,
    JSON_FLAG_FAILED = 1<<11
};

#define JSON_BUILDER_DEFAULT_FLAG (JSON_FLAG_FIN)

static manapi::json_error::status json_invalid_char (std::string_view n, std::size_t pos) {
    return manapi::json_error::status_invalid_argument("json: invalid char", pos, {});
}

static manapi::json_error::status json_duplicate_key (std::size_t pos) {
    return manapi::json_error::status_invalid_argument("json: duplicate key", pos, {});
}

static manapi::json_error::status json_unexpected_end (std::size_t pos) {
    return manapi::json_error::status_invalid_argument("json: unexpected end of JSON input at", pos, {});
}

struct manapi::json_builder::data_t {
    uint32_t skip_types{};
    int flags{};
    std::size_t i{};
    int32_t tindx{};
    int state{};
    json object;
    unsigned char utf[6]{};
    std::string buffer;
    std::vector<json_mask_path_t> paths;
    const manapi::json_mask_object_t *types{};
};

static manapi::json_error::status json_builder_check_end(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, data->i++) {
        if (!manapi::unicode::is_space_symbol(plain_text[j]))
            return ::json_invalid_char(plain_text, j);
    }

    return manapi::json_error::status_ok();
}

static bool json_builder_next_type_cond (manapi::json_builder::data_t *data, const manapi::json_mask_object_t * types, uint32_t path_indx) {
    auto &path = data->paths[path_indx];

    path.type ++;

    if (path.type < types->types.size()) {
        return true;
    }

    path.type = 0;

    return false;
}

static manapi::json_error::status json_builder_check_part_object(manapi::json_builder::data_t *data, const manapi::json_mask_object_t * types, uint32_t path_indx, bool complete = false) {
    manapi::json_mask mask_child;
    mask_child.complete_status(complete);
    mask_child.api_tree(types, false);
    return  mask_child.valid(*data->paths[path_indx].p, data->paths.data() + path_indx, static_cast<uint32_t>(data->paths.size()) - path_indx, &data->types);
}

static bool json_builder_next_parent(manapi::json_builder::data_t *data, const manapi::json_mask_object_t * types, uint32_t path_indx) {
    // std::vector<const manapi::json_mask_object_t *> paths;
    uint32_t paths = 0;

    while (true) {
        if (!types->parent)
            return false;

        assert(types->parent->parent);

        // paths.push_back(types);
        paths ++;

        types = types->parent->parent;

        auto const p2indx = path_indx - paths;

        if (!::json_builder_next_type_cond(data, types, p2indx)) {

            continue;
        }

        auto res = ::json_builder_check_part_object(data, types, p2indx);

        if (!res.ok())
            continue;

        break;
    }

    return true;
}

static bool json_builder_next_type(manapi::json_builder::data_t *data, const manapi::json_mask_object_t * types, uint32_t path_indx) {
    if (::json_builder_next_type_cond(data, types, path_indx))
        return true;

    if (::json_builder_next_parent(data, types, path_indx)) {
        return true;
    }

    return false;
}

static bool json_builder_next_type(manapi::json_builder::data_t *data) {
    if (data->flags & JSON_FLAG_IS_KEY || data->flags & JSON_FLAG_SKIP_TYPE)
        return true;

    assert(data->types);

    return ::json_builder_next_type(data, data->types, static_cast<uint32_t>(data->paths.size()) - 1);
}

static manapi::json_error::status json_builder_check_mask (manapi::json_builder::data_t *data) {
    while (true) {
        auto res = ::json_builder_check_part_object(data, data->types, static_cast<uint32_t>(data->paths.size()) - 1, true);
        if (res.ok())
            return std::move(res);
        if (!::json_builder_next_type(data))
            return std::move(res);
    }
}

static bool json_builder_check_type(manapi::json_builder::data_t *data, const manapi::json_mask_object_t * types, uint32_t path_indx) {
    while (true) {
        assert(!types->types.empty());
        assert(data->paths.size() > path_indx);
        auto const obj = types->types[data->paths[path_indx].type].get();
        auto const type = obj->type;

        if (type == -1)
            return true;

        if (data->state == JSON_CALLBACK_BUILD_NUMERIC) {
            if (
                type == manapi::json::type_decimal ||
    #ifdef MANAPIHTTP_BIGINT_SUPPORT
                type == manapi::json::type_bigint ||
    #endif
                type == manapi::json::type_integer ||
                type == manapi::json::type_boolean ||
                type == manapi::json::type_null) {
                return true;
            }
        }
        else if (data->state == JSON_CALLBACK_BUILD_STRING) {
            if (type == manapi::json::type_string)
                return true;
        }
        else if (data->state == JSON_CALLBACK_BUILD_ARRAY) {
            if (type == manapi::json::type_array)
                return true;
        }
        else if (data->state == JSON_CALLBACK_BUILD_OBJECT) {
            if (type == manapi::json::type_object)
                return true;
        }

        if (!::json_builder_next_type(data, types, path_indx))
            return false;
    }

    return true;
}

static bool json_builder_check_type(manapi::json_builder::data_t *data) {
    if (data->flags & JSON_FLAG_IS_KEY || data->flags & JSON_FLAG_SKIP_TYPE)
        return true;

    assert(data->types);

    return ::json_builder_check_type(data, data->types, static_cast<uint32_t>(data->paths.size()) - 1);
}

static bool json_builder_fail_type (manapi::json_builder::data_t *data) {
    while (true) {
        if (!::json_builder_next_type(data))
            return false;

        if (!::json_builder_check_type(data))
            return false;

        break;
    }

    return true;
}

static manapi::json_error::status json_builder_check_type(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {

    if (data->flags & JSON_FLAG_IS_KEY) {
        // by the way
        data->state = JSON_CALLBACK_BUILD_STRING;
    }
    else {
        for (; j < plain_text.size(); data->i++, j++) {
            const char c = plain_text.at(j);

            if (manapi::unicode::is_space_symbol(c))
                continue;

            data->paths.push_back(manapi::json_mask_path_t {});

            switch (c) {
                case '{':
                    data->state = JSON_CALLBACK_BUILD_OBJECT;
                break;
                case '[':
                    data->state = JSON_CALLBACK_BUILD_ARRAY;
                break;
                case '"':
                    data->state = JSON_CALLBACK_BUILD_STRING;
                break;
                default:
                    data->state = JSON_CALLBACK_BUILD_NUMERIC;
                break;
            }

            break;
        }

        if (plain_text.size() != j) {
            auto &b = data->paths.back();

            if (data->paths.size() == 1) {
                data->object = manapi::json (nullptr);
                b.p = &data->object;
                b.indx = 0;
            }
            else {
                auto &p = data->paths[data->paths.size() - 2];
                if (p.p->is_object()) {
                    p.it->second = manapi::json (nullptr);
                    b.p = &p.it->second;
                }
                else if (p.p->is_array()) {
                    p.p->push_back(manapi::json (nullptr));
                    p.indx = p.p->size() - 1;
                    b.p = &p.p->as_array()[p.indx];
                }
                else {
                    assert(false && "unreachable");
                }
            }

            switch (data->state) {
                case JSON_CALLBACK_BUILD_STRING:
                    *b.p = std::string{};
                break;
                default:
                    break;
            }

            if (!::json_builder_check_type(data)) {
                return manapi::json_error::status_invalid_argument("json_mask: type isn't the same", j, manapi::json_format_path2(data->paths));
            }
        }
    }

    return manapi::json_error::status_ok();
}

static bool json_builder_valid_utf_char(std::string_view plain_text, size_t i, uint8_t &left, manapi::json_error::status *st) {
    const char c = plain_text[i];

    if (left > 0 || c > 127) {
        if (left == 0) {
            auto const octet = static_cast<uint8_t>(manapi::unicode::count_of_octet(static_cast<uint8_t>(c)));
            if (octet == 1) {
                // char cant be equal 10xxxxxx
                if (st)
                    *st = json_invalid_char(plain_text, i);
                return false;
            }
            assert(octet > 0);
            left = octet - 1;
        }
        else {
            // if c != 10xxxxxx
            if (manapi::unicode::count_of_octet(static_cast<uint8_t>(c)) != 1) {
                if (st)
                    *st = json_invalid_char(plain_text, i);
                return false;
            }

            left--;
        }
    }

    return true;
}

manapi::json_error::status manapi::json_builder_valid_utf_string(std::string_view str) {
    uint8_t wchar_left = 0;
    auto st = manapi::json_error::status_ok();

    for (size_t i = 0; i < str.size(); i++) {
        if (!json_builder_valid_utf_char(str, i, wchar_left, &st))
            break;
    }

    return st;
}

std::string manapi::json_format_path2(const std::vector<manapi::json_mask_path_t> &p) {
    std::string res;
    if (p.size() >= 2) {
        std::size_t size = 0;
        auto *back = &p.back();
        for (auto &c : p) {
            if (&c != back) {
                if (c.p->is_object())
                    size += c.it->first.size();
                else if (c.p->is_array()) {
                    size += c.indx ? static_cast<std::size_t>(std::log10(c.indx)) + 1 : 1;
                }
            }
        }

        size += p.size();
        res.reserve(size);

        for (const auto &c : p) {
            if (&c != back) {
                res += '/';

                if (c.p->is_object()) {
                    res.append(c.it->first);
                }
                else if (c.p->is_array()) {
                    auto n = c.indx ? static_cast<std::size_t>(std::log10(c.indx)) : 1;
                    res.resize(res.size() + n + 1);
                    auto err = std::snprintf(res.data() + res.size() - n - 1, n + 1, "%zu", c.indx);
                    res.resize(res.size() - 1);
                    if (err < 0)
                        manapi_log_error("bug:snprintf() return < 0 on format path");
                }
            }
        }
    }

    return std::move(res);
}

static bool json_builder_check_max_mean(manapi::json_builder::data_t *data, manapi::json_error::status *st) {

    if (data->flags & JSON_FLAG_SKIP_TYPE) {
        return true;
    }

    {
        assert(data->types);
        auto &path = data->paths.back();
        auto z = data->types->types[path.type].get();
        switch (path.type) {
            case manapi::json::type_integer:
                if (manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->as_integer()))
                    return true;
            break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case manapi::json::type_bigint:
                if (manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->as_bigint()))
                    return true;
            break;
#endif
            case manapi::json::type_decimal:
                if (manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->as_decimal()))
                    return true;
            break;
            case manapi::json::type_string:
            case manapi::json::type_array:
            case manapi::json::type_object:
                if (manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->size()))
                return true;
            break;
            default:
                return true;
        }

        if (st)
            *st = manapi::json_error::status_invalid_argument("json_mask: value is greater or equal max_mean",
            std::format("max_mean({})", path.p->dump()), data->i, manapi::json_format_path2(data->paths));

        return false;
    }
    return true;
}

static bool json_builder_check_min_mean(manapi::json_builder::data_t *data, manapi::json_error::status *st) {

    if (data->flags & JSON_FLAG_SKIP_TYPE)
        return true;

    {
        assert(data->types);
        auto &path = data->paths.back();
        auto z = data->types->types[path.type].get();
        switch (path.type) {
            case manapi::json::type_integer:
                if (manapi::json_verify_min_mean(z->flags, z->max_mean, path.p->as_integer()))
                    goto ok;
            break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case manapi::json::type_bigint:
                if (manapi::json_verify_min_mean(z->flags, z->max_mean, path.p->as_bigint()))
                    goto ok;
            break;
#endif
            case manapi::json::type_decimal:
                if (manapi::json_verify_min_mean(z->flags, z->max_mean, path.p->as_decimal()))
                    goto ok;
            break;
            case manapi::json::type_string:
            case manapi::json::type_array:
            case manapi::json::type_object:
                if (manapi::json_verify_min_mean(z->flags, z->max_mean, path.p->size()))
                    goto ok;
            break;
            default:
                goto ok;
        }

        if (st)
            *st = manapi::json_error::status_invalid_argument("json_mask: value is less or equal min_mean",
            std::format("min_mean({})", path.p->dump()), data->i, manapi::json_format_path2(data->paths));

        return false;
    }
    ok:
    return true;
}

static manapi::json_error::status json_builder_final_object (manapi::json_builder::data_t *data) {
    assert(!data->paths.empty());
    assert(data->buffer.empty());

    if (!(data->flags & JSON_FLAG_SKIP_TYPE)) {
        auto res = ::json_builder_check_mask(data);
        if (!res)
            return std::move(res);
    }

    data->paths.pop_back();
    if (data->paths.empty()) {
        data->state = JSON_CALLBACK_CHECK_END;
    }
    else {
        auto z = data->paths.back().p;

        if (!(data->flags & JSON_FLAG_SKIP_TYPE)) {
            assert(data->types);
            data->types = data->types->parent->parent;
        }

        if (data->flags & JSON_FLAG_SKIP_TYPE) {
            if (data->paths.size() == data->skip_types) {
                data->skip_types = std::numeric_limits<uint32_t>::max();
                data->flags ^= JSON_FLAG_SKIP_TYPE;
            }
        }

        if (z->is_object()) {
            data->state = JSON_CALLBACK_BUILD_OBJECT;
            data->flags |= JSON_FLAG_GO_TO_DELIM;
        }
        else if (z->is_array()) {
            data->state = JSON_CALLBACK_BUILD_ARRAY;
            data->flags |= JSON_FLAG_GO_TO_DELIM;
        }
        else {
            assert(false && "unreachable");
        }
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_build_string(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    auto st = manapi::json_error::status_ok();
    std::string *bf;
    if (data->flags & JSON_FLAG_IS_KEY)
        bf = &data->buffer;
    else
        bf = &data->paths.back().p->as_string();

    for (; j < plain_text.size(); j++, data->i++) {
        char c = plain_text.at(j);

        if (!::json_builder_valid_utf_char(plain_text, j, data->utf[5], &st)) {
            return std::move(st);
        }

        if (data->flags & JSON_FLAG_ESCAPED) {
            data->flags ^= JSON_FLAG_ESCAPED;

            switch (c) {
                case 't':
                    c = '\t';
                break;
                case 'n':
                    c = '\n';
                break;
                case 'r':
                    c = '\r';
                break;
                case 'f':
                    c = '\f';
                break;
                case 'b':
                    c = '\b';
                break;
                case 'u':
                    // \u1234 must be
                    data->utf[4] = 1;
                    continue;
                break;
                case '\\':
                    break;
                case '/':
                    break;
                case '"':
                    break;
                default:
                    return manapi::json_error::status_invalid_argument("json: bad escaped character", data->i,
                        manapi::json_format_path2(data->paths));
            }
        }
        else {
            if (data->utf[4] != 0) {
                // we grab ascii chars for utf char
                c = static_cast<char>(std::tolower(c));
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    // U+0000 - U+007F -> 0yyyzzzz
                    // U+0080 - U+07FF -> 110xxxyy10yyzzzz
                    // U+0800 - U+FFFF -> 1110wwww10xxxxyy10yyzzzz
                    // U+010000 - U+10FFFF -> 11110uvv10vvwwww10xxxxyy10yyzzzz
                    data->utf[data->utf[4]++ - 1] = manapi::unicode::onehex2dec(static_cast<uint8_t>(c));

                    if (data->utf[4] == 5) {

                        if (data->utf[0] == 0 && data->utf[1] <= 7) {
                            if (data->utf[1] == 0 && data->utf[2] <= 7) {
                                // 0yyy zzzz
                                bf->push_back(static_cast<char> (data->utf[2] << 4 | data->utf[3]));
                            }
                            else {
                                // 110x xxyy 10yy zzzz
                                // 1100 0000 <- 192 (128 + 64)
                                // 100000000 <- 128
                                bf->push_back(static_cast<char> (static_cast<unsigned char>(192) | (data->utf[1] << 2) | (data->utf[2] >> 2)));
                                bf->push_back(static_cast<char> (static_cast<unsigned char>(128) | (static_cast<unsigned char> (data->utf[2] << 6) >> 2) | data->utf[3]));
                            }
                        }
                        else {
                            // 1110 wwww 10xx xxyy 10yy zzzz
                            // 1110 0000 <- 224 (128 + 64 + 32)
                            // 1000 0000 <- 128
                            bf->push_back(static_cast<char> (static_cast<unsigned char> (224) | (data->utf[0])));
                            bf->push_back(static_cast<char> (static_cast<unsigned char> (128) | (data->utf[1] << 2) | (data->utf[2] >> 2)));
                            bf->push_back(static_cast<char> (static_cast<unsigned char> (128) | (static_cast<unsigned char> (data->utf[2] << 6) >> 2) | data->utf[3]));
                        }

                        data->utf[4] = 0;
                    }
                    continue;
                }

                // error
                return manapi::json_error::status_invalid_argument ("json: bad unicode escape", data->i, manapi::json_format_path2(data->paths));
            }

            switch (c) {
                case '\t':
                case '\n':
                case '\r':
                case '\f':
                case '\b':
                    return manapi::json_error::status_invalid_argument("json: bad control character",
                        data->i, manapi::json_format_path2(data->paths));
            }

            if (c == '\\') {

                data->flags |= JSON_FLAG_ESCAPED;
                continue;
            }

            if (c == '"') {
                data->flags ^= JSON_FLAG_OPENED_QUOTE;

                if (!(data->flags & JSON_FLAG_OPENED_QUOTE)) {
                    break;
                }

                continue;
            }
        }

        if (data->flags & JSON_FLAG_OPENED_QUOTE) {
            bf->push_back(c);
        }
        else {
            if (!manapi::unicode::is_space_symbol(c)) {
                return json_invalid_char(plain_text, j);
            }
        }
    }


    if (plain_text.size() != j || (data->flags & JSON_FLAG_FIN)) {
        if (data->flags & JSON_FLAG_OPENED_QUOTE || data->flags & JSON_FLAG_ESCAPED)
            return json_unexpected_end(j);

        auto &b = data->paths.back();

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        assert(data->paths.back().p);
        if (data->flags & JSON_FLAG_IS_KEY) {
            assert(data->paths.back().p->is_object());
            auto zres = data->paths.back().p->insert({std::move(data->buffer), manapi::json (nullptr)});
            if (!zres.second)
                return json_duplicate_key(data->i);
            b.it = zres.first;
            data->flags |= JSON_FLAG_GO_TO_DELIM;
            data->state = JSON_CALLBACK_BUILD_OBJECT;
        }
        else {
            st = ::json_builder_final_object(data);
            if (!st)
                return std::move(st);
        }

        data->buffer.clear();
    }
    else {
        if (!(data->flags & JSON_FLAG_IS_KEY) && !(data->flags & JSON_FLAG_SKIP_TYPE)) {
            auto &path = data->paths.back();
            auto z = data->types->types[path.type].get();

            if (!z->max_mean.is_null() && !manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->size())) {
                if (!::json_builder_fail_type(data)) {
                    return manapi::json_error::status_invalid_argument("json_mask: value is greater or equal max_mean",
                            std::format("max_mean({})", z->max_mean.dump()), data->i, manapi::json_format_path2(data->paths));
                }
            }
        }
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_build_numeric(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, data->i++) {
        const char c = plain_text[j];

        if (data->buffer.empty()) {
            data->flags |= JSON_FLAG_OPERATE_ALREADY;

            if (c == '-' || c == '+') {
                data->buffer += c;
                continue;
            }
        }

        if (c >= '0' && c <= '9') {
            if (data->buffer.size() == 1 && data->buffer[0] == '0') {
                // it can't be
                return json_invalid_char(plain_text, j);
            }
            data->buffer += c;
        }
        else {
            if (data->buffer.empty()) {
                // its true or false or null
                data->state = JSON_CALLBACK_BUILD_NUMERIC_STRING;

                return manapi::json_error::status_ok();
            }

            if (data->buffer.size() == 1 && data->buffer[0] == '-') {
                // we need at least 1 digit
                return json_invalid_char(plain_text, j);
            }

            if (manapi::unicode::is_space_symbol(c) || c == '}' || c == ']' || c == ',') {
                break;
            }

            switch (c) {
                case '-':
                case '+':
                    if (data->flags & JSON_FLAG_OPERATE_ALREADY) {
                        return json_invalid_char(plain_text, j);
                    }
                    data->flags |= JSON_FLAG_OPERATE_ALREADY;
                    data->buffer += (c);
                break;
                case 'e':
                case 'E':
                    // exp
                    if (data->flags & JSON_FLAG_EXP_ALREADY)
                        return json_invalid_char(plain_text, j);

                    data->flags |= JSON_FLAG_EXP_ALREADY|JSON_FLAG_IS_DECIMAL;
                    data->buffer += (c);
                    if (data->flags & JSON_FLAG_OPERATE_ALREADY)
                        data->flags ^= JSON_FLAG_OPERATE_ALREADY;
                break;
                case '.':
                    if (data->flags & JSON_FLAG_IS_DECIMAL)
                        return json_invalid_char(plain_text, j);

                    // its decimal
                    data->flags |= JSON_FLAG_IS_DECIMAL;
                    data->buffer += (c);
                    break;
                default:
                    return json_invalid_char(plain_text, j);
            }
        }
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {

        if (data->flags & JSON_FLAG_EXP_ALREADY)
            data->flags ^= JSON_FLAG_EXP_ALREADY;

        if (data->flags & JSON_FLAG_OPERATE_ALREADY)
            data->flags ^= JSON_FLAG_OPERATE_ALREADY;

        auto &b = data->paths.back();

        if (data->flags & JSON_FLAG_IS_DECIMAL) {
            std::stringstream stream (std::move(data->buffer));
            manapi::json::DECIMAL d;
            stream >> d;
            *b.p = manapi::json (d);
            data->flags ^= JSON_FLAG_IS_DECIMAL;
        }
        else {
            const bool have_sign = !data->buffer.empty() && data->buffer[0] == '-';
            std::stringstream stream (std::move(data->buffer));

            if (have_sign) {
                manapi::json::INTEGER n;
                stream >> n;
                *b.p = manapi::json(static_cast<manapi::json::INTEGER> (n));
            }
            else {
                std::size_t un;
                stream >> un;
                *b.p = manapi::json(static_cast<manapi::json::INTEGER> (un));
            }
        }

        data->buffer.clear();
        auto st = ::json_builder_final_object(data);
        if (!st)
            return std::move(st);
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_build_numeric_string(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); data->i++, j++) {
        const char c = plain_text[j];

        if (manapi::unicode::is_space_symbol(c) || c == '}' || c == ',' || c == ']') {
            break;
        }

        if (data->buffer.size() == sizeof("false")) {
            return ::json_invalid_char(plain_text, j);
        }

        data->buffer += (c);
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        // check if this is true, false, null ->

        auto &b = data->paths.back();
        // true
        if (data->buffer == "true") {
            // this->type = json::type_boolean;
            *b.p = manapi::json (true);
        }
        // false
        else if (data->buffer == "false") {
            // this->type = json::type_boolean;
            *b.p = manapi::json (false);
        }
        // null
        else if (data->buffer == "null") {
            // this->type = json::type_null;
            *b.p = manapi::json(nullptr);
        }
        else {
            return manapi::json_error::status_invalid_argument("json: invalid string", j, manapi::json_format_path2(data->paths));
        }

        data->buffer.clear();
        auto st = ::json_builder_final_object(data);
        if (!st)
            return std::move(st);
    }
    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_build_object(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    auto z = data->paths.back().p;

    for (; j < plain_text.size(); j++, data->i++) {
        const char c = plain_text[j];

        if (manapi::unicode::is_space_symbol(c)) {
            continue;
        }

        if (z->is_null()) {
            if (c == '{') {
                *z = manapi::json::object();

                continue;
            }

            return json_invalid_char(plain_text, j);
        }

        if (c == '}') {
            if ((data->flags & JSON_FLAG_GO_TO_DELIM) && !(data->flags & JSON_FLAG_IS_KEY))
                data->flags ^= JSON_FLAG_GO_TO_DELIM;

            break;
        }

        if (data->flags & JSON_FLAG_GO_TO_DELIM) {
            data->flags ^= JSON_FLAG_GO_TO_DELIM;

            if (c == ':') {
                if (data->flags & JSON_FLAG_IS_KEY) {
                    data->flags ^= JSON_FLAG_IS_KEY;
                    // is value

                    if (!(data->flags & JSON_FLAG_SKIP_TYPE)) {
                        while (true) {
                            auto &b = data->paths.back();
                            auto &type = data->types->types[b.type];

                            if (type->mean.is_object()) {
                                auto mit = type->mean.find(b.it->first);
                                if (mit == type->mean.end<manapi::json::OBJECT>()) {
                                    if (!::json_builder_fail_type(data)) {
                                        return manapi::json_error::status_invalid_argument("json_mask: key is invalid",
                                            std::format("key({})", b.it->first), data->i, manapi::json_format_path2(data->paths));
                                    }

                                    continue;
                                }
                                auto d = dynamic_cast<manapi::json_mask_object_t *>(&mit->second.as_source());
                                assert(d);
                                data->types = d;
                            }
                            else {
                                data->skip_types = static_cast<uint32_t>(data->paths.size());
                                data->flags |= JSON_FLAG_SKIP_TYPE;
                            }

                            break;
                        }
                    }

                    data->state = JSON_CALLBACK_CHECK_TYPE;

                    j++;
                    data->i++;

                    return manapi::json_error::status_ok();
                }
            }
            else if (c == ',') {
                if (!(data->flags & JSON_FLAG_IS_KEY))
                    continue;
            }
        }
        else if (!(data->flags & JSON_FLAG_IS_KEY)) {
            // is key
            data->flags |= JSON_FLAG_IS_KEY;
            data->state = JSON_CALLBACK_CHECK_TYPE;
            return manapi::json_error::status_ok();
        }

        return json_invalid_char(plain_text, j);
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        // true when {..., ...',' <- we are expecting a key}, false otherwise
        if (data->flags & JSON_FLAG_IS_KEY || data->flags & JSON_FLAG_GO_TO_DELIM)
            return json_unexpected_end(data->i);

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        auto st = ::json_builder_final_object(data);
        if (!st)
            return std::move(st);
    }
    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_build_array(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    auto z = data->paths.back().p;

    for (; j < plain_text.size(); j++, data->i++) {
        const char c = plain_text[j];

        if (manapi::unicode::is_space_symbol(c))
            continue;

        if (z->is_null()) {
            if (c == '[') {
                *z = manapi::json::array();

                continue;
            }

            return json_invalid_char(plain_text, j);
        }

        if (c == ']') {
            if (data->flags & JSON_FLAG_GO_TO_DELIM)
                data->flags ^= JSON_FLAG_GO_TO_DELIM;

            break;
        }

        if (data->flags & JSON_FLAG_GO_TO_DELIM) {
            if (c == ',') {
                data->flags ^= JSON_FLAG_GO_TO_DELIM;
                continue;
            }

            return json_invalid_char(plain_text, j);
        }

        data->state = JSON_CALLBACK_CHECK_TYPE;

        if (!(data->flags & JSON_FLAG_SKIP_TYPE)) {
            auto &b = data->paths.back();
            auto &type = data->types->types[b.type];

            if (type->zdefault) {
                data->types = type->zdefault.get();
            }
            else {
                data->flags |= JSON_FLAG_SKIP_TYPE;
                data->skip_types =static_cast<uint32_t>( data->paths.size());
            }
        }

        return manapi::json_error::status_ok();
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        if (data->flags & JSON_FLAG_GO_TO_DELIM)
            return json_unexpected_end(data->i);

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        auto st = ::json_builder_final_object(data);
        if (!st)
            return std::move(st);
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_builder_call_state(manapi::json_builder::data_t *data, const std::string_view &plain_text, size_t &j) {
    switch (data->state) {
        case JSON_CALLBACK_CHECK_TYPE:
            return ::json_builder_check_type(data, (plain_text), j);
        case JSON_CALLBACK_BUILD_STRING:
            return ::json_builder_build_string(data, (plain_text), j);
        case JSON_CALLBACK_BUILD_NUMERIC:
            return ::json_builder_build_numeric(data, (plain_text), j);
        case JSON_CALLBACK_BUILD_NUMERIC_STRING:
            return ::json_builder_build_numeric_string(data, (plain_text), j);
        case JSON_CALLBACK_BUILD_OBJECT:
            return ::json_builder_build_object(data, (plain_text), j);
        case JSON_CALLBACK_BUILD_ARRAY:
            return ::json_builder_build_array(data, (plain_text), j);
        case JSON_CALLBACK_CHECK_END:
            return ::json_builder_check_end(data, (plain_text), j);
        default:
            return manapi::json_error::status_invalid_argument("unreachable",
                data->i, manapi::json_format_path2(data->paths));
    }
}

static manapi::json_error::status json_builder_parse(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    if (data->flags & JSON_FLAG_FAILED)
        return manapi::json_error::status {manapi::status_invalid_argument("json:build was failed")};

    while (j < plain_text.size()) {
        // ????: yo, 2026: what?
        auto res = ::json_builder_call_state (data, plain_text, j);
        if (!res.ok()) {
            data->flags |= JSON_FLAG_FAILED;
            return std::move(res);
        }
    }

    return manapi::json_error::status_ok();
}

static void json_builder_check_flags (manapi::json_builder::data_t *data) {
    if (data->types) {
        if (data->flags & JSON_FLAG_SKIP_TYPE)
            data->flags ^= JSON_FLAG_SKIP_TYPE;
    }
    else {
        data->flags |= JSON_FLAG_SKIP_TYPE;
    }
}

static void json_builder_reset(manapi::json_builder::data_t *data) {
    // object can not be clean up here
    data->i = 0;
    data->utf[5] = 0;
    data->utf[4] = 0;
    data->flags = 0;
    data->buffer.clear();
    data->state = JSON_CALLBACK_CHECK_TYPE;
    data->types = nullptr;
    data->tindx = 0;
    data->paths.clear();
    data->object = nullptr;
    data->skip_types = std::numeric_limits<uint32_t>::max();

    ::json_builder_check_flags(data);
}

manapi::json_builder::json_builder() {
    this->m_data = std::make_unique<json_builder::data_t>();
    this->m_data->state = JSON_CALLBACK_CHECK_TYPE;
    this->m_data->skip_types = std::numeric_limits<uint32_t>::max();
    json_builder_check_flags(this->m_data.get());
}

manapi::json_builder::json_builder(json_mask &mask) : json_builder() {
    this->m_data->types = mask.api_tree();
    json_builder_check_flags(this->m_data.get());
}

manapi::json_builder::~json_builder() = default;

manapi::json_builder & manapi::json_builder::operator<<(std::string_view str) {
    this->parse(str).unwrap();
    return *this;
}

manapi::json_builder & manapi::json_builder::operator<<(char c) {
    this->parse(c).unwrap();
    return *this;
}

manapi::json_error::status manapi::json_builder::parse(std::string_view str) {
    size_t j = 0;
    return ::json_builder_parse(this->m_data.get(), str, j);
}

manapi::json_error::status manapi::json_builder::parse(char c) {
    return this->parse(std::string_view(&c, 1));
}

manapi::json_error::status_or<manapi::json> manapi::json_builder::get() {
    if (this->m_data->flags & JSON_FLAG_FAILED)
        return manapi::json_error::status {manapi::status_invalid_argument("json:build was failed")};

    manapi::json result;
    this->m_data->flags |= JSON_FLAG_FIN;
    try {
        if (this->is_ready()) {
            result = std::move(this->m_data->object);
            this->clear();
        }
        else {
            size_t j = 0;
            manapi::json_error::status res = ::json_builder_call_state(this->m_data.get(), {}, j);

            if (!res.ok()) {
                this->clear();
                return std::move(res);
            }

            if (!this->is_ready()) {
                res = ::json_unexpected_end(this->m_data->i);

                this->clear();
                return std::move(res);
            }

            result = std::move(this->m_data->object);
            this->clear();
        }

        return std::move(result);
    }
    catch (...) {
        if (this->m_data->flags & JSON_FLAG_FIN)
            this->m_data->flags ^= JSON_FLAG_FIN;

        this->clear();

        std::rethrow_exception(std::current_exception());
    }
}

bool manapi::json_builder::is_ready() const {
    return this->m_data->state == JSON_CALLBACK_CHECK_END;
}

bool manapi::json_builder::is_empty() const {
    return this->m_data->i == 0;
}

void manapi::json_builder::set(json_mask &mask) {
    this->clear();
    this->m_data->types = mask.api_tree();
    ::json_builder_check_flags(this->m_data.get());
}

void manapi::json_builder::clear() {
    ::json_builder_reset(this->m_data.get());
}
