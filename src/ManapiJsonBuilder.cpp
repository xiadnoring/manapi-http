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

    this->action = std::bind(&json_builder::_check_type, this, std::placeholders::_1, std::placeholders::_2);
}

manapi::json_builder::
json_builder(const json &mask, const bool &use_bigint, const size_t &bigint_precision) {
    this->start_cut = 0;
    this->end_cut = 0;
    this->use_bigint = use_bigint;
    this->bigint_precision = bigint_precision;
    this->current_types = mask == nullptr ? nullptr : &mask;
    this->current_type = 0;

    this->action = std::bind(&json_builder::_check_type, this, std::placeholders::_1, std::placeholders::_2);
}

manapi::json_builder::~json_builder() = default;

manapi::json_builder & manapi::json_builder::operator<<(std::string_view str) {
    if (this->getting) { this->getting = false; }
    size_t j = 0;
    _parse(str, j);
    return *this;
}

manapi::json_builder & manapi::json_builder::operator<<(const char &c) {
    return this->operator<<(std::string_view(&c, 1));
}

manapi::json manapi::json_builder::get() {
    this->getting = true;
    if (!this->ready)
    {
        size_t j = 0;
        this->action("", j);
    }
    if (this->object.is_null()) {
        // maybe no content provided
        // TODO: think
        _check_eq_type();
    }
    _reset ();
    return std::move(this->object);
}

const bool & manapi::json_builder::is_ready() const {
    return this->ready;
}

bool manapi::json_builder::is_empty() const {
    return this->i == 0 && this->type == json::type_null;
}

void manapi::json_builder::clear() {
    _reset();
}

void manapi::json_builder::_parse(std::string_view plain_text, size_t &j, bool root) {

    this->use_bigint = this->use_bigint;
    this->bigint_precision = this->bigint_precision;

    this->end_cut = plain_text.size() + this->i;

    this->i += j;

    while (i < this->end_cut) {
        // yo
        this->action (plain_text, j);
    }

    if (root)
    {
        _check_end (plain_text, j);
    }
}

