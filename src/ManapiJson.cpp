#include <memory.h>
#include <format>

#include "ManapiJson.hpp"
#include "ManapiBigint.hpp"
#include "ManapiUtils.hpp"

const static std::string JSON_TRUE   = "true";
const static std::string JSON_FALSE  = "false";
const static std::string JSON_NULL   = "null";

const static manapi::utils::json::UNICODE_STRING JSON_W_TRUE   = manapi::utils::str4to32("true");
const static manapi::utils::json::UNICODE_STRING JSON_W_FALSE  = manapi::utils::str4to32("false");
const static manapi::utils::json::UNICODE_STRING JSON_W_NULL   = manapi::utils::str4to32("null");

#define THROW_MANAPI_JSON_MISSING_FUNCTION this->throw_could_not_use_func(__FUNCTION__)

manapi::utils::json::json() = default;

manapi::utils::json::json(const std::string &str, bool is_json) {
    if (is_json)
    {
        this->parse(str);
    }
    else
    {
        type = type_string;
        src = new STRING (str);
    }
}

manapi::utils::json::json(const UNICODE_STRING &str, bool is_json) {
    if (is_json)
    {
        this->parse(str);
    }
    else
    {
        type = type_string;
        src = new STRING (str32to4(str));
    }
}

manapi::utils::json::json(const NUMBER &num) {
    this->parse(num);
}

manapi::utils::json::json(const size_t &num) {
    this->parse(num);
}

manapi::utils::json::json(const manapi::utils::json &other) {
    *this = other;
}

manapi::utils::json::json(json &&other) noexcept {
    src = other.src;
    type = other.type;

    other.type = type_null;
    other.src = nullptr;
}

manapi::utils::json::json(const char *plain_text, bool is_json)
{
    if (is_json)
    {
        this->parse(plain_text);
    }
    else
    {
        src = new STRING (plain_text);
        type = type_string;
    }
}

manapi::utils::json::json(const int &num)
{
    this->parse(num);
}

manapi::utils::json::json(const double &num)
{
    this->parse(num);
}

manapi::utils::json::json(const DECIMAL &num)
{
    this->parse(num);
}

manapi::utils::json::json(const BIGINT &num)
{
    this->parse(num);
}

manapi::utils::json::json(const BOOLEAN &value)
{
    this->parse(value);
}

manapi::utils::json::json(const OBJECT &obj) {
    this->parse(obj);
}

manapi::utils::json::json(const ARRAY &arr) {
    this->parse(arr);
}

manapi::utils::json::json(const nullptr_t &n)
{
    this->parse(n);
}

manapi::utils::json::json (const std::initializer_list<json> &data) {
    if (data.size() == 0)
    {
        // nothing
        return;
    }
    if (data.size() == 1 && data.begin()->type != type_pair)
    {
        // equal
        this->operator=(*data.begin());
    }
    else if (data.size() == 2 && data.begin()->is_string())
    {
        // std::pair will be automatic clean up, bcz ~json do it
        // pair
        auto it = data.begin();

        auto key = it++;
        auto value = *it;

        // resolve pair to array
        if (value.type == type_pair)
        {
            json arr = json::array ();
            arr.push_back(reinterpret_cast <std::pair <json, json> *> (value.src)->first);
            arr.push_back(reinterpret_cast <std::pair <json, json> *> (value.src)->second);

            value = arr;
        }

        // do not need to malloc json
        src = new std::pair <json, json> (*key, std::move(value));
        type = type_pair;
    }
    else {
        // array or map

        // if one element is pair -> all elements must be as pair
        bool map = true;

        for (const auto &it: data)
        {
            if (it.type != type_pair)
            {
                map = false;

                break;
            }
        }

        if (map)
        {
            type = type_object;
            src = new json::OBJECT ();
        }
        else {
            type = type_array;
            src = new json::ARRAY ();
        }

        if (map)
        {
            for (const auto & it : data)
            {
                auto &key = reinterpret_cast <std::pair <json, json> *> (it.src)->first.get<std::string>();
                json value = reinterpret_cast <std::pair <json, json> *> (it.src)->second;

                reinterpret_cast <json::OBJECT *> (src)->insert({key, std::move(value)});
            }
        }
        else
        {
            for (auto it = data.begin(); it != data.end(); it++)
            {
                if (it->type == type_pair)
                {
                    // we need to resolve pair to array

                    json first = reinterpret_cast <std::pair <json, json> *> (it->src)->first;
                    json second = reinterpret_cast <std::pair <json, json> *> (it->src)->second;

                    json element (json::array());

                    try
                    {
                        reinterpret_cast <json::ARRAY *> (element.src)->push_back(std::move(first));
                        reinterpret_cast <json::ARRAY *> (element.src)->push_back(std::move(second));

                        reinterpret_cast <json::ARRAY *> (src)->push_back(std::move(element));
                    }
                    catch (const json_parse_exception &e)
                    {
                        throw json_parse_exception(e);
                    }
                }
                else
                {
                    try
                    {
                        reinterpret_cast <json::ARRAY *> (src)->push_back(*it);
                    }
                    catch (const json_parse_exception &e)
                    {
                        throw json_parse_exception(e);
                    }
                }
            }
        }
    }
}

