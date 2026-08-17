#include <utility>

#include "ManapiDebug.hpp"
#include "json/ManapiJsonMask.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiJsonMaskUtils.hpp"
#include "json/ManapiJsonInternal.hpp"

#define MANAPIHTTP_JSON_ANY (-1)
#define MANAPIHTTP_JSON_NONE (-2)

#define MANAPIHTTP_MASK_COMPARE_NONE (-1)
#define MANAPIHTTP_MASK_COMPARE_EQUAL 0
#define MANAPIHTTP_MASK_COMPARE_GREATER 1
#define MANAPIHTTP_MASK_COMPARE_LESS 2
#define MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER 3
#define MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS 4

#define THROW_MANAPIHTTP_JSON_MASK_ERROR(errnum, msg, ...) throw manapi::json_parse_exception(errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_MASK_ERROR2(errnum, msg) throw manapi::json_parse_exception(errnum, std::format(msg));

enum json_mask_flags {
    JSON_MASK_FLAG_INCOMPLETE = 1<<0,
    JSON_MASK_FLAG_VIEW = 1<<1
};

manapi::json_error::status::status(err_num code, std::string_view msg, std::string data, std::size_t pos, std::string path) : manapi::status(code, msg) {
    this->m_pos = pos;
    this->m_path = std::move(path);
    this->m_data = std::move(data);
}

manapi::json_error::status::status(err_num code, std::string msg, std::string data, std::size_t pos, std::string path) : manapi::status(code, std::move(msg)) {
    this->m_pos = pos;
    this->m_path = std::move(path);
    this->m_data = std::move(data);
}

manapi::json_error::status::status(err_num code, const char *msg, std::string data, std::size_t pos, std::string path) : manapi::status(code, msg) {
    this->m_pos = pos;
    this->m_path = std::move(path);
    this->m_data = std::move(data);
}


manapi::json_error::status::status() : manapi::status() {
    this->m_pos = 0;
}


manapi::json_error::status::status(err_num code, std::string_view msg, std::size_t pos, std::string path) : json_error::status(code, msg, std::string{}, pos, std::move(path)) {
}

manapi::json_error::status::~status() = default;

manapi::json_error::status::status(const status &err) {
    this->m_pos = 0;
    manapi::status::operator=(std::forward<decltype(err)>(err));
}

manapi::json_error::status::status(json_error::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
    this->m_path = std::move(n.m_path);
    this->m_pos = std::exchange(n.m_pos, 0);
    manapi::status::operator=(std::forward<decltype(n)>(n));
}

