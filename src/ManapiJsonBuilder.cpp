#include <sstream>

#include "ManapiJsonBuilder.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiUtils.hpp"

manapi::json_builder::json_builder(const json_mask &mask, const bool &use_bigint, const size_t &bigint_precision)  {
    this->start_cut = 0;
    this->end_cut = 0;
    this->use_bigint = use_bigint;
    this->bigint_precision = bigint_precision;
    this->current_types = mask.get_api_tree().is_null() ? nullptr : &mask.get_api_tree()["obj"];
    this->current_type = 0;

    action = std::bind(&json_builder::_check_type, this, std::placeholders::_1, std::placeholders::_2);
}

manapi::json_builder::
json_builder(const json &mask, const bool &use_bigint, const size_t &bigint_precision) {
    this->start_cut = 0;
    this->end_cut = 0;
    this->use_bigint = use_bigint;
    this->bigint_precision = bigint_precision;
    this->current_types = mask == nullptr ? nullptr : &mask;
    this->current_type = 0;

    action = std::bind(&json_builder::_check_type, this, std::placeholders::_1, std::placeholders::_2);
}

manapi::json_builder::~json_builder() = default;

manapi::json_builder & manapi::json_builder::operator<<(const std::string_view &str) {
    if (getting) { getting = false; }
    size_t j = 0;
    _parse(str, j);
    return *this;
}

manapi::json_builder & manapi::json_builder::operator<<(const char &c) {
    return this->operator<<(std::string_view(&c, 1));
}

manapi::json manapi::json_builder::get() {
    getting = true;
    if (!ready)
    {
        size_t j = 0;
        action("", j);
    }
    if (object.is_null()) {
        // maybe no content provided
        // TODO: think
        _check_eq_type();
    }
    _reset ();
    return std::move(object);
}

const bool & manapi::json_builder::is_ready() const {
    return ready;
}

bool manapi::json_builder::is_empty() const {
    return i == 0 && type == json::type_null;
}

void manapi::json_builder::clear() {
    _reset();
}

void manapi::json_builder::_parse(const std::string_view &plain_text, size_t &j, bool root) {

    this->use_bigint = use_bigint;
    this->bigint_precision = bigint_precision;

    end_cut = plain_text.size() + i;

    i += j;

    while (i < end_cut) {
        // yo
        action (plain_text, j);
    }

    if (root)
    {
        _check_end (plain_text, j);
    }
}