manapi::utils::json::~json() {
    delete_value();
}

void manapi::utils::json::parse(const NUMBER &num) {
    type    = type_number;
    src     = new NUMBER (num);
}

void manapi::utils::json::parse(const int &num) {
    this->parse(static_cast<NUMBER> (num));
}

void manapi::utils::json::parse(const double &num) {
    this->parse(static_cast<DECIMAL> (num));
}

void manapi::utils::json::parse(const DECIMAL &num) {
    type    = type_decimal;
    src     = new DECIMAL (num);
}

void manapi::utils::json::parse(const BIGINT &num) {
    type    = type_bigint;
    src     = new BIGINT(num);
}

void manapi::utils::json::parse(const OBJECT &obj) {
    type    = type_object;
    src     = new OBJECT (obj);
}

void manapi::utils::json::parse(const ARRAY &arr) {
    type    = type_array;
    src     = new ARRAY (arr);
}

void manapi::utils::json::parse(const BOOLEAN &val) {
    type    = type_boolean;
    src     = new BOOLEAN (val);
}

void manapi::utils::json::parse(const STRING &plain_text) {
    this->parse(str4to32(plain_text));
}

void manapi::utils::json::parse(const NULLPTR &n) {
    type    = type_null;
    src     = nullptr;
}

void manapi::utils::json::parse(const UNICODE_STRING &plain_text, const bool &use_bigint, const size_t &bigint_precision, size_t start) {
    if (plain_text.empty())
    {
        error_unexpected_end (0);
    }

    start_cut       = start;
    end_cut         = plain_text.size() - 1;

    for (size_t i = start_cut; i <= end_cut; i++) {
        if (is_space_symbol(plain_text[i]))
        {
            continue;
        }

        switch (plain_text[i]) {
            case '{':
                type = type_object;
                break;
            case '[':
                type = type_array;
                break;
            case '"':
                type = type_string;
                break;
            default:
                type = type_numeric;
                break;
        }

        start_cut = i;

        break;
    }

    UNICODE_STRING sub_plain_text;
    UNICODE_STRING key;
    bool opened_quote;

    switch (type) {
        case type_object:
            src = new json::OBJECT ();

            opened_quote    = false;

            for (size_t i = start; i <= end_cut; i ++) {
                // delete extra '\' in string
                if (opened_quote && plain_text[i] == '\\') {
                    i++;
                    if (i > end_cut)
                        error_unexpected_end(end_cut);
                }
                // opened " for key
                else if (plain_text[i] == '"') {
                    opened_quote = !opened_quote;

                    continue;
                }

                if (opened_quote) {
                    // add to key
                    key += plain_text[i];
                } else {
                    if (plain_text[i] == ':') {

                        // if key empty
                        if (key.empty()) {
                            error_unexpected_end(i);
                        }

                        json obj;
                        obj.root = false;
                        obj.parse(plain_text, use_bigint, bigint_precision, i + 1);
                        i = obj.get_end_cut() + 1;
                        reinterpret_cast<json::OBJECT *>(src)->insert({str32to4(key), std::move(obj)});

                        if (i > end_cut)
                        {
                            error_invalid_char(plain_text, end_cut);
                        }

                        // skip to ',' or '}'

                        for (; i <= end_cut; i++) {
                            if (is_space_symbol(plain_text[i]))
                            {
                                continue;
                            }

                            else if (plain_text[i] == '}') {
                                end_cut = i;
                                break;
                            }

                            else if (plain_text[i] == ',')
                            {
                                break;
                            }

                            else
                            {
                                error_invalid_char(plain_text, i);
                            }
                        }

                        key = UNICODE_STRING();
                    }
                    else if (plain_text[i] == '}') {
                        if (!key.empty())
                        {
                            error_invalid_char(plain_text, i);
                        }

                        end_cut = i;
                        break;
                    } else {
                        if (is_space_symbol(plain_text[i]) || plain_text[i] == '{')
                        {
                            continue;
                        }

                        error_invalid_char(plain_text, i);
                    }
                }
            }

            if (plain_text[end_cut] != '}')
            {
                error_invalid_char(plain_text, end_cut);
            }

            break;
        case type_array:
            src = new std::vector<json *> ();

            // bcz we know that it is an array -> skip the first char ('[')

            for (size_t i = start_cut + 1; i <= end_cut; i++) {
                if (plain_text[i] == ']') {
                    // the end
                    end_cut = i;
                    break;
                }

                // skip the space symbols
                if (is_space_symbol(plain_text[i]))
                {
                    continue;
                }

                json obj;
                obj.root = false;
                obj.parse(plain_text, use_bigint, bigint_precision, i);
                i = obj.get_end_cut() + 1;

                if (i > end_cut)
                {
                    error_unexpected_end(end_cut);
                }

                // append
                reinterpret_cast<json::ARRAY  *> (src)->push_back(std::move(obj));

                // skip all chars to ','
                for (; i <= end_cut; i++) {
                    if (is_space_symbol(plain_text[i]))
                    {
                        continue;
                    }

                    else if (plain_text[i] == ',')
                    {
                        break;
                    }

                    else if (plain_text[i] == ']') {
                        // the end
                        end_cut = i;
                        break;
                    }

                    else
                    {
                        error_invalid_char(plain_text, i);
                    }
                }
            }

            if (plain_text[end_cut] != ']')
            {
                error_unexpected_end(end_cut);
            }

            break;
        case type_string:
            opened_quote = false;

            for (size_t i = start_cut; i <= end_cut; i++) {
                if (plain_text[i] == '\\') {
                    i++;

                    if (i > end_cut)
                    {
                        error_unexpected_end(end_cut);
                    }

                    // TODO: Bad escaped char

                } else if (plain_text[i] == '"') {
                    opened_quote = !opened_quote;

                    if (!opened_quote) {
                        // closed string
                        end_cut = i;
                        break;
                    }

                    continue;
                }

                if (opened_quote)
                    sub_plain_text.push_back(plain_text[i]);
            }
            if (opened_quote)
                error_unexpected_end(end_cut);
            src = new std::string(str32to4(sub_plain_text));

            break;
        case type_numeric:

            size_t i = start_cut;

            if (i >= plain_text.size()) {
                error_invalid_char(plain_text, plain_text.size() - 1);
            }

            if (plain_text[i] == '-') {
                sub_plain_text += '-';

                i++;
                if (i >= plain_text.size())
                    error_invalid_char(plain_text, plain_text.size() - 1);
            }

            if (plain_text[i] >= '0' && plain_text[i] <= '9') {
                // we think that it is the integer
                type = type_number;

                sub_plain_text += plain_text[i];
                // next
                i++;

                for (; i <= end_cut; i++) {
                    if (plain_text[i] >= '0' && plain_text[i] <= '9')
                        sub_plain_text += plain_text[i];
                    else {
                        if (is_space_symbol(plain_text[i])) {
                            end_cut = i;
                            break;
                        }

                        switch (plain_text[i]) {
                            case '.':
                                if (type == type_decimal)
                                    error_invalid_char(plain_text, i);

                                // its decimal
                                type            = type_decimal;
                                sub_plain_text  += plain_text[i];
                                break;
                            case '}':
                            case ']':
                            case ',':
                                // bcz we didnt consider this char ('}', ',') in this type
                                end_cut = i - 1;
                                break;
                            default:
                                error_invalid_char(plain_text, i);
                        }
                    }
                }

                if (use_bigint) {
                    // bigint for all type of number
                    src     = new bigint(str32to4(sub_plain_text), bigint_precision);

                    type    = type_bigint;
                }
                else {

                    if (type == type_number)
                        // numeric
                        src = new ssize_t(std::stoll(str32to4(sub_plain_text)));
                    else {
                        if (plain_text[end_cut] == '.')
                            error_invalid_char(plain_text, end_cut);
                        // decimal
                        src = new json::DECIMAL(std::stold(str32to4(sub_plain_text)));
                    }
                }
            }
            else {
                for (; i <= end_cut; i++) {
                    if (is_space_symbol(plain_text[i]) || plain_text[i] == '}' ||
                            plain_text[i] == ',' || plain_text[i] == ']') {
                        // i - 1 (not just i) bcz this is not true, or false}
                        end_cut = i - 1;
                        break;
                    }

                    sub_plain_text += plain_text[i];
                }
                // check if its true, false, null ->

                // true
                if (sub_plain_text == JSON_W_TRUE) {
                    src     = new bool (true);
                    type    = type_boolean;
                    break;
                }

                // false
                if (sub_plain_text == JSON_W_FALSE) {
                    src     = new bool (false);
                    type    = type_boolean;
                    break;
                }

                // null
                if (sub_plain_text == JSON_W_NULL) {
                    src     = nullptr;
                    type    = type_null;
                    break;
                }

                // otherwise -> null
                src         = nullptr;
                type        = type_null;

                // TODO escape sub_plain_text
                throw json_parse_exception(std::format("Invalid string: '{}' ({}, {})",
                                                       manapi::utils::str32to4(sub_plain_text), start_cut + 1,
                                                       end_cut + 1));
            }

            break;
    }

    if (root)
    {
        for (size_t i = end_cut + 1; i < plain_text.size(); i++)
        {
            if (is_space_symbol(plain_text[i]))
            {
                continue;
            }

            error_invalid_char(plain_text, i);
        }
    }
}