manapi::json_error::status & manapi::json_error::status::operator=(json_error::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
    this->m_path = std::move(n.m_path);
    this->m_pos = std::exchange(n.m_pos, 0);
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

manapi::json_error::status::status(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_pos = 0;
    manapi::status::operator=(std::forward<decltype(n)>(n));

}

manapi::json_error::status & manapi::json_error::status::operator=(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_pos = 0;
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

std::string manapi::json_error::status::fullmsg() const {
    return std::format("{} pos={} path={} data={}", manapi::status::fullmsg(), this->m_pos, this->m_path, this->m_data);
}

std::string manapi::json_error::status::path() {
    return std::move(this->m_path);
}

std::string manapi::json_error::status::additional_data() {
    return std::move(this->m_data);
}

std::size_t manapi::json_error::status::pos() const {
    return this->m_pos;
}

manapi::json_error::status manapi::json_error::status_invalid_argument(std::string_view msg, std::size_t pos, std::string path) {
    return {ERR_INVALID_ARGUMENT, msg, pos, std::move(path)};
}

manapi::json_error::status manapi::json_error::status_invalid_argument(std::string_view msg, std::string data, std::size_t pos, std::string path) {
    return {ERR_INVALID_ARGUMENT, msg, std::move(data), pos, std::move(path)};
}

manapi::json_error::status manapi::json_error::status_ok() {
    return {ERR_OK, "OK", {}, {}};
}

static std::unique_ptr<manapi::json_mask_object_t> json_mask_initial_resolve_data(manapi::json obj) {
    auto zres = std::make_unique<manapi::json_mask_object_t>();

    if (obj.is_string()) {
        auto &str = obj.as_string();

        // if {x}
        if (str.size() <= 2
                || *str.begin() != '{'
                || *str.rbegin() != '}') {

            zres->flags = 0;
            zres->types.push_back(std::make_unique<manapi::json_mask_type_t>());
            auto &b = zres->types.back();
            b->flags = 0;
            b->type = manapi::json::type_string;
            b->mean = std::move(str);
            b->parent = zres.get();

            return std::move(zres);
        }

        bool none = false;
        size_t i = 1;
        size_t m = str.size() - 1;

        zres->flags = 0;

repeat:

        std::string type;
        int ntype;

        bool bracket = false;
        bool square_bracket = false;
        bool quotes = false;
        bool special_type = false;

        // calc type
        for (; i < m; i++) {
            if (std::isalpha(str.at(i))) {
                type += str.at(i);
                continue;
            }

            break;
        }


        // check types
        if (type == "string")
            ntype = manapi::json::type_string;
        else if (type == "integer")
            ntype = manapi::json::type_integer;
        else if (type == "decimal")
            ntype = manapi::json::type_decimal;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        else if (type == "bigint")
            ntype = manapi::json::type_bigint;
#endif
        else if (type == "bool")
            ntype = manapi::json::type_boolean;
        else if (type == "null")
            ntype = manapi::json::type_null;
        else if (type == "number")
            ntype = manapi::json::type_number;
        else if (type == "any")
            ntype = MANAPIHTTP_JSON_ANY;
        else if (type == "none") {
            none = true;
            special_type = true;
        }
        else
            THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED,
                "Could not resolve type for this expression: {}", manapi::unicode::escape_string(str));

        if (!special_type) {
            zres->types.push_back(std::make_unique<manapi::json_mask_type_t>());
            auto &b = zres->types.back();
            b->type = static_cast<int>(ntype);
            b->parent = zres.get();
        }

        manapi::json parsed_buff;
        manapi::json_builder builder;

        // compare type (=, >=, <=, >, <)
        char compare_type = MANAPIHTTP_MASK_COMPARE_NONE;

        // calc params
        for (; i < m; i++) {
            char c = str.at(i);

            if (bracket || square_bracket) {
                if (c == '"' && ntype == manapi::json::type_string) {
                    if (!quotes) {
                        if (!builder.is_empty()) {
                            THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                        }
                    }
                    builder << '"';
                    if (quotes) {
                        if (builder.is_ready()) {
                            auto res = builder.get();
                            zres->types.back()->mean = res.unwrap();
                            quotes = false;
                        }
                    }
                    else {
                        quotes = true;
                    }
                    continue;
                }
                if (quotes) {
                    builder << c;
                }
                else {
                    for (; i < m; i++) {
                        c = str.at(i);

                        if (c == '=') {
                            if (compare_type == MANAPIHTTP_MASK_COMPARE_GREATER) {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER;
                            }
                            else if (compare_type == MANAPIHTTP_MASK_COMPARE_LESS) {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS;
                            }
                            else if (compare_type == MANAPIHTTP_MASK_COMPARE_NONE) {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL;
                            }
                            else {
                                THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }
                        }
                        else if (c == '>') {
                            if (compare_type != MANAPIHTTP_MASK_COMPARE_EQUAL && compare_type != MANAPIHTTP_MASK_COMPARE_NONE) {
                                THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }

                            compare_type = MANAPIHTTP_MASK_COMPARE_GREATER;
                        }
                        else if (c == '<') {
                            if (compare_type != MANAPIHTTP_MASK_COMPARE_EQUAL && compare_type != MANAPIHTTP_MASK_COMPARE_NONE) {
                                THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }

                            compare_type = MANAPIHTTP_MASK_COMPARE_LESS;
                        }
                        else {
                            break;
                        }
                    }

                    if ((bracket && c == ')') || (square_bracket && c == ']')) {

                        if (bracket) {
                            bracket = false;
                        }

                        if (square_bracket) {
                            square_bracket = false;
                        }

                        if (builder.is_empty()) {
                            continue;
                        }

                    }
                    else if (c != ' ') {
                        builder << c;
                        continue;
                        //THROW_MANAPIHTTP_JSON_MASK_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                    }

                    if (builder.is_empty()) {
                        continue;
                    }

                    if (!special_type) {
                        if (ntype == manapi::json::type_decimal)
                            parsed_buff = std::move(builder.get().unwrap().cast_decimal());

    #ifdef MANAPIHTTP_BIGINT_SUPPORT
                        else if (ntype == manapi::json::type_bigint)
                            parsed_buff = std::move(builder.get().unwrap().cast_bigint());

    #endif
                        else if (ntype == manapi::json::type_boolean)
                            parsed_buff = std::move(builder.get().unwrap().cast_bool());
                        else
                            // others
                            parsed_buff = std::move(builder.get().unwrap().cast_integer());

                        auto &b = zres->types.back();

                        switch (compare_type) {
                            case MANAPIHTTP_MASK_COMPARE_NONE:
                                // it is value
                                zres->types.back()->mean = (parsed_buff);
                            break;
                            case MANAPIHTTP_MASK_COMPARE_EQUAL:
                                b->flags |= manapi::JSON_MASK_TYPE_FLAG_E|manapi::JSON_MASK_TYPE_FLAG_S;
                                b->max_mean = (parsed_buff);
                                b->min_mean = (parsed_buff);
                            break;
                            case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS:
                                b->flags |= manapi::JSON_MASK_TYPE_FLAG_E;
                                b->max_mean = (parsed_buff);
                            break;
                            case MANAPIHTTP_MASK_COMPARE_LESS:
                                if (b->flags & manapi::JSON_MASK_TYPE_FLAG_E)
                                    b->flags ^= manapi::JSON_MASK_TYPE_FLAG_E;
                                b->max_mean = (parsed_buff);
                            break;
                            case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER:
                                b->flags |= manapi::JSON_MASK_TYPE_FLAG_S;
                                b->min_mean = (parsed_buff);
                            break;
                            case MANAPIHTTP_MASK_COMPARE_GREATER:
                                if (b->flags & manapi::JSON_MASK_TYPE_FLAG_S)
                                    b->flags ^= manapi::JSON_MASK_TYPE_FLAG_S;
                                b->min_mean = (parsed_buff);
                            break;
                            default:
                                THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Bug has been detected: {}", "compare type has invalid value");
                        }
                    }

                    // clean up
                    builder.clear();
                    compare_type = MANAPIHTTP_MASK_COMPARE_NONE;
                }
            }

            else if (c == '(') {
                if (ntype == manapi::json::type_array || ntype == manapi::json::type_object) {
                    THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                }
                bracket = true;
                builder.clear();
            }

            else if (c == '[') {
                // change type to array
                ntype = manapi::json::type_array;

                auto p = std::make_unique<manapi::json_mask_object_t>();
                p->flags = 0;
                p->types.push_back (std::make_unique<manapi::json_mask_type_t>());
                auto &b = p->types.back();
                b->parent = p.get();
                b->type = manapi::json::type_array;
                p->parent = zres->parent;
                zres->parent = b.get();
                b->zdefault = std::move(zres);
                zres = std::move(p);

                square_bracket = true;
                builder.clear();
            }

            else if (c == '|') {
                i++;

                goto repeat;
            }

            else {
                THROW_MANAPIHTTP_JSON_MASK_ERROR (manapi::ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
            }
        }

        goto end;
        end:

        if (!builder.is_empty()) {
            THROW_MANAPIHTTP_JSON_MASK_ERROR(manapi::ERR_JSON_UNEXPECTED_END, "Unexpected end at {}", m);
        }

        if (none) {
            zres->flags |= manapi::JSON_MASK_OBJECT_FLAG_NULL;
        }
    }
    else if (obj.is_object()) {
        auto zobj = manapi::json::object();

        zres->flags = 0;
        zres->types.push_back(std::make_unique<manapi::json_mask_type_t>());
        auto &b = zres->types.back();
        b->type = manapi::json::type_object;
        b->parent = zres.get();

        for (auto it = obj.begin<manapi::json::OBJECT>(); it != obj.end<manapi::json::OBJECT>(); ++it) {
            auto gg = ::json_mask_initial_resolve_data(std::move(it->second));
            gg->parent = zres->types.back().get();
            auto fit = zobj.insert({it->first, manapi::json{}});
            fit.first->second.source(gg.release());
        }

        zres->types.back()->mean = std::move(zobj);
    }
    else if (obj.is_array()) {

        zres->flags = 0;
        zres->types.push_back(std::make_unique<manapi::json_mask_type_t>());
        auto &b = zres->types.back();
        b->type = manapi::json::type_array;
        b->parent = zres.get();

        auto arr = manapi::json::array();
        for (auto it = obj.begin<manapi::json::ARRAY>(); it != obj.end<manapi::json::ARRAY>(); ++it) {
            auto gg = ::json_mask_initial_resolve_data(std::move(*it));
            gg->parent = zres->types.back().get();
            manapi::json z;
            z.source(gg.release());
            arr.push_back(std::move(z));
        }

        zres->types.back()->mean = std::move(arr);
    }
    else if (obj.is_source()) {
        auto z1 = dynamic_cast <manapi::json_mask_object_t *>(&obj.as_source());
        assert(z1);
        obj.release_source();
        return std::unique_ptr<manapi::json_mask_object_t> (z1);
    }
    else {
        zres->flags = 0;
        zres->types.push_back(std::make_unique<manapi::json_mask_type_t>());
        auto &b = zres->types.back();
        b->type = obj.data_type();
        b->mean = std::move(obj);
        b->parent = zres.get();
    }

    return std::move(zres);
}


manapi::json_mask_object_t::json_mask_object_t() {
    this->flags = 0;
    this->parent = nullptr;
}

manapi::json_mask_object_t::~json_mask_object_t() {

}

manapi::json_mask_object_t::json_mask_object_t(const json_mask_object_t &n) : json_mask_object_t() {
    this->operator=(n);
}

manapi::json_mask_object_t::json_mask_object_t(json_mask_object_t &&n) MANAPIHTTP_NOEXCEPT : json_mask_object_t() {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::json_mask_object_t & manapi::json_mask_object_t::operator=(const json_mask_object_t &n) {
    if (this != &n) {
        this->flags = n.flags;
        this->parent = n.parent;
        this->types.reserve(n.types.size());

        for (auto &t : n.types) {
            this->types.push_back(std::make_unique<json_mask_type_t>());
            auto &b = this->types.back();
            *b = *t;
            b->parent = this;
        }
    }

    return *this;
}

manapi::json_mask_object_t & manapi::json_mask_object_t::operator=(json_mask_object_t &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        this->flags = n.flags;
        this->parent = n.parent;
        this->types = std::move(n.types);

        for (auto &t : this->types)
            t->parent = this;

        n.flags = 0;
        n.parent = nullptr;
    }

    return *this;
}

manapi::json_source * manapi::json_mask_object_t::copy() const {
    auto g = std::make_unique<manapi::json_mask_object_t>();
    *g = *this;
    return g.release();
}

manapi::json_mask_type_t::json_mask_type_t() {
    this->flags = 0;
    this->parent = nullptr;
    this->type = 0;
}

manapi::json_mask_type_t::json_mask_type_t(const json_mask_type_t &n) : json_mask_type_t() {
    this->operator=(n);
}

manapi::json_mask_type_t::json_mask_type_t(json_mask_type_t &&n) MANAPIHTTP_NOEXCEPT : json_mask_type_t() {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::json_mask_type_t & manapi::json_mask_type_t::operator=(const manapi::json_mask_type_t &n) {
    if (this != &n) {
        this->flags = n.flags;
        this->type = n.type;
        this->parent = n.parent;
        this->max_mean = n.max_mean;
        this->min_mean = n.min_mean;
        this->mean = n.mean;

        if (n.zdefault) {
            this->zdefault = std::make_unique<manapi::json_mask_object_t>(*n.zdefault);
            this->zdefault->parent = this;
        }
        else
            this->zdefault = nullptr;


        if (this->mean.is_source()) {
            auto oit = dynamic_cast<manapi::json_mask_object_t *>(&this->mean.as_source());
            if (oit) {
                oit->parent = this;
            }
        }
        else if (this->mean.is_object()) {
            for (auto &z : this->mean.entries()) {
                if (z.second.is_source()) {
                    auto oit = dynamic_cast<manapi::json_mask_object_t *>(&z.second.as_source());
                    if (oit)
                        oit->parent = this;
                }
            }
        }
        else if (this->mean.is_array()) {
            for (auto &z : this->mean.each()) {
                if (z.is_source()) {
                    auto oit = dynamic_cast<manapi::json_mask_object_t *>(&z.as_source());
                    if (oit)
                        oit->parent = this;
                }
            }
        }
    }

    return *this;
}

manapi::json_mask_type_t & manapi::json_mask_type_t::operator=(manapi::json_mask_type_t &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        this->flags = n.flags;
        this->max_mean = std::move(n.max_mean);
        this->mean = std::move(n.mean);
        this->min_mean = std::move(n.min_mean);
        this->parent = n.parent;
        this->type = n.type;
        this->zdefault = std::move(n.zdefault);

        n.parent = nullptr;
        n.type = 0;
        n.flags = 0;

        if (this->zdefault)
            this->zdefault->parent = this;

        if (this->mean.is_source()) {
            auto oit = dynamic_cast<manapi::json_mask_object_t *>(&this->mean.as_source());
            if (oit) {
                oit->parent = this;
            }
        }
        else if (this->mean.is_object()) {
            for (auto &z : this->mean.entries()) {
                if (z.second.is_source()) {
                    auto oit = dynamic_cast<manapi::json_mask_object_t *>(&z.second.as_source());
                    if (oit)
                        oit->parent = this;
                }
            }
        }
        else if (this->mean.is_array()) {
            for (auto &z : this->mean.each()) {
                if (z.is_source()) {
                    auto oit = dynamic_cast<manapi::json_mask_object_t *>(&z.as_source());
                    if (oit)
                        oit->parent = this;
                }
            }
        }
    }

    return *this;
}

std::size_t manapi::json_source::dump_size(manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) const {
    return sizeof ("undefined") - 1;
}

void manapi::json_source::dump(manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) const {
    p->push_back("undefined", sizeof ("undefined") - 1);
}

template<typename T>
manapi::json_error::status json_mask_cmp_value(const T &val, const manapi::json_mask_type_t &type, const std::vector<manapi::json_mask_path_t> &path, int flags) {

    if (!(flags & JSON_MASK_FLAG_INCOMPLETE) && !type.min_mean.is_null()) {
        if (!manapi::json_verify_min_mean<T>(type.flags, type.min_mean, val))
            return manapi::json_error::status_invalid_argument("json_mask: value is lower or equals min_mean",
                std::format("min_mean={}", type.min_mean.dump()), 0, manapi::json_format_path2(path));
    }

    if (!type.max_mean.is_null()) {
        if (!manapi::json_verify_max_mean<T>(type.flags, type.max_mean, val)) {
            return manapi::json_error::status_invalid_argument("json_mask: value is greater or equals max_mean",
                std::format("max_mean={}", type.max_mean.dump()), 0, manapi::json_format_path2(path));
        }
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_mask_valid_val (int flags, const manapi::json &z, const manapi::json_mask_type_t &p, std::vector<::manapi::json_mask_path_t> &paths) {

    if (p.type == manapi::json::type_string) {
        // invalid type
        if (!z.is_string() && !z.is_slice()) {
            return manapi::json_error::status_invalid_argument("json_mask: must be a string", 0, manapi::json_format_path2(paths));
        }

        auto res = json_mask_cmp_value<size_t> (z.size(), p, paths, 0);
        if (!res.ok()) {
            return std::move(res);
        }

        // by value ex: STR1 != STR2
        if (!p.mean.is_null()) {
            if (p.mean != z)
                return manapi::json_error::status_invalid_argument("json_mask: value isn't the same",
                    std::format("value({})", p.mean.dump()), 0, manapi::json_format_path2(paths));
        }

        // ex: {string}
        return manapi::json_error::status_ok();
    }
    if (p.type == manapi::json::type_null) {
        // invalid type
        if (!z.is_null())
            return manapi::json_error::status_invalid_argument("json_mask: must be a null", 0, manapi::json_format_path2(paths));
        return manapi::json_error::status_ok();
    }
    if (p.type == manapi::json::type_boolean) {
        // invalid type
        if (!z.is_bool())
            return manapi::json_error::status_invalid_argument("json_mask: must be a bool", 0, manapi::json_format_path2(paths));


        if (!p.mean.is_null()) {
            if (p.mean != z)
                return manapi::json_error::status_invalid_argument("json_mask: value isn't the same", std::format("value({})", p.mean.dump()), 0, manapi::json_format_path2(paths));
        }

        return manapi::json_error::status_ok();
    }
    if (p.type == manapi::json::type_integer) {
        // invalid type
        if (!z.is_integer()) {
            return manapi::json_error::status_invalid_argument("json_mask: must be an integer", 0, manapi::json_format_path2(paths));
        }

        // invalid value
        if (!p.mean.is_null()) {
            if (p.mean != z)
                return manapi::json_error::status_invalid_argument("json_mask: value isn't the same", std::format("value({})", p.mean.dump()), 0, manapi::json_format_path2(paths));
        }

        auto res = json_mask_cmp_value<manapi::json::INTEGER> (z.as_integer(), p, paths, 0);
        if (!res.ok())
            return std::move(res);

        // ex: {number}
        return manapi::json_error::status_ok();
    }
    if (p.type == manapi::json::type_decimal) {
        // invalid type
        if (!z.is_decimal()) {
            return manapi::json_error::status_invalid_argument("json_mask: must be a decimal", 0, manapi::json_format_path2(paths));
        }

        // invalid value
        if (!p.mean.is_null()) {
            if (z != p.mean)
                return manapi::json_error::status_invalid_argument("json_mask: value isn't the same", std::format("value({})", p.mean.dump()), 0, manapi::json_format_path2(paths));
        }

        auto res = json_mask_cmp_value<manapi::json::DECIMAL> (z.as_decimal(), p, paths, 0);
        if (!res.ok())
            return std::move(res);

        // ex: {decimal}
        return manapi::json_error::status_ok();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (p.type == manapi::json::type_bigint) {
        // invalid type
        if (!z.is_bigint()) {
            return manapi::json_error::status_invalid_argument("json_mask: must be a bigint", 0, manapi::json_format_path2(paths));
        }

        // invalid value
        if (!p.mean.is_null()) {
            if (z != p.mean)
                return manapi::json_error::status_invalid_argument("json_mask: value isn't the same",
                    std::format("value({})", p.mean.as_bigint().stringify()), 0, manapi::json_format_path2(paths));
        }

        auto res = json_mask_cmp_value<manapi::bigint> (z.as_bigint(), p, paths, 0);
        if (!res.ok())
            return std::move(res);

        // ex: {bigint}
        return manapi::json_error::status_ok();
    }
#endif
    if (p.type == manapi::json::type_number) {
        if (
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            !z.is_bigint() &&
#endif
            !z.is_integer() && !z.is_decimal()) {
            return manapi::json_error::status_invalid_argument("json_mask: must be a number", 0, manapi::json_format_path2(paths));
        }

        // TODO: please I need this

        return manapi::json_error::status_ok();
    }

    if (p.type == -1) {
        // any type
        return manapi::json_error::status_ok();
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_mask_valid_obj (int flags, const manapi::json &z, const manapi::json_mask_type_t &p, std::vector<::manapi::json_mask_path_t> &paths, manapi::json_mask_object_t const**ptype) {
    if (!z.is_object())
        return manapi::json_error::status_invalid_argument("json_mask: must be an object", 0, json_format_path2(paths));

    enum obj_flags {
        OBJ_FLAG_LOCK = 1<<0,
        OBJ_FLAG_MEAN_FIN = 1<<1
    };

    auto &b = paths.back();
    auto res = json_mask_cmp_value<size_t> (z.size(), p, paths, flags & JSON_MASK_FLAG_INCOMPLETE);
    if (!res.ok())
        return std::move(res);

    if (!(b.flags & OBJ_FLAG_MEAN_FIN)) {
        if (!p.mean.is_null()) {
            if (z.size() > p.mean.size())
                return manapi::json_error::status_invalid_argument("json_mask: size is invalid", std::format("size({})", p.mean.size()), 0, json_format_path2(paths));

            if (!(flags & JSON_MASK_FLAG_INCOMPLETE)) {
                auto mean = const_cast<manapi::json *>(&p.mean);
                auto &gz = z.as_object();
                if (!(b.flags & OBJ_FLAG_LOCK)) {
                    b.it = mean->begin<manapi::json::OBJECT>();
                    b.flags |= OBJ_FLAG_LOCK;
                }
                else {
                    ++b.it;
                }

                auto &it = b.it;

                for (; it != mean->end<manapi::json::OBJECT>(); ++it) {
                    auto fit = gz.find(it->first);
                    auto itp = &it->second;
                    assert(itp && itp->is_source());
                    auto objp = dynamic_cast<const manapi::json_mask_object_t *>(&itp->as_source());
                    // incorrect key
                    if (fit == gz.end()) {
                        if (objp->flags & manapi::JSON_MASK_OBJECT_FLAG_NULL)
                            continue;

                        return manapi::json_error::status_invalid_argument("json_mask: key doesn't exist", std::format("key({})", it->first),
                            0, json_format_path2(paths));
                    }

                    // incorrect value
                    paths.push_back(manapi::json_mask_path_t {
                        .p = const_cast<manapi::json *>(&fit->second),
                    });

                    *ptype = objp;

                    return manapi::json_error::status_ok();
                }

                b.flags |= OBJ_FLAG_MEAN_FIN;
                b.flags ^= OBJ_FLAG_LOCK;
            }
            else {
                auto &gz = p.mean.as_object();
                auto zz = const_cast<manapi::json *> (&z);
                if (!(b.flags & OBJ_FLAG_LOCK)) {
                    b.flags |= OBJ_FLAG_LOCK;
                    b.it = zz->begin<manapi::json::OBJECT>();
                }
                else {
                    ++b.it;
                }
                auto &it = b.it;
                for (; it != zz->end<manapi::json::OBJECT>(); ++it) {
                    auto fit = gz.find(it->first);

                    // doesn't exist
                    if (fit == gz.end())
                        return manapi::json_error::status_invalid_argument("json_mask: key is invalid",
                            std::format("key({})", it->first), 0, json_format_path2(paths));

                    auto itp = &fit->second;
                    assert(itp && itp->is_source());
                    auto objp = dynamic_cast<const manapi::json_mask_object_t *>(&itp->as_source());

                    paths.push_back(manapi::json_mask_path_t {
                        .p = &it->second,
                    });

                    *ptype = objp;

                    return manapi::json_error::status_ok();

                }
                b.flags ^= OBJ_FLAG_LOCK;
            }
        }
        b.flags |= OBJ_FLAG_MEAN_FIN;
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_mask_valid_arr (int flags, const manapi::json &z, const manapi::json_mask_type_t &p, std::vector<::manapi::json_mask_path_t> &paths, const manapi::json_mask_object_t **ptype) {
    if (!z.is_array())
        return manapi::json_error::status_invalid_argument("json_mask: must be an array", 0, json_format_path2(paths));

    enum arr_flags {
        ARR_FLAG_LOCK = 1<<0,
        ARR_FLAG_MEAN_FIN = 1<<1,
        ARR_FLAG_DEFAULT_FIN = 1<<2
    };

    auto &b = paths.back();
    auto res = json_mask_cmp_value<size_t> (z.size(), p, paths, flags & JSON_MASK_FLAG_INCOMPLETE);
    if (!res.ok())
        return std::move(res);

    if (!(b.flags & ARR_FLAG_MEAN_FIN)) {
        if (!p.mean.is_null()) {
            if (!(b.flags & ARR_FLAG_LOCK)) {
                b.flags |= ARR_FLAG_LOCK;
                b.indx = 0;

                if (p.mean.size() != z.size()) {
                    return manapi::json_error::status_invalid_argument("json_mask: size isn't the same",
                        std::format("size({})", p.mean.size()), 0, json_format_path2(paths));
                }
            }
            else {
                ++b.indx;
            }

            for (; b.indx < p.mean.size(); ) {
                paths.push_back(manapi::json_mask_path_t {
                    .p = const_cast<manapi::json *>(&z[b.indx])
                });

                auto gz = &p.mean[b.indx];
                assert(gz && gz->is_source());
                *ptype = dynamic_cast<const manapi::json_mask_object_t *>(&gz->as_source());

                return manapi::json_error::status_ok();
            }

            b.flags ^= ARR_FLAG_LOCK;
        }

        b.flags |= ARR_FLAG_MEAN_FIN;
    }

    if (!(b.flags & ARR_FLAG_DEFAULT_FIN)) {
        if (p.zdefault) {
            if (!(b.flags & ARR_FLAG_LOCK)) {
                b.flags |= ARR_FLAG_LOCK;
                b.indx = 0;
            }
            else {
                ++b.indx;
            }

            for (; b.indx < z.size(); ) {
                paths.push_back(manapi::json_mask_path_t {
                    .p = const_cast<manapi::json *> (&z[b.indx])
                });

                *ptype = p.zdefault.get();

                return manapi::json_error::status_ok();
            }
            b.flags ^= ARR_FLAG_LOCK;
        }
        b.flags |= ARR_FLAG_DEFAULT_FIN;
    }

    return manapi::json_error::status_ok();
}

static manapi::json_error::status json_mask_valid (int flags, const manapi::json &z, const manapi::json_mask_object_t *p, manapi::json_mask_path_t *orig, uint32_t orig_len, const manapi::json_mask_object_t **out) {
    enum valid_flags {
        VALID_FLAG_SKIP_TYPE = 1<<0
    };

    auto st = manapi::json_error::status_ok();
    int pflags = 0;
    int eway = 0;
    uint32_t p2indx = 0;

    std::vector<manapi::json_mask_path_t> paths;
    paths.push_back(manapi::json_mask_path_t {
        .p = const_cast<manapi::json *>(&z)
    });

    if (orig_len) {
        assert(!p2indx && paths.back().p==orig[p2indx].p);
        paths.back().type = orig[p2indx].type;
    }

    assert(!p->types.empty());

    if (p->types.size() > paths.back().type + 1) {
        eway++;
    }

    while (!paths.empty()) {
        paths.reserve(paths.size() + 1);
        auto &b = paths.back();

        if (pflags & VALID_FLAG_SKIP_TYPE) {
            assert(p->types.size() > b.type);
            bool const eorig = p2indx < orig_len && b.p == orig[p2indx].p;

            b.type++;
            b.flags = 0;

            if (eorig) {
                orig[p2indx].type++;

                for (uint32_t j = p2indx+1; j < orig_len; j++) {
                    orig[j].type = 0;
                }
            }

            auto d = p->types.size() - b.type;
            if (!d) {
                if (eorig) {
                    orig[p2indx].type=0;
                    assert(p2indx);
                    p2indx--;
                }

                paths.pop_back();
                assert(p);
                p = p->parent->parent;

                continue;
            }

            if (d == 1) {
                // one type left
                eway--;
            }

            pflags ^= VALID_FLAG_SKIP_TYPE;
        }

        auto cur = p->types[b.type].get();
        assert(cur->parent == p);

        try {
            if (cur->type == manapi::json::type_object) {
                st = ::json_mask_valid_obj(flags, *b.p, *cur, paths, &p);
            }
            else if (cur->type == manapi::json::type_array) {
                st = ::json_mask_valid_arr(flags, *b.p, *cur, paths, &p);
            }
            else {
                st = ::json_mask_valid_val(flags, *b.p, *cur, paths);
            }
        }
        catch (std::exception const &e) {
            manapi_log_trace2("manapihttp::json_mask", e.what());
            return  manapi::json_error::status_invalid_argument("json_mask: failed", 0, manapi::json_format_path2(paths));
        }

        if (st.ok()) {
            if (&b != &paths.back()) {
                if (orig) {
                    if (p2indx + 1 < orig_len && paths.back().p == orig[p2indx + 1].p) {
                        auto &bp = paths.back();
                        p2indx++;
                        bp.type = orig[p2indx].type;

                        // if (flags & JSON_MASK_FLAG_INCOMPLETE && bp.p->is_null()) {
                        //     paths.pop_back();
                        //     continue;
                        // }
                    }
                }

                assert(p->types.size() > paths.back().type);
                if (p->types.size() > paths.back().type + 1)
                    eway++;

                continue;
            }

            auto d = p->types.size() - b.type;
            if (d > 1)
                eway--;

            if (p2indx < orig_len && b.p == orig[p2indx].p) {
                if (out && p2indx + 1 == orig_len && b.p == orig[p2indx].p) {
                    *out = p;
                }

                paths.pop_back();

                if (paths.empty()) {
                    assert(!eway);
                    break;
                }

                p2indx--;
            }
            else {
                paths.pop_back();

                if (paths.empty()) {
                    assert(!eway);
                    break;
                }
            }

            assert(p && p->parent);
            p = p->parent->parent;
        }
        else {
            if (!eway)
                break;

            b.flags = 0;
            pflags |= VALID_FLAG_SKIP_TYPE;
            st = manapi::json_error::status_ok();
        }
    }

    assert(!st.ok() || !p2indx);

    return std::move(st);

}


manapi::json_mask::json_mask(const std::initializer_list<json> &data) : m_flags(0) {
    this->m_data = ::json_mask_initial_resolve_data (data).release();
}

manapi::json_mask::json_mask(json data) {
    this->m_data = ::json_mask_initial_resolve_data(std::move(data)).release();
    this->m_flags = 0;
}

manapi::json_mask::json_mask(json_mask &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = n.m_data;
    this->m_flags = n.m_flags;

    n.m_data = nullptr;
    n.m_flags = 0;
}

manapi::json_mask::json_mask(const nullptr_t &) {
    this->m_data = nullptr;
    this->m_flags = 0;
}
//
manapi::json_mask::json_mask(const json_mask &n) {
    if ((n.m_flags & JSON_MASK_FLAG_VIEW) || !n.m_data)
        this->m_data = n.m_data;
    else
        this->m_data = new manapi::json_mask_object_t (*n.m_data);
    this->m_flags = n.m_flags;
}

manapi::json_mask::~json_mask() {
    if (!(this->m_flags & JSON_MASK_FLAG_VIEW))
        delete this->m_data;
}

bool manapi::json_mask::enabled() const {
    return !!this->m_data;
}

manapi::json_error::status manapi::json_mask::valid(const manapi::json &obj) const {
    return this->valid(obj, nullptr, 0, nullptr);
}

manapi::json_error::status manapi::json_mask::valid(const json &obj, manapi::json_mask_path_t *orig, uint32_t orig_len, const json_mask_object_t **out) const {
    if (!this->enabled())
        return json_error::status_invalid_argument("json_mask: disabled", 0, {});

    return ::json_mask_valid (this->m_flags, obj, this->m_data, orig, orig_len, out);
}

manapi::json_error::status manapi::json_mask::valid(const std::map<std::string, std::string> &obj) const {
    if (!this->enabled())
        return json_error::status_invalid_argument("json_mask: disabled", 0, {});

    return ::json_mask_valid (this->m_flags, json{obj}, this->m_data, nullptr, 0, nullptr);
}

manapi::json_error::status manapi::json_mask::valid(const std::map<std::string, std::string, std::less<>> &obj) const {
    if (!this->enabled())
        return json_error::status_invalid_argument("json_mask: disabled", 0, {});

    return ::json_mask_valid (this->m_flags, json{obj}, this->m_data, nullptr, 0, nullptr);
}

const manapi::json_mask_object_t * manapi::json_mask::api_tree() const {
    return this->m_data;
}

void manapi::json_mask::api_tree(const json_mask_object_t * tree, bool own) {
    if (!(this->m_flags & JSON_MASK_FLAG_VIEW)) {
        // own
        delete this->m_data;
    }
    else {
        this->m_flags ^= JSON_MASK_FLAG_VIEW;
    }

    this->m_data = tree;
    if (!own) {
        this->m_flags |= JSON_MASK_FLAG_VIEW;
    }
}

manapi::json manapi::json_mask::Or(json data, bool none) {
    manapi::json zres;
    zres.source(new manapi::json_mask_object_t());
    auto &p = *dynamic_cast<json_mask_object_t *>(&zres.as_source());

    if (data.is_pair())
        data = manapi::json::array({std::move(data)});

    if (!data.is_array()) {
        throw std::runtime_error ("json_mask::Or accepts only array and pair");
    }

    for (auto &item: data.each()) {
        auto z = ::json_mask_initial_resolve_data(std::move(item));

        if (z->flags & manapi::JSON_MASK_OBJECT_FLAG_NULL) {
            none = true;
        }

        for (auto &b : z->types) {
            p.types.push_back(std::move(b));
            p.types.back()->parent = &p;
        }
    }

    if (none)
        p.flags |= manapi::JSON_MASK_OBJECT_FLAG_NULL;

    return std::move(zres);
}

manapi::json manapi::json_mask::Array(json data, ssize_t min, ssize_t max, bool none) {
    manapi::json zres;
    zres.source(new manapi::json_mask_object_t ());
    auto &p = *dynamic_cast<manapi::json_mask_object_t *>(&zres.as_source());

    auto z = ::json_mask_initial_resolve_data(std::move(data));
    p.types.push_back(std::make_unique<manapi::json_mask_type_t>());
    auto &b = p.types.back();
    b->type = manapi::json::type_array;
    b->parent = &p;

    z->parent = b.get();
    b->zdefault = std::move(z);

    if (none)
        p.flags |= manapi::JSON_MASK_OBJECT_FLAG_NULL;

    if (min >= 0) {
        b->flags |= manapi::JSON_MASK_TYPE_FLAG_S;
        b->min_mean = min;
    }

    if (max >= 0) {
        b->flags |= manapi::JSON_MASK_TYPE_FLAG_E;
        b->max_mean = max;
    }

    return std::move(zres);
}

manapi::json manapi::json_mask::Array(json data, bool none) {
    return json_mask::Array(std::move(data), -1, -1, none);
}

void manapi::json_mask::complete_status(bool state) {
    if (!state) this->m_flags |= JSON_MASK_FLAG_INCOMPLETE;
    else if (this->m_flags & JSON_MASK_FLAG_INCOMPLETE)
        this->m_flags ^= JSON_MASK_FLAG_INCOMPLETE;
}