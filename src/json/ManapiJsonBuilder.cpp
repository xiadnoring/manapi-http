#include <sstream>
#include <array>

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
    JSON_FLAG_EXPECT_LOW_SURROGATE = 1<<3,
    JSON_FLAG_OPENED_QUOTE = 1<<4,
    JSON_FLAG_ESCAPED = 1<<5,
    JSON_FLAG_IS_KEY = 1<<6,
    JSON_FLAG_GO_TO_DELIM = 1<<7,
    JSON_FLAG_RESERVED = 1<<8,
    JSON_FLAG_IS_DECIMAL = 1<<9,
    JSON_FLAG_SKIP_TYPE = 1<<10,
    JSON_FLAG_FAILED = 1<<11,
    JSON_FLAG_MAX_SHIFT = 20
};

#define JSON_BUILDER_DEFAULT_FLAG (JSON_FLAG_FIN)

static constexpr auto js_is_space = [] {
    std::array<bool, 256> tbl{};
    tbl[' ']  = true;
    tbl['\t'] = true;
    tbl['\n'] = true;
    tbl['\r'] = true;
    return tbl;
}();

static constexpr auto js_is_space2 = [] {
    std::array<bool, 256> tbl{};
    tbl[' ']  = true;
    tbl['\t'] = true;
    tbl['\n'] = true;
    tbl['\r'] = true;
    tbl['}'] = true;
    tbl[']'] = true;
    tbl[','] = true;
    return tbl;
}();

static constexpr auto utf8_octet_type = [] {
    std::array<uint8_t, 256> tbl{};
    for (uint8_t i = 0; i < (uint8_t)256; i++) {
        if ((i & 0x80) == 0)        tbl[i] = 0;   // ASCII
        else if ((i & 0xE0) == 0xC0) tbl[i] = 2;  // 110xxxxx
        else if ((i & 0xF0) == 0xE0) tbl[i] = 3;  // 1110xxxx
        else if ((i & 0xF8) == 0xF0) tbl[i] = 4;  // 11110xxx
        else if ((i & 0xC0) == 0x80) tbl[i] = 1;  // 10xxxxxx - continuation byte
        else                          tbl[i] = 0xFF; // invalid
    }
    return tbl;
}();

static constexpr auto escape_table2 = [] {
    std::array<char, 256> tbl{};
    tbl['t'] = '\t';
    tbl['n'] = '\n';
    tbl['r'] = '\r';
    tbl['f'] = '\f';
    tbl['b'] = '\b';
    tbl['\\'] = '\\';
    tbl['/'] = '/';
    tbl['"'] = '"';
    tbl['u'] = 'u';
    return tbl;
}();

static constexpr auto is_valid_escape = [] {
    std::array<bool, 256> tbl{};
    tbl['t'] = tbl['n'] = tbl['r'] = tbl['f'] = tbl['b'] = true;
    tbl['\\'] = tbl['/'] = tbl['"'] = tbl['u'] = true;
    return tbl;
}();

static constexpr auto hex_to_val = [] {
    std::array<uint8_t, 256> tbl{};
    for (uint8_t i = 0; i < (uint8_t)256; i++) tbl[i] = 0xFF; // invalid
    for (uint8_t i = '0'; i <= '9'; i++) tbl[i] = i - '0';
    for (uint8_t i = 'a'; i <= 'f'; i++) tbl[i] = i - 'a' + 10;
    for (uint8_t i = 'A'; i <= 'F'; i++) tbl[i] = i - 'A' + 10;
    return tbl;
}();