void manapi::utils::json::parse(const size_t &num) {
    this->parse (static_cast<NUMBER> (num));
}

std::string manapi::utils::json::dump(const size_t &spaces, const size_t &first_spaces) const {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) str += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else str += ' ';
#define JSON_DUMP_NEED_SPACES   for (size_t z = 0; z < total_spaces; z++) str += ' ';
#define JSON_DUMP_LAST_SPACES   for (size_t z = 0; z < first_spaces; z++) str += ' ';
    std::string str;

    if (type == type_string)
    {
        // TODO: what the hell is that: str32to4(foo(str4to32(...)))
        str = '"' + str32to4(escape_string(str4to32(as_string()))) + '"';
    }

    else if (type == type_decimal)
    {
        str = std::to_string(as_decimal());
    }

    else if (type == type_number)
    {
        str = std::to_string(as_number());
    }

    else if (type == type_bigint)
    {
        str = '"' + as_bigint().stringify() + '"';
    }

    else if (type == type_boolean)
    {
        str = as_bool() ? JSON_TRUE : JSON_FALSE;
    }

    else if (type == type_null)
    {
        str = JSON_NULL;
    }

    else if (type == type_object) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto map = as_object();
        str += '{';

        JSON_DUMP_NEED_NEW_LINE

        if (!map.empty()) {
            auto it = map.begin();

            // first
            if (it != map.end()) {
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += str32to4(escape_string(str4to32(it->first))) + "\": " + it->second.dump(spaces, total_spaces);
                ++it;
            }

            // others
            for (; it != map.end(); ++it) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += str32to4(escape_string(str4to32(it->first))) + "\": " + it->second.dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE

        JSON_DUMP_LAST_SPACES

        str += '}';
    }

    else if (type == type_array) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto arr = as_array();
        str += '[';

        JSON_DUMP_NEED_NEW_LINE


        if (!arr.empty()) {
            // dump items
            size_t i = 0;

            // first
            JSON_DUMP_NEED_SPACES
            str += arr.at(i).dump(spaces, total_spaces);

            i++;

            // others
            for (; i < arr.size(); i++) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += arr.at(i).dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE
        JSON_DUMP_LAST_SPACES

        str += ']';
    }

    else if (type == type_pair)
    {
        THROW_MANAPI_EXCEPTION("A bug has been detected: {}", "type = type_pair");
    }

    return str;
}