void manapi::json_builder::_check_type(std::string_view plain_text, size_t &j) {
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
                this->action = std::bind(&json_builder::_build_object, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            case '[':
                this->type = json::type_array;
                this->object = json::array();
                this->action = std::bind(&json_builder::_build_array, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            case '"':
                this->type = json::type_string;
                this->action = std::bind(&json_builder::_build_string, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
            default:
                this->type = json::type_number;
                this->action = std::bind(&json_builder::_build_numeric, this, std::placeholders::_1, std::placeholders::_2);
                goto finish;
        }
    }

    finish:

    if (j != plain_text.size())
    {
        // type was found
        this->start_cut = this->i;

        _check_eq_type ();
    }
}

void manapi::json_builder::_build_string(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        _check_max_mean(true);

        unsigned char c = plain_text.at(j);
        if (_valid_utf_char(plain_text, j, this->wchar_left))
        {
            // nothing
        }
        else
        {
            if (this->escaped)
            {
                this->escaped = false;

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
                        throw json_parse_exception(ERR_JSON_BAD_ESCAPED_CHAR, "Bad escaped character at " + std::to_string(this->i));
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
                    throw json_parse_exception (ERR_JSON_BAD_ESCAPED_CHAR, "bad Unicode escape at " + std::to_string(this->i));
                }

                switch (c) {
                    case '\t':
                    case '\n':
                    case '\r':
                    case '\f':
                    case '\b':
                        throw json_parse_exception(ERR_JSON_INVALID_CHAR, "Bad control character at " + std::to_string(this->i));
                }

                if (c == '\\')
                {

                    this->escaped = true;
                    continue;
                }

                if (c == '"')
                {
                    this->opened_quote = !this->opened_quote;

                    if (!this->opened_quote) {
                        goto finish;
                    }

                    continue;
                }
            }
        }


        if (this->opened_quote)
        {
            this->buffer.push_back(static_cast<char> (c));
        }
        else {
            if (!unicode::is_space_symbol(c))
            {
                json::error_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (plain_text.size() != j || this->getting)
    {
        if (this->opened_quote)
        {
            json::error_unexpected_end(j);
        }

        // closed string
        i++;
        j++;

        this->end_cut = this->i;
        this->object = this->buffer;
        _check_string();

        // clean up only after passing the checks
        this->buffer.clear();

        this->ready = true;
    }
}

void manapi::json_builder::_build_numeric(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, this->i++)
    {
        unsigned char c = plain_text[j];

        if (this->buffer.empty())
        {
            this->operate_already = true;

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
                json::error_invalid_char(plain_text, j);
            }
            this->buffer += static_cast<char> (c);
        }
        else {
            if (this->buffer.empty())
            {
                // its true or false or null
                this->action = std::bind(&json_builder::_build_numeric_string, this, std::placeholders::_1, std::placeholders::_2);

                return;
            }

            if (this->buffer.size() == 1 && this->buffer[0] == '-')
            {
                // we need at least 1 digit
                json::error_invalid_char(plain_text, j);
            }

            if (unicode::is_space_symbol(c)) {
                goto finish;
            }

            switch (c) {
                case '-':
                case '+':
                    if (this->operate_already) {
                        json::error_invalid_char(plain_text, j);
                    }
                    this->operate_already = true;
                    this->buffer += static_cast<char> (c);
                break;
                case 'e':
                case 'E':
                    // exp
                    if (this->exp_already) {
                        json::error_invalid_char(plain_text, j);
                    }
                    if (this->type != json::type_decimal) {
                        this->type = json::type_decimal;
                    }
                    this->buffer += static_cast<char> (c);
                    this->operate_already = false;
                break;
                case '.':
                    if (this->type == json::type_decimal)
                    {
                        json::error_invalid_char(plain_text, j);
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
                    json::error_invalid_char(plain_text, j);
            }
        }
    }

    finish:
    if (j != plain_text.size() || this->getting)
    {
        // bcz we didnt consider this chars ('}', ',') in this type
        this->end_cut = this->i;

        // finish
        if (this->use_bigint)
        {
            this->object = json (bigint(this->buffer));
        }
        else
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

        _check_numeric();

        // clean up only after passing the checks
        this->buffer.clear();

        this->ready = true;
    }
}

void manapi::json_builder::_build_numeric_string(std::string_view plain_text, size_t &j) {
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
    if (j != plain_text.size() || this->getting)
    {
        this->end_cut = i;

        // check if this is true, false, null ->

        const std::string_view value = std::move(this->buffer);

        this->ready = true;

        // true
        if (value == "true") {
            this->type = json::type_boolean;
            this->object = json (true);
            _check_numeric();
            return;
        }

        // false
        if (value == "false") {
            this->type = json::type_boolean;
            this->object = json (false);
            _check_numeric();
            return;
        }

        // null
        if (value == "null") {
            this->type = json::type_null;
            this->object = json(nullptr);
            _check_numeric();
            return;
        }

        // TODO escape sub_plain_text
        throw json_parse_exception(ERR_JSON_INVALID_STRING, std::format("Invalid string: '{}' ({}, {})",
                                               value, start_cut + 1,
                                               end_cut));
    }
}

void manapi::json_builder::_build_object(std::string_view plain_text, size_t &j) {
    const auto next_parent_for_child = [this] () -> const json& {
        _next_type();
        _check_part_object();

        return get_current_type()["value"][this->key]["obj"];
    };

    doit:
    if (this->item != nullptr)
    {
        this->i -= j;
        this->item->_parse(plain_text, j, false);
        this->i = this->i + j;

        if (this->item->ready)
        {
            json it = this->item->get();
            this->item = nullptr;

            if (this->is_key)
            {
                this->key = it.as_string();
            }
            else
            {
                this->object.insert(this->key, std::move(it));
            }

            this->go_to_delimiter = true;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (unicode::is_space_symbol(c))
        {
            continue;
        }

        if (this->go_to_delimiter)
        {
            if (unicode::is_space_symbol(c))
            {
                continue;
            }

            if ((this->is_key && c == ':') || (!this->is_key && c == ','))
            {
                this->is_key = !is_key;
                this->go_to_delimiter = false;
                continue;
            }

            if (c == '}') {
                this->opened_quote = false;
                // the end
                goto finish;
            }


            json::error_invalid_char(plain_text, j);
        }

        if (this->opened_quote)
        {
            if (c == '}')
            {
                this->opened_quote = false;
                goto finish;
            }

            if (this->is_key || this->current_types == nullptr)
            {
                this->item = std::make_unique<json_builder> (json(nullptr), this->use_bigint, this->bigint_precision);
            }
            else
            {
                this->item = std::make_unique<json_builder>(get_current_type()["value"][key]["obj"], this->use_bigint, this->bigint_precision);
                this->item->next_parent = next_parent_for_child;
            }
            goto doit;
        }

        if (c == '{')
        {
            this->opened_quote = true;
            continue;
        }

        json::error_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || this->getting)
    {
        // is_key = true when {..., ...',' <- we are expecting a key}, false otherwise
        if (this->opened_quote || (this->is_key && this->object.size() > 0))
        {
            json::error_unexpected_end(i);
        }

        this->i++;
        j++;

        this->end_cut = this->i;
        this->is_key = true;
        this->key.clear();

        _check_object();

        this->ready = true;
    }
}

void manapi::json_builder::_build_array(std::string_view plain_text, size_t &j) {
    doit:
    if (this->item != nullptr)
    {
        this->i -= j;
        this->item->_parse(plain_text, j, false);
        this->i = this->i + j;

        if (this->item->ready)
        {
            json it = this->item->get();
            this->item = nullptr;

            this->object.push_back(std::move(it));

            this->go_to_delimiter = true;
        }
    }

    for (; j < plain_text.size(); j++, i++)
    {
        unsigned char c = plain_text[j];

        if (unicode::is_space_symbol(c))
        {
            continue;
        }

        if (this->go_to_delimiter)
        {
            if (unicode::is_space_symbol(c))
            {
                continue;
            }

            if (c == ',')
            {
                this->go_to_delimiter = false;
                continue;
            }

            if (c == ']') {
                this->opened_quote = false;
                // the end
                goto finish;
            }


            json::error_invalid_char(plain_text, j);
        }

        if (this->opened_quote)
        {
            if (c == ']')
            {
                this->opened_quote = false;
                goto finish;
            }

            // if exists default (TYPE(...)[...]) or exists ({..., ..., ...})

            if (this->current_types == nullptr)
            {
                this->item = std::make_unique<json_builder>(json(nullptr), this->use_bigint, this->bigint_precision);
            }
            else
            {
                const auto &current = get_current_type();

                if (current.contains("default"))
                {
                    this->item = std::make_unique<json_builder>(get_current_type()["default"], this->use_bigint, this->bigint_precision);
                }
                else
                {
                    this->item = std::make_unique<json_builder>(get_current_type()["value"][this->element_index]["obj"], this->use_bigint, this->bigint_precision);
                }
            }
            this->element_index++;
            goto doit;
        }

        if (c == '[')
        {
            this->opened_quote = true;
            continue;
        }

        json::error_invalid_char(plain_text, j);
    }

    finish:
    if (j != plain_text.size() || this->getting)
    {
        if (this->opened_quote)
        {
            json::error_unexpected_end(this->i);
        }

        this->i++;
        j++;

        this->end_cut = this->i;
        _check_array();

        this->ready = true;
    }
}

void manapi::json_builder::_check_end(std::string_view plain_text, size_t &j) {
    for (; j < plain_text.size(); j++, i++)
    {
        if (!unicode::is_space_symbol(plain_text[j]))
        {
            json::error_invalid_char(plain_text, j);
        }
    }
}

void manapi::json_builder::_reset_type() {
    this->max_mean_size = nullptr;
    this->min_mean_size = nullptr;
}

void manapi::json_builder::_next_type() {
    this->current_type++;

    if ((this->current_types != nullptr && this->current_types->is_array() && this->current_type < this->current_types->size()) || _next_parent())
    {
        _check_eq_type();
        return;
    }

    throw json_parse_exception(ERR_JSON_MASK_VERIFY_FAILED, "json_mask error");
}

bool manapi::json_builder::_next_parent() {
    if (this->next_parent != nullptr) {
        this->current_types = &this->next_parent();
        this->current_type = 0;
        return true;
    }
    return false;
}

void manapi::json_builder::_check_eq_type() {
    _reset_type();

    if (this->current_types == nullptr)
    {
        return;
    }
    auto &current = get_current_type();
    auto &current_type = current["type"].as_integer();
    if (current.contains("max_mean")) { max_mean_size = &current["max_mean"].as_integer(); }
    if (current.contains("min_mean")) { min_mean_size = &current["min_mean"].as_integer(); }

    if (current_type == -1)
    {
        // this solve is better
        this->current_types = nullptr;
        return;
    }

    if (this->type == json::type_number)
    {
        if (
            current_type == json::type_decimal ||
            current_type == json::type_bigint ||
            current_type == json::type_integer ||
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

    this->_next_type();
    this->_check_eq_type();
}

bool manapi::json_builder::_check_max_mean(const bool &building) {
    if (this->max_mean_size == nullptr) { return true; }

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
        switch (this->type)
        {
            case json::type_string:
                if (this->object.size() < *this->max_mean_size) { return true; }
            break;
            case json::type_integer:
                if (this->object.as_integer() < *this->max_mean_size) { return true; }
            break;
            case json::type_bigint:
                if (this->object.as_bigint() < *this->max_mean_size) { return true; }
            break;
            case json::type_decimal:
                if (this->object.as_decimal() < *this->max_mean_size) { return true; }
            break;
            case json::type_array:
            case json::type_object:
                if (this->object.size() < *this->max_mean_size) { return true; }
            break;
            default:
                return true;
        }
    }
    _next_type();
    return false;
}

bool manapi::json_builder::_check_min_mean() {
    if (this->min_mean_size == nullptr) { return true; }

    switch (this->type)
    {
        case json::type_string:
            if (*this->min_mean_size < 0 || this->object.size() > *this->min_mean_size) { return true; }
        break;
        case json::type_integer:
            if (this->object.as_integer() > *this->min_mean_size) { return true; }
        break;
        case json::type_bigint:
            if (this->object.as_bigint() > *this->min_mean_size) { return true; }
        break;
        case json::type_decimal:
            if (this->object.as_decimal() > *this->min_mean_size) { return true; }
        break;
        case json::type_array:
        case json::type_object:
            if (*this->min_mean_size < 0 || this->object.size() > *this->min_mean_size) { return true; }
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
    if ((current["value"] == this->object)) { return true; }

    _next_type();
    return false;
}

bool manapi::json_builder::_check_default() {
    return (this->current_types == nullptr) || (_check_max_mean() && _check_min_mean() && _check_type_none_complex_value() && _check_meta_value());
}

bool manapi::json_builder::_check_meta_value() {
    const auto &current = get_current_type ();
    if (this->object.is_array() || this->object.is_object() || !current.contains("value")) { return true; }

    if (current.at("value") == this->object) { return true; }
    _next_type();
    return false;
}

void manapi::json_builder::_check_string() {
    while (true)
    {
        if (this->_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_numeric() {
    while (true)
    {
        if (this->_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_object() {
    while (true)
    {
        if (this->_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_array() {
    while (true)
    {
        if (this->_check_default())
        {
            break;
        }
    }
}

void manapi::json_builder::_check_part_object() {
    while (true) {
        json_mask mask_child;
        mask_child.set_api_tree({{"obj", get_current_type()}, {"none", false}});
        mask_child.set_complete_status(false);
        if (mask_child.valid(this->object)) {
            break;
        }
        _next_type();
    }
}

const manapi::json & manapi::json_builder::get_current_type() {
    if (this->current_types->is_array()) { return this->current_types->at(this->current_type); }
    return *this->current_types;
}

bool manapi::json_builder::_valid_utf_char(std::string_view plain_text, const size_t &i, size_t &left) {
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

    this->_reset_type();

    this->action = [this](std::string_view && PH1, size_t & PH2) -> void { _check_type(std::forward<decltype(PH1)>(PH1), PH2); };
}