struct utf8_from_unicode_entry {
    uint8_t bytes[3];
    uint8_t len;
};
static constexpr auto utf8_from_unicode_table = [] {
    // U+0000 - U+007F -> 1 байт: 0xxxxxxx
    // U+0080 - U+07FF -> 2 байта: 110xxxxx 10xxxxxx
    // U+0800 - U+FFFF -> 3 байта: 1110xxxx 10xxxxxx 10xxxxxx
    std::array<utf8_from_unicode_entry, 0x10000> tbl{}; // 64K — только BMP
    for (uint32_t cp = 0; cp < (uint32_t)0x10000; cp++) {
        if (cp <= 0x7F) {
            tbl[cp].bytes[0] = static_cast<uint8_t>(cp);
            tbl[cp].len = 1;
        } else if (cp <= 0x7FF) {
            tbl[cp].bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
            tbl[cp].bytes[1] = (uint8_t)(0x80 | (cp & 0x3F));
            tbl[cp].len = 2;
        } else {
            tbl[cp].bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
            tbl[cp].bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            tbl[cp].bytes[2] = (uint8_t)(0x80 | (cp & 0x3F));
            tbl[cp].len = 3;
        }
    }
    return tbl;
}();


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
    uint32_t flags{};
    std::size_t i{};
    int32_t tindx{};
    uint32_t surrogate_high{};
    int state{};
    json object;
    unsigned char utf[6]{};
    std::string buffer;
    std::vector<json_mask_path_t> paths;
    const manapi::json_mask_object_t *types{};
    manapi::json_error::status st;
};

static int json_builder_check_end(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    std::size_t start = j;
    while (j < plain_text.size() && js_is_space[static_cast<unsigned char>(plain_text[j])]) {
        j++;
    }

    data->i += j - start;

    if (j != plain_text.size()) {
        data->st = ::json_invalid_char(plain_text, data->i);
        return 1;
    }

    return 0;
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

static int json_builder_check_type(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {

    if (data->flags & JSON_FLAG_IS_KEY) {
        // by the way
        data->state = JSON_CALLBACK_BUILD_STRING;
    }
    else {
        for (; j < plain_text.size(); data->i++, j++) {
            const char c = plain_text.at(j);

            if (js_is_space[static_cast<unsigned char>(c)])
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
                data->st = manapi::json_error::status_invalid_argument("json_mask: type isn't the same", j, manapi::json_format_path2(data->paths));
                return 1;
            }
        }
    }

    return 0;
}

static int json_builder_valid_utf_char(manapi::json_builder::data_t *data, const std::string_view& plain_text, size_t i, uint8_t &left) {
    const auto c = static_cast<uint8_t>(plain_text[i]);
    const uint8_t type = utf8_octet_type[c];

    if (type == 0){
        if (left > 0) {
            if (data) data->st = json_invalid_char(plain_text, data->i);
            return 1;
        }
        return 0;
    }

    if (type == 1) {
        if (left > 0) {
            left--;
            return 0;
        }
        if (data) data->st = json_invalid_char(plain_text, data->i);
        return 1;
    }

    if (type >= 2 && type <= 4)  {
        if (left > 0) {
            if (data) data->st = json_invalid_char(plain_text, data->i);
            return 1;
        }

        if (type == 2 && (c & 0xFE) == 0xC0) {
            if (data) data->st = json_invalid_char(plain_text, data->i);
            return 1;
        }
        if (type == 3 && c == 0xE0 &&
            static_cast<uint8_t>(plain_text[i + 1]) < 0xA0) {
                if (data) data->st = json_invalid_char(plain_text, data->i);
                return 1;
            }
        if (type == 4 && c == 0xF0 &&
            static_cast<uint8_t>(plain_text[i + 1]) < 0x90) {
                if (data) data->st = json_invalid_char(plain_text, data->i);
                return 1;
            }

        left = type - 1;
        return 0;
    }

    if (data) data->st = json_invalid_char(plain_text, data->i);
    return 1;
}

manapi::json_error::status manapi::json_builder_valid_utf_string(std::string_view str) {
    uint8_t wchar_left = 0;
    for (size_t i = 0; i < str.size(); i++) {
        if (json_builder_valid_utf_char(nullptr, str, i, wchar_left))
            return json_invalid_char(str, i);
    }

    return manapi::status_ok();
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

manapi::json_dump_buf_str::json_dump_buf_str(std::string *str) : m_str(str) {}

void manapi::json_dump_buf_str::set(std::string *str) {
    this->m_str = str;
}

void manapi::json_dump_buf_str::push_back(const char *buffer, std::size_t sz) {
    this->m_str->append(buffer, sz);
}

void manapi::json_dump_buf_str::push_back(char c) {
    this->m_str->push_back(c);
}

manapi::json_dump_buf_prealloc_sv::json_dump_buf_prealloc_sv(manapi::slice *sv){
    if (sv) this->m_sv = *sv;
}

void manapi::json_dump_buf_prealloc_sv::set(manapi::slice *sv) {
    if (sv) this->m_sv = *sv;
}

void manapi::json_dump_buf_prealloc_sv::push_back(const char *buffer, std::size_t sz) {
    this->m_sv.copy_from(buffer, 0, sz).unwrap();
    this->m_sv = this->m_sv.subslice(sz).unwrap();
}

void manapi::json_dump_buf_prealloc_sv::push_back(char c) {
    this->push_back(&c, 1);
}

manapi::json_dump_buf_sv::json_dump_buf_sv(manapi::slice *sv) : m_sv(sv) {
}

void manapi::json_dump_buf_sv::set(manapi::slice *sv) {
    this->m_sv = sv;
}

void manapi::json_dump_buf_sv::push_back(const char *buffer, std::size_t sz) {
    this->m_sv->push_back(buffer, sz);
}

void manapi::json_dump_buf_sv::push_back(char c) {
    this->push_back(&c, 1);
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

static int json_builder_final_object (manapi::json_builder::data_t *data) {
    assert(!data->paths.empty());
    assert(data->buffer.empty());

    if (!(data->flags & JSON_FLAG_SKIP_TYPE)) {
        auto res = ::json_builder_check_mask(data);
        if (!res) {
            data->st = std::move(res);
            return 1;
        }
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

    return 0;
}

static int append_utf8_codepoint(manapi::json_dump_buffer *bf, uint32_t cp) {
    if (cp <= 0x7F) {
        bf->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        char buf[2] = {
            static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)),
            static_cast<char>(0x80 | (cp & 0x3F))
        };
        bf->push_back(buf, 2);
    } else if (cp <= 0xFFFF) {
        char buf[3] = {
            static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)),
            static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
            static_cast<char>(0x80 | (cp & 0x3F))
        };
        bf->push_back(buf, 3);
    } else if (cp <= 0x10FFFF) {
        char buf[4] = {
            static_cast<char>(0xF0 | ((cp >> 18) & 0x07)),
            static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
            static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
            static_cast<char>(0x80 | (cp & 0x3F))
        };
        bf->push_back(buf, 4);
    } else {
        return 1;
    }

    return 0;
}