size_t manapi::utils::json::get_start_cut() const {
    return start_cut;
}

size_t manapi::utils::json::get_end_cut() const {
    return end_cut;
}

void manapi::utils::json::error_invalid_char(const UNICODE_STRING &plain_text, const size_t &i) {
    throw json_parse_exception(std::format("Invalid char '{}' at {}", str32to4(plain_text[i]), i + 1));
}

void manapi::utils::json::error_unexpected_end(const size_t &i) {
    throw json_parse_exception(std::format("Unexpected end of JSON input at {}", i + 1));
}

void manapi::utils::json::delete_value() {
    delete_value_static(type, src);
}

manapi::utils::json &manapi::utils::json::operator[](const STRING &key) {
    return std::ref(this->at(key));
}

manapi::utils::json &manapi::utils::json::operator[](const UNICODE_STRING &key) {
    return std::ref(this->at(key));
}

manapi::utils::json &manapi::utils::json::operator[](const size_t &index) {
    return std::ref(this->at(index));
}

manapi::utils::json & manapi::utils::json::operator[](const int &index) {
    return std::ref(this->at(index));
}

const manapi::utils::json & manapi::utils::json::operator[](const STRING &key) const {
    return std::ref(this->at(key));
}

const manapi::utils::json & manapi::utils::json::operator[](const UNICODE_STRING &key) const {
    return std::ref(this->at(key));
}

