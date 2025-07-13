#include <sstream>

#include "ManapiJsonBuilder.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "include/ManapiUtils.hpp"
#include "include/ManapiJsonMaskUtils.hpp"

enum json_builder_callbacks {
    JSON_CALLBACK_CHECK_TYPE,
    JSON_CALLBACL_BUILD_STRING,
    JSON_CALLBACK_BUILD_NUMERIC,
    JSON_CALLBACK_BUILD_NUMERIC_STRING,
    JSON_CALLBACK_BUILD_OBJECT,
    JSON_CALLBACK_BUILD_ARRAY,
    JSON_CALLBACK_CHECK_END
};

enum json_builder_flags {
    JSON_FLAG_GETTING = 1,
    JSON_FLAG_READY = 2,
    JSON_FLAG_OPERATE_ALREADY = 4,
    JSON_FLAG_EXP_ALREADY = 8,
    JSON_FLAG_OPENED_QUOTE = 16,
    JSON_FLAG_ESCAPED = 32,
    JSON_FLAG_IS_NOT_KEY = 64,
    JSON_FLAG_GO_TO_DELIM = 128,
    JSON_FLAG_USE_BIGINT = 256
};

manapi::error::status json_invalid_char (std::string_view n, std::size_t pos) {
    return manapi::error::status_invalid_argument("json: invalid char", {{"chr", n[pos]}, {"pos", pos}});
}