static int json_builder_build_string(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    static const bool *js_special = [] {
        static bool js_tbl[256]{};
        for (int i = 0; i <= 0x1F; ++i) js_tbl[i] = true; // control chars
        js_tbl['"'] = true;
        js_tbl['\\'] = true;
        for (int i = 0x80; i <= 0xFF; ++i) js_tbl[i] = true; // non-ASCII
        return js_tbl;
    }();

    manapi::json_dump_buffer *bf;
    manapi::json_dump_buf_str zstr(nullptr);
    manapi::json_dump_buf_sv zsv(nullptr);
    std::size_t sz;
    auto &p = data->paths.back().p;

    auto& ctx = manapi::async::internal::current_();
    if (ctx) sz = manapi::async::memory_fabric()->area_size();
    else sz = std::numeric_limits<std::size_t>::max();

    auto ptz = plain_text.size();

    if (data->flags & JSON_FLAG_IS_KEY) {
        data->buffer.reserve(256);
        zstr.set(&data->buffer);
        bf = &zstr;
    }
    else {
        if (p->is_string()) {
            zstr.set(&p->as_string());
            bf = &zstr;
        }
        else {
            zsv.set(&p->as_slice());
            bf = &zsv;
        }
    }

    while (j < ptz) {
        if (data->utf[4] == 0 && !(data->flags & JSON_FLAG_ESCAPED))  {
            size_t next = j;
            while (next < plain_text.size() && !js_special[static_cast<unsigned char>(plain_text[next])]) {
                ++next;
            }
            if (next > j) {
                if (data->flags & (manapi::JSON_FLAG_SLICES << JSON_FLAG_MAX_SHIFT) &&
                    p->is_string() && p->size() + next - j >= sz) {
                    zsv.set(&p->cast_slice().as_slice());
                    bf = &zsv;
                }
                bf->push_back(plain_text.data() + j, next - j);
                data->i += (next - j);
                j = next;
                continue;
            }
        }

         auto c = static_cast<uint8_t>(plain_text[j]);

        {
            const uint8_t type = utf8_octet_type[c];

            if (type == 0)  {                     // ASCII
                if (data->utf[5] > 0) {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
            } else if (type == 1)  {              // continuation byte
                if (data->utf[5] > 0)  {
                    data->utf[5]--;
                } else {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
            } else if (type >= 2 && type <= 4)  {
                if (data->utf[5] > 0) {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
                // overlong проверки
                if (type == 2 && (c & 0xFE) == 0xC0) {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
                if (type == 3 && c == 0xE0 &&
                    static_cast<uint8_t>(plain_text[j+1]) < 0xA0) {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
                if (type == 4 && c == 0xF0 &&
                    static_cast<uint8_t>(plain_text[j+1]) < 0x90) {
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
                }
                data->utf[5] = type - 1;   // continuation
            } else {
                data->st = json_invalid_char(plain_text, data->i);
                return 1;
            }
        }

        j++;
        data->i++;

        if (data->flags & JSON_FLAG_ESCAPED) {
            data->flags ^= JSON_FLAG_ESCAPED;

            if (is_valid_escape[c])  {
                if (c == 'u') {
                    data->utf[4] = 1;   // \uXXXX
                    continue;
                }
                c = (uint8_t)escape_table2[c];
            } else {
                data->st = manapi::json_error::status_invalid_argument(
                    "json: bad escaped character", data->i - 1,
                    manapi::json_format_path2(data->paths));
                return 1;
            }
        }
        else if (data->utf[4] != 0) {
            uint8_t val = hex_to_val[c];
            if (val != 0xFF)  {
                data->utf[data->utf[4]++ - 1] = val;

                if (data->utf[4] == 5) {
                    uint32_t codepoint = (static_cast<uint32_t>(data->utf[0]) << 12) |
                         (static_cast<uint32_t>(data->utf[1]) << 8)  |
                         (static_cast<uint32_t>(data->utf[2]) << 4)  |
                         static_cast<uint32_t>(data->utf[3]);

                    if (data->flags & JSON_FLAG_EXPECT_LOW_SURROGATE) {
                        if (codepoint < 0xDC00 || codepoint > 0xDFFF) {
                            data->st = json_invalid_char(plain_text, data->i - 1);
                            return 1;
                        }
                        uint32_t real = 0x10000 + ((data->surrogate_high - 0xD800) << 10) + (codepoint - 0xDC00);
                        if (append_utf8_codepoint(bf, real)) {
                            data->st = json_invalid_char(plain_text, data->i);
                            return 1;
                        }
                        data->flags ^= JSON_FLAG_EXPECT_LOW_SURROGATE;
                        data->surrogate_high = 0;
                    } else if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        data->surrogate_high = codepoint;
                        data->flags |= JSON_FLAG_EXPECT_LOW_SURROGATE;
                    } else {
                        if (append_utf8_codepoint(bf, codepoint)) {
                            data->st = json_invalid_char(plain_text, data->i);
                            return 1;
                        }
                    }
                    data->utf[4] = 0;
                }
                continue;
            } else {
                data->st = manapi::json_error::status_invalid_argument(
                    "json: bad unicode escape", data->i - 1,
                    manapi::json_format_path2(data->paths));
                return 1;
            }
        }
        else {
            if (c == '\\') {
                data->flags |= JSON_FLAG_ESCAPED;
                continue;
            }

            if (data->flags & JSON_FLAG_EXPECT_LOW_SURROGATE) {
                data->st = json_invalid_char(plain_text, data->i);
                return 1;
            }

            if (c <= 0x1F) {
                if (c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\b') {
                    data->st = manapi::json_error::status_invalid_argument(
                        "json: bad control character", data->i - 1,
                        manapi::json_format_path2(data->paths));
                    return 1;
                }
            }

            if (c == '"') {
                data->flags ^= JSON_FLAG_OPENED_QUOTE;
                if (!(data->flags & JSON_FLAG_OPENED_QUOTE)) {
                    j--;
                    break;
                }
                continue;
            }
        }

        if (data->flags & JSON_FLAG_OPENED_QUOTE)  {
            bf->push_back(static_cast<char>(c));
            if (data->flags & (manapi::JSON_FLAG_SLICES << JSON_FLAG_MAX_SHIFT) &&
                p->is_string() && p->size() >= sz) {
                zsv.set(&p->cast_slice().as_slice());
                bf = &zsv;
            }
        } else {
            if (!js_is_space[c]) {
                data->st = json_invalid_char(plain_text, data->i - 1);
                return 1;
            }
        }
    }


    if (plain_text.size() != j || (data->flags & JSON_FLAG_FIN)) {
        if (data->flags & JSON_FLAG_OPENED_QUOTE || data->flags & JSON_FLAG_ESCAPED) {
            data->st =  json_unexpected_end(j);
            return 1;
        }

        auto &b = data->paths.back();

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        assert(data->paths.back().p);
        if (data->flags & JSON_FLAG_IS_KEY) {
            assert(data->paths.back().p->is_object());
            auto zres = data->paths.back().p->insert({std::move(data->buffer), manapi::json (nullptr)});
            if (!zres.second) {
                data->st = json_duplicate_key(data->i);
                return 1;
            }
            b.it = zres.first;
            data->flags |= JSON_FLAG_GO_TO_DELIM;
            data->state = JSON_CALLBACK_BUILD_OBJECT;
        }
        else {
            if (::json_builder_final_object(data))
                return 1;
        }

        data->buffer.clear();
    }
    else {
        if (!(data->flags & JSON_FLAG_IS_KEY) && !(data->flags & JSON_FLAG_SKIP_TYPE)) {
            auto &path = data->paths.back();
            auto z = data->types->types[path.type].get();

            if (!z->max_mean.is_null() && !manapi::json_verify_max_mean(z->flags, z->max_mean, path.p->size())) {
                if (!::json_builder_fail_type(data)) {
                    data->st = manapi::json_error::status_invalid_argument("json_mask: value is greater or equal max_mean",
                            std::format("max_mean({})", z->max_mean.dump()), data->i, manapi::json_format_path2(data->paths));
                    return 1;
                }
            }
        }
    }

    return 0;
}

static int json_builder_build_numeric(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    static constexpr std::array<bool, 256> is_number_char = []{
        std::array<bool, 256> t{};
        for (uint8_t i = '0'; i <= '9'; ++i) t[i] = true;
        t['-'] = t['+'] = t['.'] = t['e'] = t['E'] = true;
        return t;
    }();

    data->buffer.reserve(32);
    std::size_t start;

    if (data->buffer.empty()) {
        start = j;
        while (j < plain_text.size() && js_is_space[static_cast<uint8_t> (plain_text[j])]) {
            j++;
        }

        data->i += j - start;
    }

    start = j;
    while (j < plain_text.size() && is_number_char[static_cast<uint8_t> (plain_text[j])]) {
        j++;
    }

    data->buffer.append(plain_text.data() + start, j - start);
    data->i += j - start;

    if (j != plain_text.size() && !js_is_space2[static_cast<uint8_t> (plain_text[j])]) {
        data->state = JSON_CALLBACK_BUILD_NUMERIC_STRING;
        return 0;
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        auto &b = data->paths.back();

        if (data->buffer.find_first_of(".eE") != std::string::npos) {
            long double d;
            auto [ptr, ec] = std::from_chars(data->buffer.data(), data->buffer.data() + data->buffer.size(), d);
            *b.p = manapi::json(d);
            if (ec != std::errc{}) {
                data->st= json_invalid_char(plain_text, data->i);
                return 1;
            }
        }
        else {
            int64_t un;
            auto [ptr, ec] = std::from_chars(data->buffer.data(), data->buffer.data() + data->buffer.size(), un);
            if (ec != std::errc{}) {
                long double d;
                auto [ptr, ec] = std::from_chars(data->buffer.data(), data->buffer.data() + data->buffer.size(), d);
                *b.p = manapi::json(d);
                if (ec != std::errc{}) {
                    data->st= json_invalid_char(plain_text, data->i);
                    return 1;
                }
            }
            else {
                *b.p = manapi::json(un);
            }
        }

        data->buffer.clear();
        if (::json_builder_final_object(data))
            return 1;
    }

    return 0;
}

static int json_builder_build_numeric_string(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    data->buffer.reserve(5);

    std::size_t start;

    if (data->buffer.empty()) {
        start = j;
        while (j < plain_text.size() && js_is_space[static_cast<uint8_t> (plain_text[j])]) {
            j++;
        }

        data->i += j - start;
    }

    start = j;
    while (j < plain_text.size() && !js_is_space2[static_cast<unsigned char>(plain_text[j])]) {
        j++;
    }

    if (data->buffer.size() + (j - start) > sizeof ("false") - 1) {
        data->st = json_invalid_char(plain_text, data->i);
        return 1;
    }

    data->buffer.append(plain_text.data() + start, j - start);
    data->i += j - start;

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        auto &b = data->paths.back();

        if (data->buffer == "true") {
            *b.p = manapi::json(true);
        } else if (data->buffer == "false") {
            *b.p = manapi::json(false);
        } else if (data->buffer == "null") {
            *b.p = manapi::json(nullptr);
        } else {
            data->st= manapi::json_error::status_invalid_argument("json: invalid string", j, manapi::json_format_path2(data->paths));
            return 1;
        }

        data->buffer.clear();
        if (::json_builder_final_object(data))
            return 1;
    }
    return 0;
}

static int json_builder_build_object(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    auto z = data->paths.back().p;
    std::size_t start;

    if (z->is_null()) {
        start = j;

        while (j < plain_text.size() && js_is_space[static_cast<uint8_t>(plain_text[j])])
            j++;

        data->i += j - start;

        if (j == plain_text.size())
            return 0;

        if (plain_text[j] == '{') {
            *z = manapi::json::object();

            j++;
            data->i++;
        }
        else {
            data->st = json_invalid_char(plain_text, data->i);
            return 1;
        }
    }

    while (j < plain_text.size()) {
        start = j;
        while (j < plain_text.size() && js_is_space[static_cast<uint8_t>(plain_text[j])])
            j++;

        data->i += j - start;

        if (j == plain_text.size())
            break;

        if (data->flags & JSON_FLAG_GO_TO_DELIM) {
            switch (plain_text[j]) {
                case '}': {
                    if (!(data->flags & JSON_FLAG_IS_KEY))
                        data->flags ^= JSON_FLAG_GO_TO_DELIM;
                    break;
                }
                case ':': {
                    data->flags ^= JSON_FLAG_GO_TO_DELIM;

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
                                            data->st = manapi::json_error::status_invalid_argument("json_mask: key is invalid",
                                                std::format("key({})", b.it->first), data->i, manapi::json_format_path2(data->paths));
                                            return 1;
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

                        return 0;
                    }

                    data->st =  json_invalid_char(plain_text, data->i);
                    return 1;
                }
                case ',':
                    data->flags ^= JSON_FLAG_GO_TO_DELIM;

                    if (!(data->flags & JSON_FLAG_IS_KEY)) {
                        data->i++;
                        j++;
                        continue;
                    }
                    data->st =  json_invalid_char(plain_text, data->i);
                    return 1;
                default:
                    data->flags ^= JSON_FLAG_GO_TO_DELIM;

                    data->st = json_invalid_char(plain_text, j);
                    return 1;
            }
        }
        else {
            switch (plain_text[j]) {
                case '}': {
                    break;
                }
                default:
                    if (!(data->flags & JSON_FLAG_IS_KEY)) {
                        // is key
                        data->flags |= JSON_FLAG_IS_KEY;
                        data->state = JSON_CALLBACK_CHECK_TYPE;
                        return 0;
                    }

                    data->st =  json_invalid_char(plain_text, data->i);
                    return 1;
            }
        }

        break;
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        // true when {..., ...',' <- we are expecting a key}, false otherwise
        if (data->flags & JSON_FLAG_IS_KEY || data->flags & JSON_FLAG_GO_TO_DELIM) {
            data->st = json_unexpected_end(data->i);
            return 1;
        }

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        if (::json_builder_final_object(data)) {
            return 1;
        }
    }
    return 0;
}

static int json_builder_build_array(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    auto z = data->paths.back().p;
    std::size_t start;
    if (z->is_null()) {

        start = j;
        while (j < plain_text.size() && js_is_space[static_cast<unsigned char>(plain_text[j])]) {
            j++;
        }

        data->i += j - start;

        if (plain_text.size() == j)
            return 0;

        if (plain_text[j] == '[') {
            *z = manapi::json::array();

            j++;
            data->i++;
        }
        else {
            data->st = json_invalid_char(plain_text, data->i);
            return 1;
        }
    }

    while (j < plain_text.size()) {

        start = j;
        while (j < plain_text.size() && js_is_space[static_cast<unsigned char>(plain_text[j])]) {
            j++;
        }

        data->i += j - start;

        if (j == plain_text.size())
            break;

        if (data->flags & JSON_FLAG_GO_TO_DELIM) {
            data->flags ^= JSON_FLAG_GO_TO_DELIM;

            switch (plain_text[j]) {
                case ',':
                    j++;
                    data->i++;
                    continue;
                case ']':
                    break;
                default:
                    data->st = json_invalid_char(plain_text, data->i);
                    return 1;
            }
        }
        else {
            if (plain_text[j] == ']') {
                break;
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

            return 0;
        }

        break;
    }

    if (j != plain_text.size() || (data->flags & JSON_FLAG_FIN)) {
        if (data->flags & JSON_FLAG_GO_TO_DELIM) {
            data->st = json_unexpected_end(data->i);
            return 1;
        }

        if (j < plain_text.size()) {
            data->i++;
            j++;
        }

        if (::json_builder_final_object(data)) {
            return 1;
        }
    }

    return 0;
}

static int json_builder_call_state(manapi::json_builder::data_t *data, const std::string_view &plain_text, size_t &j) {
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
            data->st = manapi::json_error::status_invalid_argument("unreachable",
                data->i, manapi::json_format_path2(data->paths));
            return 1;
    }
}

static manapi::json_error::status json_builder_parse(manapi::json_builder::data_t *data, std::string_view plain_text, size_t &j) {
    if (data->flags & JSON_FLAG_FAILED)
        return manapi::json_error::status {manapi::status_invalid_argument("json:build was failed")};

    while (j < plain_text.size()) {
        // ????: yo, 2026: what?
        if (::json_builder_call_state (data, plain_text, j)) {
            data->flags |= JSON_FLAG_FAILED;
            return std::move(data->st);
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
    data->surrogate_high = 0;
    data->buffer.clear();
    data->state = JSON_CALLBACK_CHECK_TYPE;
    data->types = nullptr;
    data->tindx = 0;
    data->paths.clear();
    data->object = nullptr;
    data->st = manapi::json_error::status_ok();
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

            if (::json_builder_call_state(this->m_data.get(), {}, j)) {
                auto res = std::move(this->m_data->st);
                this->clear();
                return std::move(res);
            }

            if (!this->is_ready()) {
                auto const indx = this->m_data->i;
                this->clear();
                return ::json_unexpected_end(indx);
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

void manapi::json_builder::flags(uint32_t flags) {
    this->m_data->flags |= (flags << JSON_FLAG_MAX_SHIFT);
}

void manapi::json_builder::clear() {
    ::json_builder_reset(this->m_data.get());
}