const manapi::utils::json & manapi::utils::json::operator[](const size_t &index) const {
    return std::ref(this->at(index));
}

const manapi::utils::json & manapi::utils::json::operator[](const int &index) const {
    return std::ref(this->at(index));
}

manapi::utils::json &manapi::utils::json::at(const UNICODE_STRING &key)  {
    return std::ref(this->at(str32to4(key)));
}

manapi::utils::json &manapi::utils::json::at(const std::string &key)  {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &map = this->get<OBJECT>();

    if (!map.contains(key))
    {
        THROW_MANAPI_EXCEPTION("No such key. ({})", escape_string(key));
    }

    return map.at(key);
}

manapi::utils::json &manapi::utils::json::at(const size_t &index)  {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

    if (arr.size() <= index)
    {
        THROW_MANAPI_EXCEPTION("Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

manapi::utils::json & manapi::utils::json::at(const int &index) {
    return std::ref(this->at(static_cast<size_t> (index)));
}

const manapi::utils::json & manapi::utils::json::at(const std::string &key) const {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &map = this->get<OBJECT>();

    if (!map.contains(key))
    {
        THROW_MANAPI_EXCEPTION("No such key. ({})", escape_string(key));
    }

    return map.at(key);
}

const manapi::utils::json & manapi::utils::json::at(const UNICODE_STRING &key) const {
    return std::ref(this->at(str32to4(key)));
}

const manapi::utils::json & manapi::utils::json::at(const size_t &index) const {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

    if (arr.size() <= index)
    {
        THROW_MANAPI_EXCEPTION("Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

const manapi::utils::json & manapi::utils::json::at(const int &index) const {
    return std::ref(this->at(static_cast<size_t> (index)));
}

manapi::utils::json& manapi::utils::json::operator=(const std::string &str) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_string;
    src     = new std::string (str);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const bool &b) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_boolean;
    src     = new bool (b);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const ssize_t &num) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_number;
    src     = new ssize_t (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const double &num) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_decimal;
    src     = new json::DECIMAL (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const json::DECIMAL &num) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_decimal;
    src     = new json::DECIMAL (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const long long &num) {
    return this->operator=((ssize_t) num);
}

manapi::utils::json &manapi::utils::json::operator=(nullptr_t const &n) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_null;
    src     = nullptr;

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const UNICODE_STRING &str) {
    return this->operator=(str32to4(str));
}

manapi::utils::json &manapi::utils::json::operator=(const char *str) {
    return this->operator=(std::string(str));
}

manapi::utils::json &manapi::utils::json::operator=(const int &num) {
    return this->operator=(static_cast<ssize_t> (num));
}

manapi::utils::json &manapi::utils::json::operator=(const manapi::utils::bigint &num) {
    utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    type    = type_bigint;
    src     = new bigint(num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const manapi::utils::json &obj) {
    if (&obj != this)
    {
        // temp values
        short   temp_type = this->type;
        void    *temp_src = this->src;

        this->start_cut             = 0;
        this->type                  = obj.type;
        this->root                  = true;

        switch (type) {
            case type_string:
                src = new STRING (obj.as_string());
            break;
            case type_number:
                src = new ssize_t (*reinterpret_cast<ssize_t *> (obj.src));
            break;
            case type_decimal:
                src = new json::DECIMAL (*reinterpret_cast<json::DECIMAL *> (obj.src));
            break;
            case type_bigint:
                src = new bigint(*reinterpret_cast<bigint *> (obj.src));
            break;
            case type_array:
            {
                const auto arr_oth = reinterpret_cast <json::ARRAY  *> (obj.src);

                src = new json::ARRAY (*arr_oth);
                break;
            }
            case type_object:
            {
                const auto map_oth = reinterpret_cast <json::OBJECT  *> (obj.src);

                src = new json::OBJECT(*map_oth);
                break;
            }
            case type_boolean:
                src = new bool(*reinterpret_cast<bool *> (obj.src));
            break;
            case type_null:
                src = nullptr;
            break;
        }

        delete_value_static (temp_type, temp_src);
    }
    return *this;
}

manapi::utils::json & manapi::utils::json::operator=(json &&obj) {
    std::swap(src, obj.src);
    std::swap(type, obj.type);
    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::utils::json (data);
}

void manapi::utils::json::insert(const UNICODE_STRING &key, const manapi::utils::json &obj) {
    insert (str32to4(key), obj);
}

void manapi::utils::json::insert(const std::string &key, const manapi::utils::json &obj) {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto map = reinterpret_cast <json::OBJECT *> (src);

    if (map->contains(key))
    {
        THROW_MANAPI_EXCEPTION("duplicate key: {}", escape_string(key));
    }

    json item;
    item = obj;
    item.root = false;

    map->insert({key, std::move(item)});
}

void manapi::utils::json::push_back(manapi::utils::json obj) {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto arr = reinterpret_cast <json::ARRAY  *> (src);

    obj.root = false;

    arr->push_back(std::move(obj));
}

void manapi::utils::json::pop_back() {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto arr = reinterpret_cast <json::ARRAY  *> (src);

    arr->pop_back();
}

manapi::utils::json manapi::utils::json::object() {
    json obj;

    obj.type    = type_object;
    obj.src     = new json::OBJECT;

    return obj;
}

manapi::utils::json manapi::utils::json::array() {
    // rvalue, arr not be destroyed
    json arr;

    arr.type    = type_array;
    arr.src     = new json::ARRAY ;

    return arr;
}

manapi::utils::json manapi::utils::json::array(const std::initializer_list<json> &data) {
    // rvalue, arr not be destroyed
    auto arr = json::array();

    for (const auto &it: data)
    {
        reinterpret_cast <json::ARRAY *> (arr.src)->push_back(it);
    }

    return arr;
}

const manapi::utils::json::ARRAY & manapi::utils::json::each() const {
    return as_array();
}

const manapi::utils::json::OBJECT & manapi::utils::json::entries() const {
    return as_object();
}

manapi::utils::json::ARRAY & manapi::utils::json::each() {
    return get<ARRAY>();
}

manapi::utils::json::OBJECT & manapi::utils::json::entries() {
    return get<OBJECT>();
}

bool manapi::utils::json::contains(const UNICODE_STRING &key) const {
    return contains(str32to4(key));
}

bool manapi::utils::json::contains(const std::string &key) const {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    return reinterpret_cast <json::OBJECT *> (src)->contains(key);
}

void manapi::utils::json::erase(const UNICODE_STRING &key) {
    erase(str32to4(key));
}

void manapi::utils::json::erase(const std::string &key) {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    reinterpret_cast <json::OBJECT *> (src)->erase(key);
}

bool manapi::utils::json::is_object() const {
    return type == type_object;
}

bool manapi::utils::json::is_array() const {
    return type == type_array;
}

bool manapi::utils::json::is_string() const {
    return type == type_string;
}

bool manapi::utils::json::is_number() const {
    return type == type_number;
}

bool manapi::utils::json::is_null() const {
    return type == type_null;
}

bool manapi::utils::json::is_decimal() const {
    return type == type_decimal;
}

bool manapi::utils::json::is_bigint() const {
    return type == type_bigint;
}

bool manapi::utils::json::is_bool() const {
    return type == type_boolean;
}

const manapi::utils::json::OBJECT & manapi::utils::json::as_object() const {
    if (type == type_object) {
        return *static_cast<json::OBJECT *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::ARRAY & manapi::utils::json::as_array() const {
    if (type == type_array) {
        return *static_cast<json::ARRAY *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::STRING & manapi::utils::json::as_string() const {
    if (type == type_string) {
        return *static_cast<json::STRING *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::NUMBER & manapi::utils::json::as_number() const {
    if (type == type_number) {
        return *static_cast<json::NUMBER *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::BIGINT & manapi::utils::json::as_bigint() const {
    if (type == type_bigint) {
        return *static_cast<json::BIGINT *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::BOOLEAN & manapi::utils::json::as_bool() const {
    if (type == type_boolean) {
        return *static_cast<json::BOOLEAN *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::utils::json::DECIMAL & manapi::utils::json::as_decimal() const {
    if (type == type_decimal) {
        return *static_cast<json::DECIMAL *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::NULLPTR manapi::utils::json::as_null() const {
    if (type == type_null) {
        return *static_cast<json::NULLPTR *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::OBJECT manapi::utils::json::as_object_cast() const {
    if (type == type_object) {
        return as_object();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::ARRAY manapi::utils::json::as_array_cast() const {
    if (type == type_array) {
        return as_array();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::STRING manapi::utils::json::as_string_cast() const {
    if (type == type_string) {
        return as_string();
    }
    if (type == type_number) {
        return std::move(std::to_string(*static_cast<json::NUMBER *> (src)));
    }
    if (type == type_bigint) {
        return std::move(static_cast<json::BIGINT *> (src)->stringify());
    }
    if (type == type_decimal) {
        return std::move(std::to_string(*static_cast<json::DECIMAL *> (src)));
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::NUMBER manapi::utils::json::as_number_cast() const {
    if (type == type_number) {
        return as_number();
    }
    if (type == type_bigint) {
        return static_cast<json::BIGINT *> (src)->numberify();
    }
    if (type == type_decimal) {
        // long double to long long
        return static_cast <json::NUMBER> (*static_cast<json::DECIMAL *> (src));
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::NULLPTR manapi::utils::json::as_null_cast() const {
    if (type == type_null) {
        return as_null();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::DECIMAL manapi::utils::json::as_decimal_cast() const {
    if (type == type_decimal) {
        return as_decimal();
    }
    if (type == type_number) {
        return static_cast<json::DECIMAL> (*static_cast<json::NUMBER *> (src));
    }
    if (type == type_bigint) {
        return static_cast<json::BIGINT *> (src)->decimalify();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::BIGINT manapi::utils::json::as_bigint_cast() const {
    if (type == type_bigint) {
        return as_bigint();
    }
    if (type == type_number) {
        return std::move(bigint (*static_cast<json::NUMBER *> (src)));
    }
    if (type == type_decimal) {
        return std::move(bigint (*static_cast<json::DECIMAL *> (src)));
    }
    if (type == type_string) {
        return std::move(bigint (*static_cast<json::STRING *> (src)));
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json::BOOLEAN manapi::utils::json::as_bool_cast() const {
    if (type == type_boolean) {
        return as_bool();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

size_t manapi::utils::json::size() const {
    if (is_array())
    {
        return reinterpret_cast <json::ARRAY *> (src)->size();
    }
    if (is_string())
    {
        return str4to32(*reinterpret_cast <std::string *> (src)).size();
    }
    if (is_object())
    {
        return reinterpret_cast <json::OBJECT *> (src)->size();
    }

    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json &manapi::utils::json::operator+(const ssize_t &num) {
    if (is_bigint())
    {
        (*static_cast <BIGINT *> (src)) += num;
    }
    else if (is_number())
    {
        (*reinterpret_cast <ssize_t *> (src)) += num;
    }
    else if (is_decimal())
    {
        (*reinterpret_cast <json::DECIMAL *> (src)) += num;
    }
    else
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    return *this;
}

manapi::utils::json &manapi::utils::json::operator+(const int &num) {
    return this->operator+((ssize_t) num);
}

manapi::utils::json & manapi::utils::json::operator+(const DECIMAL &num) {
    if (is_bigint())
    {
        (*static_cast <BIGINT *> (src)) += num;
    }
    else if (is_number())
    {
        (*static_cast <NUMBER *> (src)) += static_cast<NUMBER> (num);
    }
    else if (is_decimal())
    {
        (*static_cast <DECIMAL *> (src)) += num;
    }
    else {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return *this;
}

manapi::utils::json & manapi::utils::json::operator+(const double &num) {
    if (is_bigint())
    {
        (*static_cast <BIGINT *> (src)) += num;
    }
    else if (is_number())
    {
        (*static_cast <NUMBER *> (src)) += static_cast<NUMBER> (num);
    }
    else if (is_decimal())
    {
        (*static_cast <DECIMAL *> (src)) += num;
    }
    else {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return *this;
}

manapi::utils::json & manapi::utils::json::operator+(const BIGINT &num) {
    if (is_bigint())
    {
        (*static_cast <BIGINT *> (src)) += num;
    }
    else if (is_number())
    {
        (*static_cast <NUMBER *> (src)) += num.numberify();
    }
    else if (is_decimal())
    {
        (*static_cast <DECIMAL *> (src)) += num.decimalify();
    }
    else {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return *this;
}

manapi::utils::json &manapi::utils::json::operator-(const ssize_t &num) {
    return this->operator+(-num);
}

manapi::utils::json &manapi::utils::json::operator-(const int &num) {
    return this->operator+(-num);
}

manapi::utils::json & manapi::utils::json::operator-(const DECIMAL &num) {
    return this->operator+(-num);
}

manapi::utils::json & manapi::utils::json::operator-(const double &num) {
    return this->operator+(-num);
}

manapi::utils::json & manapi::utils::json::operator-(const BIGINT &num) {
    return this->operator+(-num);
}

void manapi::utils::json::delete_value_static(const short &type, void *src) {
    switch (type) {
        case type_null:
            break;
        case type_array:
            delete static_cast<ARRAY  *> (src);
            break;
        case type_object:
            delete static_cast<OBJECT *> (src);
            break;
        case type_boolean:
            delete static_cast<bool *> (src);
            break;
        case type_number:
            delete static_cast<ssize_t *> (src);
            break;
        case type_string:
            delete static_cast<std::string *> (src);
            break;
        case type_decimal:
            delete static_cast<DECIMAL*> (src);
            break;
        case type_bigint:
            delete static_cast<BIGINT *> (src);
            break;
        case type_pair:
            delete static_cast<std::pair <json, json> *> (src);
            break;
        default:
            THROW_MANAPI_EXCEPTION("JSON BUG: Invalid type of data to delete: {}", static_cast<int> (type_pair));
    }
}

void manapi::utils::json::throw_could_not_use_func(const std::string &func) const
{
    THROW_MANAPI_EXCEPTION("json object with type {} could not use func: {}", static_cast <int> (type), func);
}


// Exceptions

manapi::utils::json_parse_exception::json_parse_exception(const std::string &msg) {
    message = msg;
}

const char *manapi::utils::json_parse_exception::what() const noexcept {
    return message.data();
}