manapi::error::status json_unexpected_end (std::size_t pos) {
    return manapi::error::status_invalid_argument("json: unexpected end of JSON input at", {{"pos", pos}});
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json_builder::json_builder(const json_mask &mask, bool use_bigint, size_t bigint_precision)  {
    this->start_cut = 0;
    this->next_parent = nullptr;
    this->flags = 0;
    this->end_cut = 0;
    this->type = json::type_null;
    this->i = 0;
    if (use_bigint)
        this->flags |= JSON_FLAG_USE_BIGINT;
    this->bigint_precision = bigint_precision;
    this->current_types = mask.get_api_tree().is_null() ? nullptr : &mask.get_api_tree()["obj"];
    this->current_type = 0;

    this->action = JSON_CALLBACK_CHECK_TYPE;
}

manapi::json_builder::
json_builder(const json &mask, bool use_bigint, size_t bigint_precision) {
    this->start_cut = 0;
    this->type = json::type_null;
    this->end_cut = 0;
    this->flags = 0;
    this->next_parent = nullptr;
    this->i = 0;
    this->bigint_precision = bigint_precision;
    this->current_types = mask == nullptr ? nullptr : &mask;
    this->current_type = 0;

    this->action = JSON_CALLBACK_CHECK_TYPE;

    if (use_bigint)
        this->flags |= JSON_FLAG_USE_BIGINT;
}
#endif

manapi::json_builder::json_builder(const json_mask &mask) {
    this->start_cut = 0;
    this->end_cut = 0;
    this->type = json::type_null;
    this->i = 0;
    this->flags = 0;
    this->next_parent = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    this->bigint_precision = 128;
#endif
    this->current_types = mask.get_api_tree().is_null() ? nullptr : &mask.get_api_tree()["obj"];
    this->current_type = 0;

    this->action = JSON_CALLBACK_CHECK_TYPE;
}

manapi::json_builder::json_builder(const json &mask) {
    this->type = json::type_null;
    this->start_cut = 0;
    this->end_cut = 0;
    this->i = 0;
    this->flags = 0;
    this->next_parent = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    this->bigint_precision = 128;
#endif
    this->current_types = mask == nullptr ? nullptr : &mask;
    this->current_type = 0;

    this->action = JSON_CALLBACK_CHECK_TYPE;
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

manapi::error::status manapi::json_builder::parse(std::string_view str) {
    if (this->flags & JSON_FLAG_GETTING)
        this->flags ^= JSON_FLAG_GETTING;
    size_t j = 0;
    return this->_parse(str, j);
}

manapi::error::status manapi::json_builder::parse(char c) {
    return this->parse(std::string_view(&c, 1));
}

manapi::error::status_or<manapi::json> manapi::json_builder::get() {
    this->flags |= JSON_FLAG_GETTING;
    if (!(this->flags & JSON_FLAG_READY))
    {
        size_t j = 0;
        auto res = this->call_action_({}, j);
        if (!res.ok())
            return std::move(res);
    }
    if (this->object.is_null()) {
        // maybe no content provided
        // TODO: think
        if (!_check_eq_type())
            return error::status_invalid_argument("json_mask: types not match", jm_msg_with_path_and_param(this->path.get(),
                json_type_to_str(this->type)));
    }
    this->_reset ();
    return std::move(this->object);
}

bool manapi::json_builder::is_ready() const {
    return this->flags & JSON_FLAG_READY;
}

bool manapi::json_builder::is_empty() const {
    return this->i == 0 && this->type == json::type_null;
}

void manapi::json_builder::clear() {
    this->_reset();
}

manapi::error::status manapi::json_builder::_parse(std::string_view plain_text, size_t &j, bool root) {
    this->end_cut = plain_text.size() + this->i;

    this->i += j;

    while (i < this->end_cut) {
        // yo
        auto res = this->call_action_ (plain_text, j);
        if (!res.ok())
            return std::move(res);
    }

    if (root)
    {
        auto res = _check_end (plain_text, j);
        if (!res.ok())
            return std::move(res);
    }

    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_type(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); this->i++, j++)
    {
        const unsigned char &c = plain_text.at(j);

        // skip \t \n \s and etc
        if (unicode::is_space_symbol(c))
        {
            continue;
        }

        switch (c)
        {
            case '{':
                this->type = json::type_object;
                this->object = json::object();
                this->action = JSON_CALLBACK_BUILD_OBJECT;
                goto finish;
            case '[':
                this->type = json::type_array;
                this->object = json::array();
                this->action = JSON_CALLBACK_BUILD_ARRAY;
                goto finish;
            case '"':
                this->type = json::type_string;
                this->action = JSON_CALLBACL_BUILD_STRING;
                goto finish;
            default:
                this->type = json::type_number;
                this->action = JSON_CALLBACK_BUILD_NUMERIC;
                goto finish;
        }
    }

    finish:

    if (j != plain_text.size())
    {
        // type was found
        this->start_cut = this->i;

        if (!_check_eq_type ())
            return error::status_invalid_argument("json_mask: type not equals",
                jm_msg_with_path_and_param(this->path.get(), json_type_to_str(this->type)));
    }

    return error::status_ok();
}

manapi::error::status manapi::json_builder::_build_string(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        auto res = _check_max_mean(true);
        if (!res.ok())
            return std::move(res);

        unsigned char c = plain_text.at(j);
        if (_valid_utf_char(plain_text, j, this->wchar_left))
        {
            // nothing
        }
        else
        {
            if (this->flags & JSON_FLAG_ESCAPED)
            {
                this->flags ^= JSON_FLAG_ESCAPED;

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
                        this->utf_escaped_status = 0;
                        continue;
                    break;
                    case '\\':
                        break;
                    case '/':
                        break;
                    case '"':
                        break;
                    default:
                        return error::status_invalid_argument("json: bad escaped character", {{"pos", this->i}});
                }
            }
            else
            {
                if (this->utf_escaped_status != -1) {
                    // we grab ascii chars for utf char
                    c = std::tolower(c);
                    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                        // U+0000 - U+007F -> 0yyyzzzz
                        // U+0080 - U+07FF -> 110xxxyy10yyzzzz
                        // U+0800 - U+FFFF -> 1110wwww10xxxxyy10yyzzzz
                        // U+010000 - U+10FFFF -> 11110uvv10vvwwww10xxxxyy10yyzzzz
                        this->utf_escaped[this->utf_escaped_status++] = unicode::hex2dec(c);

                        if (this->utf_escaped_status == sizeof (this->utf_escaped)) {

                            if (this->utf_escaped[0] == 0 && this->utf_escaped[1] <= 7) {
                                if (this->utf_escaped[1] == 0 && this->utf_escaped[2] <= 7) {
                                    // 0yyy zzzz
                                    this->buffer.push_back(static_cast<char> (this->utf_escaped[2] << 4 | this->utf_escaped[3]));
                                }
                                else {
                                    // 110x xxyy 10yy zzzz
                                    // 1100 0000 <- 192 (128 + 64)
                                    // 100000000 <- 128
                                    this->buffer.push_back(static_cast<char> (static_cast<unsigned char>(192) | (this->utf_escaped[1] << 2) | (this->utf_escaped[2] >> 2)));
                                    this->buffer.push_back(static_cast<char> (static_cast<unsigned char>(128) | (static_cast<unsigned char> (this->utf_escaped[2] << 6) >> 2) | this->utf_escaped[3]));
                                }
                            }
                            else {
                                // 1110 wwww 10xx xxyy 10yy zzzz
                                // 1110 0000 <- 224 (128 + 64 + 32)
                                // 1000 0000 <- 128
                                this->buffer.push_back(static_cast<char> (static_cast<unsigned char> (224) | (this->utf_escaped[0])));
                                this->buffer.push_back(static_cast<char> (static_cast<unsigned char> (128) | (this->utf_escaped[1] << 2) | (this->utf_escaped[2] >> 2)));
                                this->buffer.push_back(static_cast<char> (static_cast<unsigned char> (128) | (static_cast<unsigned char> (this->utf_escaped[2] << 6) >> 2) | this->utf_escaped[3]));
                            }

                            this->utf_escaped_status = -1;
                        }
                        continue;
                    }

                    // error
                    return error::status_invalid_argument ("json: bad unicode escape", {{"pos", this->i}});
                }

                switch (c) {
                    case '\t':
                    case '\n':
                    case '\r':
                    case '\f':
                    case '\b':
                        return error::status_invalid_argument("json: bad control character", {{"pos", this->i}});
                }

                if (c == '\\')
                {

                    this->flags |= JSON_FLAG_ESCAPED;
                    continue;
                }

                if (c == '"')
                {
                    this->flags ^= JSON_FLAG_OPENED_QUOTE;

                    if (!(this->flags & JSON_FLAG_OPENED_QUOTE)) {
                        goto finish;
                    }

                    continue;
                }
            }
        }


        if (this->flags & JSON_FLAG_OPENED_QUOTE)
        {
            this->buffer.push_back(static_cast<char> (c));
        }
        else {
            if (!unicode::is_space_symbol(c))
            {
                return json_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (plain_text.size() != j || (this->flags & JSON_FLAG_GETTING))
    {
        if (this->flags & JSON_FLAG_OPENED_QUOTE)
        {
            return json_unexpected_end(j);
        }

        // closed string
        i++;
        j++;

        this->end_cut = this->i;
        this->object = this->buffer;
        auto res = _check_string();
        if (!res.ok())
            return std::move(res);
        // clean up only after passing the checks
        this->buffer.clear();

        this->flags |= JSON_FLAG_READY;
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_build_numeric(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, this->i++)
    {
        unsigned char c = plain_text[j];

        if (this->buffer.empty())
        {
            this->flags |= JSON_FLAG_OPERATE_ALREADY;

            if (c == '-' || c == '+') {
                this->buffer += static_cast<char> (c);
                continue;
            }
        }

        if (c >= '0' && c <= '9')
        {
            if (this->type == json::type_number) { this->type = json::type_integer; }
            if (this->buffer.size() == 1 && this->buffer[0] == '0') {
                // it can't be
                return json_invalid_char(plain_text, j);
            }
            this->buffer += static_cast<char> (c);
        }
        else {
            if (this->buffer.empty())
            {
                // its true or false or null
                this->action = JSON_CALLBACK_BUILD_NUMERIC_STRING;

                return error::status_ok();
            }

            if (this->buffer.size() == 1 && this->buffer[0] == '-')
            {
                // we need at least 1 digit
                return json_invalid_char(plain_text, j);
            }

            if (unicode::is_space_symbol(c)) {
                goto finish;
            }

            switch (c) {
                case '-':
                case '+':
                    if (this->flags & JSON_FLAG_OPERATE_ALREADY) {
                        return json_invalid_char(plain_text, j);
                    }
                    this->flags |= JSON_FLAG_OPERATE_ALREADY;
                    this->buffer += static_cast<char> (c);
                break;
                case 'e':
                case 'E':
                    // exp
                    if (this->flags & JSON_FLAG_EXP_ALREADY) {
                        return json_invalid_char(plain_text, j);
                    }
                    if (this->type != json::type_decimal) {
                        this->type = json::type_decimal;
                    }
                    this->buffer += static_cast<char> (c);
                    if (this->flags & JSON_FLAG_OPERATE_ALREADY)
                        this->flags ^= JSON_FLAG_OPERATE_ALREADY;
                break;
                case '.':
                    if (this->type == json::type_decimal)
                    {
                        return json_invalid_char(plain_text, j);
                    }

                    // its decimal
                    this->type = json::type_decimal;
                    this->buffer += static_cast<char> (c);
                    break;
                case '}':
                case ']':
                case ',':
                    goto finish;
                default:
                    return json_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (j != plain_text.size() || (this->flags & JSON_FLAG_GETTING))
    {
        // bcz we didnt consider this chars ('}', ',') in this type
        this->end_cut = this->i;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        // finish
        if (this->flags & JSON_FLAG_USE_BIGINT)
        {
            this->object = json (bigint(this->buffer, this->bigint_precision));
        }
        else
#endif
        {

            if (this->type == json::type_decimal)
            {
                std::stringstream stream (std::move(this->buffer));
                json::DECIMAL d;
                stream >> d;
                this->object = json (d);
            }
            else if (this->type == json::type_integer)
            {
                bool have_sign = this->buffer.size() && this->buffer[0] == '-';
                std::stringstream stream (std::move(this->buffer));

                if (have_sign) {
                    json::INTEGER n;
                    stream >> n;
                    this->object = json(static_cast<json::INTEGER> (n));
                }
                else {
                    /* loss of the number of values */
                    std::size_t un;
                    stream >> un;
                    this->object = json(static_cast<json::INTEGER> (un));
                }
            }
        }

        auto res = _check_numeric();

        if (!res.ok())
            return std::move(res);

        // clean up only after passing the checks
        this->buffer.clear();

        this->flags |= JSON_FLAG_READY;
    }

    return error::status_ok();
}

manapi::error::status manapi::json_builder::_build_numeric_string(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); this->i++, j++)
    {
        unsigned char c = plain_text[j];

        if (unicode::is_space_symbol(c) || c == '}' ||
            c == ',' || c == ']') {
            goto finish;
        }

        this->buffer += static_cast<char> (c);
    }

    finish:
    if (j != plain_text.size() || (this->flags & JSON_FLAG_GETTING))
    {
        this->end_cut = i;

        // check if this is true, false, null ->

        const std::string_view value = std::move(this->buffer);

        this->flags |= JSON_FLAG_READY;

        // true
        if (value == "true") {
            this->type = json::type_boolean;
            this->object = json (true);
            return _check_numeric();
        }

        // false
        if (value == "false") {
            this->type = json::type_boolean;
            this->object = json (false);
            return _check_numeric();
        }

        // null
        if (value == "null") {
            this->type = json::type_null;
            this->object = json(nullptr);
            return _check_numeric();
        }

        // TODO escape sub_plain_text
        return error::status_invalid_argument("json: invalid string", {{"data", value}});
        // throw json_parse_exception(ERR_JSON_INVALID_STRING, std::format("Invalid string: '{}' ({}, {})",
        //                                        value, this->start_cut + 1,
        //                                        this->end_cut));
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_build_object(std::string_view plain_text, size_t &j) {
    doit:
    if (this->item != nullptr)
    {
        this->i -= j;
        auto res = this->item->_parse(plain_text, j, false);
        if (!res.ok())
            return std::move(res);

        this->i = this->i + j;

        if (this->item->flags & JSON_FLAG_READY)
        {
            auto rhs = this->item->get();
            if (!rhs.ok())
                return std::move(rhs.err());

            json it = rhs.unwrap();

            this->item = nullptr;

            if (!(this->flags & JSON_FLAG_IS_NOT_KEY))
            {
                this->key = it.as_string();
            }
            else
            {
                this->object.insert(this->key, std::move(it));
            }

            this->flags |= JSON_FLAG_GO_TO_DELIM;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (unicode::is_space_symbol(c))
        {
            continue;
        }

        if (this->flags & JSON_FLAG_GO_TO_DELIM)
        {
            if (unicode::is_space_symbol(c))
            {
                continue;
            }

            if ((!(this->flags & JSON_FLAG_IS_NOT_KEY) && c == ':') || ((this->flags & JSON_FLAG_IS_NOT_KEY) && c == ','))
            {
                this->flags ^= JSON_FLAG_IS_NOT_KEY;
                if (this->flags & JSON_FLAG_GO_TO_DELIM)
                    this->flags ^= JSON_FLAG_GO_TO_DELIM;
                continue;
            }

            if (c == '}') {
                if(this->flags & JSON_FLAG_OPENED_QUOTE)
                    this->flags ^= JSON_FLAG_OPENED_QUOTE;
                // the end
                goto finish;
            }


            return json_invalid_char(plain_text, j);
        }

        if (this->flags & JSON_FLAG_OPENED_QUOTE)
        {
            if (c == '}')
            {
                this->flags ^= JSON_FLAG_OPENED_QUOTE;
                goto finish;
            }

            if (!(this->flags & JSON_FLAG_IS_NOT_KEY) || this->current_types == nullptr)
            {
                this->item = std::make_unique<json_builder> (json(nullptr)
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                    ,this->flags & JSON_FLAG_USE_BIGINT, this->bigint_precision
#endif
                );
            }
            else
            {
                this->item = std::make_unique<json_builder>(get_current_type()["value"][key]["obj"]
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                    , this->flags & JSON_FLAG_USE_BIGINT, this->bigint_precision
#endif
                    );
                this->item->next_parent = this;
            }
            goto doit;
        }

        if (c == '{')
        {
            this->flags |= JSON_FLAG_OPENED_QUOTE;
            continue;
        }

        return json_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || (this->flags & JSON_FLAG_GETTING))
    {
        // is_key = true when {..., ...',' <- we are expecting a key}, false otherwise
        if ((this->flags & JSON_FLAG_OPENED_QUOTE) || (!(this->flags & JSON_FLAG_IS_NOT_KEY) && this->object.size() > 0))
        {
            return json_unexpected_end(i);
        }

        this->i++;
        j++;

        this->end_cut = this->i;
        if (this->flags & JSON_FLAG_IS_NOT_KEY)
            this->flags ^= JSON_FLAG_IS_NOT_KEY;
        this->key.clear();

        auto res = this->_check_object();
        if (!res.ok())
            return std::move(res);

        this->flags |= JSON_FLAG_READY;
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_build_array(std::string_view plain_text, size_t &j) {
    doit:
    if (this->item != nullptr)
    {
        this->i -= j;
        auto res = this->item->_parse(plain_text, j, false);
        if (!res.ok())
            return std::move(res);
        this->i = this->i + j;

        if (this->item->flags & JSON_FLAG_READY)
        {
            auto rhs = this->item->get();
            if (!rhs.ok())
                return std::move(rhs.err());
            json it = rhs.unwrap();
            this->item = nullptr;

            this->object.push_back(std::move(it));

            this->flags |= JSON_FLAG_GO_TO_DELIM;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (unicode::is_space_symbol(c))
        {
            continue;
        }

        if (this->flags & JSON_FLAG_GO_TO_DELIM)
        {
            if (unicode::is_space_symbol(c))
            {
                continue;
            }

            if (c == ',')
            {
                this->flags ^= JSON_FLAG_GO_TO_DELIM;
                continue;
            }

            if (c == ']') {
                if (this->flags & JSON_FLAG_OPENED_QUOTE)
                    this->flags ^= JSON_FLAG_OPENED_QUOTE;
                // the end
                goto finish;
            }


            return json_invalid_char(plain_text, j);
        }

        if (this->flags & JSON_FLAG_OPENED_QUOTE)
        {
            if (c == ']')
            {
                this->flags ^= JSON_FLAG_OPENED_QUOTE;
                goto finish;
            }

            // if exists default (TYPE(...)[...]) or exists ({..., ..., ...})

            if (this->current_types == nullptr)
            {
                this->item = std::make_unique<json_builder>(json(nullptr)
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                    ,this->flags & JSON_FLAG_USE_BIGINT, this->bigint_precision
#endif
                    );
            }
            else
            {
                const auto &current = get_current_type();

                if (current.contains("default"))
                {
                    this->item = std::make_unique<json_builder>(get_current_type()["default"]
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                    , this->flags & JSON_FLAG_USE_BIGINT, this->bigint_precision
#endif
                    );
                }
                else
                {
                    this->item = std::make_unique<json_builder>(get_current_type()["value"][this->element_index]["obj"]
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                        , this->flags & JSON_FLAG_USE_BIGINT, this->bigint_precision
#endif
                        );
                }
            }
            this->element_index++;
            goto doit;
        }

        if (c == '[')
        {
            this->flags |= JSON_FLAG_OPENED_QUOTE;
            continue;
        }

       return json_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || (this->flags & JSON_FLAG_GETTING))
    {
        if (this->flags & JSON_FLAG_OPENED_QUOTE)
        {
            return json_unexpected_end(this->i);
        }

        this->i++;
        j++;

        this->end_cut = this->i;
        auto res = _check_array();
        if (!res.ok())
            return std::move(res);

        this->flags |= JSON_FLAG_READY;
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_end(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        if (!unicode::is_space_symbol(plain_text[j]))
        {
            return json_invalid_char(plain_text, j);
            //json::error_invalid_char(plain_text, j);
        }
    }
    return error::status_ok();
}

void manapi::json_builder::_reset_type() {
    this->max_mean_size = nullptr;
    this->min_mean_size = nullptr;
}

bool manapi::json_builder::_next_type() {
    this->current_type++;

    if ((this->current_types && this->current_types->is_array() && this->current_type < this->current_types->size())
        || _next_parent())
    {
        return _check_eq_type();
    }

    return false;
    //throw json_parse_exception(ERR_JSON_MASK_VERIFY_FAILED, "json_mask error");
}

bool manapi::json_builder::_next_parent() {
    if (this->next_parent) {
        auto res = json_builder::next_parent_cb(this->next_parent);
        if (!res.ok())
            return false;
        this->current_types = res.unwrap();
        this->current_type = 0;
        return true;
    }
    return false;
}

bool manapi::json_builder::_check_eq_type() {
    this->_reset_type();

    if (!this->current_types)
    {
        return true;
    }
    auto &current = get_current_type();
    auto &current_type = current["type"].as_integer();
    if (current.contains("max_mean")) { max_mean_size = &current["max_mean"]; }
    if (current.contains("min_mean")) { min_mean_size = &current["min_mean"]; }

    if (current_type == -1)
    {
        // this solve is better
        this->current_types = nullptr;
        return true;
    }

    if (this->type == json::type_number)
    {
        if (
            current_type == json::type_decimal ||
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            current_type == json::type_bigint ||
#endif
            current_type == json::type_integer ||
            current_type == json::type_boolean ||
            current_type == json::type_null)
        {
            return true;
        }
    }
    else {
        if (current_type == type)
        {
            return true;
        }
    }

    if (!this->_next_type())
        return false;
    if (!this->_check_eq_type())
        return false;
    return true;
}

manapi::error::status manapi::json_builder::_check_max_mean(bool building) {
    if (!this->max_mean_size)
        goto ok;

    if (building)
    {
        switch (type)
        {
            case json::type_string:
                if (json_verify_max_mean(*max_mean_size, buffer.size())) { goto ok; }
            default:
                goto ok;
        }
    }
    else
    {
        switch (this->type)
        {
            case json::type_string:
                if (json_verify_max_mean(*this->max_mean_size, this->object.size())) { goto ok; }
            break;
            case json::type_integer:
                if (json_verify_max_mean(*this->max_mean_size, this->object.as_integer())) { goto ok; }
            break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case json::type_bigint:
                if (json_verify_max_mean(*this->max_mean_size, this->object.as_bigint())) { goto ok; }
            break;
#endif
            case json::type_decimal:
                if (json_verify_max_mean(*this->max_mean_size, this->object.as_decimal())) { goto ok; }
            break;
            case json::type_array:
            case json::type_object:
                if (json_verify_max_mean(*this->max_mean_size, this->object.size())) { goto ok; }
            break;
            default:
                goto ok;
        }
    }
    return error::status_invalid_argument("json_mask: value is greater or equals max_mean", jm_msg_with_path_and_param(this->path.get(), *this->max_mean_size));
    ok: return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_min_mean() {
    if (!this->min_mean_size)
        goto ok;

    switch (this->type)
    {
        case json::type_string:
            if (json_verify_min_mean(*this->min_mean_size, this->object.size())) { goto ok; }
        break;
        case json::type_integer:
            if (json_verify_min_mean(*this->min_mean_size, this->object.as_integer())) { goto ok; }
        break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case json::type_bigint:
            if (json_verify_min_mean(*this->min_mean_size, this->object.as_bigint())) { goto ok; }
        break;
#endif
        case json::type_decimal:
            if (json_verify_min_mean(*this->min_mean_size, this->object.as_decimal())) { goto ok; }
        break;
        case json::type_array:
        case json::type_object:
            if (json_verify_min_mean(*this->min_mean_size, this->object.size())) { goto ok; }
        break;
        default:
            goto ok;
    }

    return error::status_invalid_argument("json_mask: value is lower or equals min_mean", jm_msg_with_path_and_param(this->path.get(), *this->min_mean_size));
    ok: return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_type_none_complex_value() {
    auto &current = get_current_type();
    auto fit = current.find("value");
    if (fit == current.end<json::OBJECT>())
    {
        goto ok;
    }
    {
        auto &value = current["value"];
        if (value.is_array() || value.is_object())
        {
            goto ok;
        }
        if ((value == this->object)) {
            goto ok;
        }

        return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(this->path.get(), this->object));
    }
    ok:
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_default() {
    if (this->current_types) {
        auto res = this->_check_max_mean();
        if (!res.ok())
            goto err;
        res = this->_check_min_mean();
        if (!res.ok())
            goto err;
        res = this->_check_type_none_complex_value();
        if (!res.ok())
            goto err;
        res = this->_check_meta_value();
        if (!res.ok())
            goto err;

        goto ok;
        err:
        if (this->_next_type())
            return error::status_failed_precondition("json_mask: again");

        return std::move(res);
    }

    ok: return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_meta_value() {
    const auto &current = get_current_type ();
    if (!(this->object.is_array() || this->object.is_object())) {
        auto fit = current.find("value");
        if (fit != current.end<manapi::json::OBJECT>()) {
            if (fit->second != this->object) {
                return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(this->path.get(), this->object));
            }
        }
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_string() {
    while (true)
    {
        auto res = this->_check_default();
        if (res.ok())
            break;

        if (res.code() == ERR_FAILED_PRECONDITION)
            continue;

        return std::move(res);
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_numeric() {
    while (true)
    {
        auto res = _check_default();
        if (res.ok())
            break;
        if (res.code() == ERR_FAILED_PRECONDITION)
            continue;
        return std::move(res);
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_object() {
    while (true)
    {
        auto res = _check_default();
        if (res.ok()) {
            if (!this->current_types)
                break;
            json_mask mask_child;
            mask_child.set_api_tree({{"obj", get_current_type()}, {"none", false}});
            res = mask_child.valid(this->object);
            if (res.ok())
                break;
        }
        if (res.code() == ERR_FAILED_PRECONDITION)
            continue;
        return std::move(res);
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_array() {
    while (true)
    {
        auto res = _check_default();
        if (res.ok())
            break;
        if (res.code() == ERR_FAILED_PRECONDITION)
            continue;
        return std::move(res);
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::_check_part_object() {
    while (true) {
        json_mask mask_child;
        mask_child.set_api_tree({{"obj", get_current_type()}, {"none", false}});
        mask_child.set_complete_status(false);
        auto res = mask_child.valid(this->object);
        if (res.ok()) {
            break;
        }
        if (!_next_type())
            return std::move(res);
    }
    return error::status_ok();
}

manapi::error::status manapi::json_builder::call_action_(const std::string_view &plain_text, size_t &j) {
    switch (this->action) {
        case JSON_CALLBACK_CHECK_TYPE:
            return this->_check_type((plain_text), j);
        case JSON_CALLBACL_BUILD_STRING:
            return this->_build_string((plain_text), j);
        case JSON_CALLBACK_BUILD_NUMERIC:
            return this->_build_numeric((plain_text), j);
        case JSON_CALLBACK_BUILD_NUMERIC_STRING:
            return this->_build_numeric_string((plain_text), j);
        case JSON_CALLBACK_BUILD_OBJECT:
            return this->_build_object((plain_text), j);
        case JSON_CALLBACK_BUILD_ARRAY:
            return this->_build_array((plain_text), j);
        case JSON_CALLBACK_CHECK_END:
            return this->_check_end((plain_text), j);
        default:
            return error::status_internal("unreachable");
    }
}

const manapi::json & manapi::json_builder::get_current_type() {
    if (this->current_types->is_array()) { return this->current_types->at(this->current_type); }
    return *this->current_types;
}

bool manapi::json_builder::_valid_utf_char(std::string_view plain_text, size_t i, size_t &left) {
    const unsigned char &c = plain_text[i];
    if (left > 0 || c > 127) {
        if (left == 0)
        {
            const size_t octet = manapi::unicode::count_of_octet(c);
            if (octet == 1) {
                // char cant be equal 10xxxxxx
                json::error_invalid_char(plain_text, i);
            }

            left = octet - 1;
        }
        else
        {
            // if c != 10xxxxxx
            if (manapi::unicode::count_of_octet(c) != 1)
            {
                json::error_invalid_char(plain_text, i);
            }

            left--;
        }
    }
    return false;
}

void manapi::json_builder::_valid_utf_string(std::string_view str) {
    size_t wchar_left = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        _valid_utf_char(str, i, wchar_left);
    }
}

manapi::error::status_or<const manapi::json *> manapi::json_builder::next_parent_cb(json_builder *p) {
    if (!p->_next_type())
        return error::status_invalid_argument("json_mask: again");

    p->_check_part_object();

    return &(p->get_current_type()["value"][p->key]["obj"]);
}

void manapi::json_builder::_reset() {
    // object can not be clean up here
    this->start_cut = 0;
    this->end_cut = 0;
    this->type = json::type_null;
    this->i = 0;
    this->item = nullptr;
    this->wchar_left = 0;
    this->utf_escaped_status = -1;
    this->key.clear();
    this->flags = 0;
    this->buffer.clear();

    this->_reset_type();

    this->action = JSON_CALLBACK_CHECK_TYPE;
}