void manapi::json_builder::_check_type(const std::string_view &plain_text, size_t &j) {
    for (; j < plain_text.size(); i++, j++)
    {
        const unsigned char &c = plain_text.at(j);

        // skip \t \n \s and etc
        if (net::utils::is_space_symbol(c))
        {
            continue;
        }

        switch (c)
        {
            case '{':
                type = json::type_object;
                object = json::object();
                action = std::bind(&json_builder::_build_object, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            case '[':
                type = json::type_array;
                object = json::array();
                action = std::bind(&json_builder::_build_array, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            case '"':
                type = json::type_string;
                action = std::bind(&json_builder::_build_string, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            default:
                type = json::type_numeric;
                action = std::bind(&json_builder::_build_numeric, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
        }
    }

    finish:

    if (j != plain_text.size())
    {
        // type was found
        start_cut = i;

        _check_eq_type ();
    }
}

void manapi::json_builder::_build_string(const std::string_view &plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        _check_max_mean(true);

        unsigned char c = plain_text.at(j);
        if (_valid_utf_char(plain_text, j, wchar_left))
        {
            // nothing
        }
        else
        {
            if (escaped)
            {
                escaped = false;

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
                        utf_escaped_status = 0;
                        continue;
                    break;
                    case '\\':
                        break;
                    case '/':
                        break;
                    case '"':
                        break;
                    default:
                        throw json_parse_exception(ERR_JSON_BAD_ESCAPED_CHAR, "Bad escaped character at " + std::to_string(i));
                }
            }
            else
            {
                if (utf_escaped_status != -1) {
                    // we grab ascii chars for utf char
                    c = std::tolower(c);
                    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                        // U+0000 - U+007F -> 0yyyzzzz
                        // U+0080 - U+07FF -> 110xxxyy10yyzzzz
                        // U+0800 - U+FFFF -> 1110wwww10xxxxyy10yyzzzz
                        // U+010000 - U+10FFFF -> 11110uvv10vvwwww10xxxxyy10yyzzzz
                        utf_escaped[utf_escaped_status++] = net::utils::hex2dec(c);

                        if (utf_escaped_status == sizeof (utf_escaped)) {

                            if (utf_escaped[0] == 0 && utf_escaped[1] <= 7) {
                                if (utf_escaped[1] == 0 && utf_escaped[2] <= 7) {
                                    // 0yyy zzzz
                                    buffer.push_back(static_cast<char> (utf_escaped[2] << 4 | utf_escaped[3]));
                                }
                                else {
                                    // 110x xxyy 10yy zzzz
                                    // 1100 0000 <- 192 (128 + 64)
                                    // 100000000 <- 128
                                    buffer.push_back(static_cast<char> (static_cast<unsigned char>(192) | (utf_escaped[1] << 2) | (utf_escaped[2] >> 2)));
                                    buffer.push_back(static_cast<char> (static_cast<unsigned char>(128) | (static_cast<unsigned char> (utf_escaped[2] << 6) >> 2) | utf_escaped[3]));
                                }
                            }
                            else {
                                // 1110 wwww 10xx xxyy 10yy zzzz
                                // 1110 0000 <- 224 (128 + 64 + 32)
                                // 1000 0000 <- 128
                                buffer.push_back(static_cast<char> (static_cast<unsigned char> (224) | (utf_escaped[0])));
                                buffer.push_back(static_cast<char> (static_cast<unsigned char> (128) | (utf_escaped[1] << 2) | (utf_escaped[2] >> 2)));
                                buffer.push_back(static_cast<char> (static_cast<unsigned char> (128) | (static_cast<unsigned char> (utf_escaped[2] << 6) >> 2) | utf_escaped[3]));
                            }

                            utf_escaped_status = -1;
                        }
                        continue;
                    }

                    // error
                    throw json_parse_exception (ERR_JSON_BAD_ESCAPED_CHAR, "bad Unicode escape at " + std::to_string(i));
                }

                switch (c) {
                    case '\t':
                    case '\n':
                    case '\r':
                    case '\f':
                    case '\b':
                        throw json_parse_exception(ERR_JSON_INVALID_CHAR, "Bad control character at " + std::to_string(i));
                }

                if (c == '\\')
                {

                    escaped = true;
                    continue;
                }

                if (c == '"')
                {
                    opened_quote = !opened_quote;

                    if (!opened_quote) {
                        goto finish;
                    }

                    continue;
                }
            }
        }


        if (opened_quote)
        {
            buffer.push_back(static_cast<char> (c));
        }
        else {
            if (!net::utils::is_space_symbol(c))
            {
                json::error_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (plain_text.size() != j || getting)
    {
        if (opened_quote)
        {
            json::error_unexpected_end(j);
        }

        // closed string
        i++;
        j++;

        end_cut = i;
        object = buffer;
        _check_string();

        // clean up only after passing the checks
        buffer.clear();

        ready = true;
    }
}

void manapi::json_builder::_build_numeric(const std::string_view &plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (buffer.empty())
        {
            operate_already = true;

            if (c == '-' || c == '+') {
                buffer += static_cast<char> (c);
                continue;
            }
        }

        if (c >= '0' && c <= '9')
        {
            if (type == json::type_numeric) { type = json::type_number; }
            if (buffer.size() == 1 && buffer[0] == '0') {
                // it can't be
                json::error_invalid_char(plain_text, j);
            }
            buffer += static_cast<char> (c);
        }
        else {
            if (buffer.empty())
            {
                // its true or false or null
                action = std::bind(&json_builder::_build_numeric_string, this, std::placeholders::_1, std::placeholders::_2);

                return;
            }

            if (buffer.size() == 1 && buffer[0] == '-')
            {
                // we need at least 1 digit
                json::error_invalid_char(plain_text, j);
            }

            if (net::utils::is_space_symbol(c)) {
                goto finish;
            }

            switch (c) {
                case '-':
                case '+':
                    if (operate_already) {
                        json::error_invalid_char(plain_text, j);
                    }
                    operate_already = true;
                    buffer += static_cast<char> (c);
                break;
                case 'e':
                case 'E':
                    // exp
                    if (exp_already) {
                        json::error_invalid_char(plain_text, j);
                    }
                    if (type != json::type_decimal) {
                        type = json::type_decimal;
                    }
                    buffer += static_cast<char> (c);
                    operate_already = false;
                break;
                case '.':
                    if (type == json::type_decimal)
                    {
                        json::error_invalid_char(plain_text, j);
                    }

                    // its decimal
                    type = json::type_decimal;
                    buffer += static_cast<char> (c);
                    break;
                case '}':
                case ']':
                case ',':
                    goto finish;
                default:
                    json::error_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (j != plain_text.size() || getting)
    {
        // bcz we didnt consider this chars ('}', ',') in this type
        end_cut = i;

        // finish
        if (use_bigint)
        {
            object.parse(bigint(buffer));
        }
        else
        {
            std::stringstream stream (buffer);

            if (type == json::type_decimal)
            {
                json::DECIMAL d;
                stream >> d;
                object.parse(d);
            }
            else if (type == json::type_number)
            {
                json::NUMBER n;
                stream >> n;
                object.parse(static_cast<json::NUMBER> (n));
            }
        }

        _check_numeric();

        // clean up only after passing the checks
        buffer.clear();

        ready = true;
    }
}

void manapi::json_builder::_build_numeric_string(const std::string_view &plain_text, size_t &j) {
    for (; j < plain_text.size(); i++, j++)
    {
        unsigned char c = plain_text[j];

        if (net::utils::is_space_symbol(c) || c == '}' ||
            c == ',' || c == ']') {
            goto finish;
        }

        buffer += static_cast<char> (c);
    }

    finish:
    if (j != plain_text.size() || getting)
    {
        end_cut = i;

        // check if this is true, false, null ->

        const std::string_view value = std::move(buffer);

        ready = true;

        // true
        if (value == "true") {
            type = json::type_boolean;
            object.parse (true);
            _check_numeric();
            return;
        }

        // false
        if (value == "false") {
            type = json::type_boolean;
            object.parse (false);
            _check_numeric();
            return;
        }

        // null
        if (value == "null") {
            type = json::type_null;
            object.parse (nullptr);
            _check_numeric();
            return;
        }

        // TODO escape sub_plain_text
        throw json_parse_exception(ERR_JSON_INVALID_STRING, std::format("Invalid string: '{}' ({}, {})",
                                               value, start_cut + 1,
                                               end_cut));
    }
}

void manapi::json_builder::_build_object(const std::string_view &plain_text, size_t &j) {
    doit:
    if (item != nullptr)
    {
        i -= j;
        item->_parse(plain_text, j, false);
        i = i + j;

        if (item->ready)
        {
            json it = item->get();
            item = nullptr;

            if (is_key)
            {
                key = it.as_string();
            }
            else
            {
                object.insert(key, std::move(it));
            }

            go_to_delimiter = true;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (net::utils::is_space_symbol(c))
        {
            continue;
        }

        if (go_to_delimiter)
        {
            if (net::utils::is_space_symbol(c))
            {
                continue;
            }

            if ((is_key && c == ':') || (!is_key && c == ','))
            {
                is_key = !is_key;
                go_to_delimiter = false;
                continue;
            }

            if (c == '}') {
                opened_quote = false;
                // the end
                goto finish;
            }


            json::error_invalid_char(plain_text, j);
        }

        if (opened_quote)
        {
            if (c == '}')
            {
                opened_quote = false;
                goto finish;
            }

            if (is_key || current_types == nullptr)
            {
                item = std::make_unique<json_builder> (json(nullptr), use_bigint, bigint_precision);
            }
            else
            {
                item = std::make_unique<json_builder>(get_current_type()["data"][key]["obj"], use_bigint, bigint_precision);
            }
            goto doit;
        }

        if (c == '{')
        {
            opened_quote = true;
            continue;
        }

        json::error_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || getting)
    {
        // is_key = true when {..., ...',' <- we are expecting a key}, false otherwise
        if (opened_quote || (is_key && object.size() > 0))
        {
            json::error_unexpected_end(i);
        }

        i++;
        j++;

        end_cut = i;
        is_key = true;
        key.clear();

        _check_object();

        ready = true;
    }
}

void manapi::json_builder::_build_array(const std::string_view &plain_text, size_t &j) {
    doit:
    if (item != nullptr)
    {
        i -= j;
        item->_parse(plain_text, j, false);
        i = i + j;

        if (item->ready)
        {
            json it = item->get();
            item = nullptr;

            object.push_back(std::move(it));

            go_to_delimiter = true;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (net::utils::is_space_symbol(c))
        {
            continue;
        }

        if (go_to_delimiter)
        {
            if (net::utils::is_space_symbol(c))
            {
                continue;
            }

            if (c == ',')
            {
                go_to_delimiter = false;
                continue;
            }

            if (c == ']') {
                opened_quote = false;
                // the end
                goto finish;
            }


            json::error_invalid_char(plain_text, j);
        }

        if (opened_quote)
        {
            if (c == ']')
            {
                opened_quote = false;
                goto finish;
            }

            // if exists default (TYPE(...)[...]) or exists ({..., ..., ...})

            if (current_types == nullptr)
            {
                item = std::make_unique<json_builder>(json(nullptr), use_bigint, bigint_precision);
            }
            else
            {
                const auto &current = get_current_type();

                if (current.contains("default"))
                {
                    item = std::make_unique<json_builder>(get_current_type()["default"], use_bigint, bigint_precision);
                }
                else
                {
                    item = std::make_unique<json_builder>(get_current_type()["data"][element_index]["obj"], use_bigint, bigint_precision);
                }
            }
            element_index++;
            goto doit;
        }

        if (c == '[')
        {
            opened_quote = true;
            continue;
        }

        json::error_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || getting)
    {
        if (opened_quote)
        {
            json::error_unexpected_end(i);
        }

        i++;
        j++;

        end_cut = i;
        _check_array();

        ready = true;
    }
}

void manapi::json_builder::_check_end(const std::string_view &plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        if (!net::utils::is_space_symbol(plain_text[j]))
        {
            json::error_invalid_char(plain_text, j);
        }
    }
}

void manapi::json_builder::_reset_type() {
    max_mean_size = nullptr;
    min_mean_size = nullptr;
}

void manapi::json_builder::_next_type() {
    current_type++;

    if (current_types != nullptr && current_types->is_array() && current_type < current_types->size())
    {
        _check_eq_type();
        return;
    }

    throw json_parse_exception(ERR_JSON_MASK_VERIFY_FAILED, "json_mask error");
}

void manapi::json_builder::_check_eq_type() {
    _reset_type();

    if (current_types == nullptr)
    {
        return;
    }
    auto &current = get_current_type();
    auto &current_type = current["type"].as_number();
    if (current.contains("max_mean")) { max_mean_size = &current["max_mean"].as_number(); }
    if (current.contains("min_mean")) { min_mean_size = &current["min_mean"].as_number(); }

    if (current_type == -1)
    {
        // this solve is better
        current_types = nullptr;
        return;
    }

    if (type == json::type_numeric)
    {
        if (
            current_type == json::type_decimal ||
            current_type == json::type_bigint ||
            current_type == json::type_number ||
            current_type == json::type_boolean ||
            current_type == json::type_null)
        {
            return;
        }
    }
    else {
        if (current_type == type)
        {
            return;
        }
    }

    _next_type();
    _check_eq_type();
}

bool manapi::json_builder::_check_max_mean(const bool &building) {
    if (max_mean_size == nullptr) { return true; }

    if (building)
    {
        switch (type)
        {
            case json::type_string:
                if (buffer.size() < *max_mean_size) { return true; }
            default:
                return true;
        }
    }
    else
    {
        switch (type)
        {
            case json::type_string:
                if (object.size() < *max_mean_size) { return true; }
            break;
            case json::type_number:
                if (object.as_number() < *max_mean_size) { return true; }
            break;
            case json::type_bigint:
                if (object.as_bigint() < *max_mean_size) { return true; }
            break;
            case json::type_decimal:
                if (object.as_decimal() < *max_mean_size) { return true; }
            break;
            case json::type_array:
            case json::type_object:
                if (object.size() < *max_mean_size) { return true; }
            break;
            default:
                return true;
        }
    }
    _next_type();
    return false;
}

bool manapi::json_builder::_check_min_mean() {
    if (min_mean_size == nullptr) { return true; }

    switch (type)
    {
        case json::type_string:
            if (*min_mean_size < 0 || object.size() > *min_mean_size) { return true; }
        break;
        case json::type_number:
            if (object.as_number() > *min_mean_size) { return true; }
        break;
        case json::type_bigint:
            if (object.as_bigint() > *min_mean_size) { return true; }
        break;
        case json::type_decimal:
            if (object.as_decimal() > *min_mean_size) { return true; }
        break;
        case json::type_array:
        case json::type_object:
            if (*min_mean_size < 0 || object.size() > *min_mean_size) { return true; }
        break;
        default:
            return true;
    }

    _next_type();
    return false;
}

bool manapi::json_builder::_check_type_none_complex_value() {
    auto &current = get_current_type();
    if (!current.contains("value"))
    {
        return true;
    }
    auto &value = current["value"];
    if (value.is_array() || value.is_object())
    {
        return true;
    }
    if ((current["value"] == object)) { return true; }

    _next_type();
    return false;
}

bool manapi::json_builder::_check_default() {
    return (current_types == nullptr) || (_check_max_mean() && _check_min_mean() && _check_type_none_complex_value() && _check_meta_value());
}

bool manapi::json_builder::_check_meta_value() {
    const auto &current = get_current_type ();
    if (!current.contains("value")) { return true; }

    if (current.at("value") == object) { return true; }
    _next_type();
    return false;
}

void manapi::json_builder::_check_string() {
    while (true)
    {
        if (_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_numeric() {
    while (true)
    {
        if (_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_object() {
    while (true)
    {
        if (_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_array() {
    while (true)
    {
        if (_check_default())
        {
            break;
        }
    }
}

const manapi::json & manapi::json_builder::get_current_type() {
    if (current_types->is_array()) { return current_types->at(current_type); }
    return *current_types;
}

bool manapi::json_builder::_valid_utf_char(const std::string_view &plain_text, const size_t &i, size_t &left) {
    const unsigned char &c = plain_text[i];
    if (left > 0 || c > 127) {
        if (left == 0)
        {
            const size_t octet = manapi::net::unicode::count_of_octet(c);
            if (octet == 1) {
                // char cant be equal 10xxxxxx
                json::error_invalid_char(plain_text, i);
            }

            left = octet - 1;
        }
        else
        {
            // if c != 10xxxxxx
            if (manapi::net::unicode::count_of_octet(c) != 1)
            {
                json::error_invalid_char(plain_text, i);
            }

            left--;
        }
    }
    return false;
}

void manapi::json_builder::_valid_utf_string(const std::string_view &str) {
    size_t wchar_left = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        _valid_utf_char(str, i, wchar_left);
    }
}

void manapi::json_builder::_reset() {
    // object can not be clean up here
    this->start_cut = 0;
    this->end_cut = 0;
    this->type = json::type_null;
    this->opened_quote = false;
    this->escaped = false;
    this->go_to_delimiter = false;
    this->getting = false;
    this->ready = false;
    this->i = 0;
    this->item = nullptr;
    this->wchar_left = 0;
    this->utf_escaped_status = -1;
    this->key.clear();
    this->exp_already = false;
    this->operate_already = false;

    _reset_type();

    action = std::bind(&json_builder::_check_type, this, std::placeholders::_1, std::placeholders::_2);
}
