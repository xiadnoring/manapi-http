#include <memory.h>
#include <format>

#include "ManapiJson.h"
#include "ManapiUtils.h"
#include "ManapiBigint.h"

const static std::string JSON_TRUE   = "true";
const static std::string JSON_FALSE  = "false";
const static std::string JSON_NULL   = "null";

const static std::u32string JSON_W_TRUE   = manapi::utils::str4to32("true");
const static std::u32string JSON_W_FALSE  = manapi::utils::str4to32("false");
const static std::u32string JSON_W_NULL   = manapi::utils::str4to32("null");

#define THROW_MANAPI_JSON_MISSING_FUNCTION this->throw_could_not_use_func(__FUNCTION__)

manapi::utils::json::json() = default;

manapi::utils::json::json(const std::string &str, bool is_json) {
    if (is_json)
    {
        this->parse(str);
    }
    else
    {
        type = MANAPI_JSON_STRING;
        src = new std::string (str);
    }
}

manapi::utils::json::json(const std::u32string &str, bool is_json) {
    if (is_json)
    {
        this->parse(str);
    }
    else
    {
        type = MANAPI_JSON_STRING;
        src = new std::string (str32to4(str));
    }
}

manapi::utils::json::json(const ssize_t &num) {
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

    other.type = MANAPI_JSON_NULL;
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
        src = new std::string (plain_text);
        type = MANAPI_JSON_STRING;
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

manapi::utils::json::json(const double long &num)
{
    this->parse(num);
}

manapi::utils::json::json(const bigint &num)
{
    this->parse(num);
}

manapi::utils::json::json(const bool &value)
{
    this->operator=(value);
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
    else if (data.size() == 1 && data.begin()->type != MANAPI_JSON_PAIR)
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
        auto value = it;

        // resolve pair to array
        if (value->type == MANAPI_JSON_PAIR)
        {
            auto first = new json (*reinterpret_cast <std::pair <const json *, const json *> *> (value->src)->first);
            auto second = new json (*reinterpret_cast <std::pair <const json *, const json *> *> (value->src)->second);

            auto arr = new json (json::array ());
            arr->push_back(*first);
            arr->push_back(*second);

            value = arr;
        }

        // do not need to malloc json
        src = new std::pair <const json *, const json *> (key, value);
        type = MANAPI_JSON_PAIR;
    }
    else {
        // array or map

        // if one element is pair -> all elements must be as pair
        bool map = true;

        for (const auto &it: data)
        {
            if (it.type != MANAPI_JSON_PAIR)
            {
                map = false;

                break;
            }
        }

        if (map)
        {
            type = MANAPI_JSON_MAP;
            src = new json::OBJECT ();
        }
        else {
            type = MANAPI_JSON_ARRAY;
            src = new json::ARRAY ();
        }

        if (map)
        {
            for (const auto & it : data)
            {
                auto *key = reinterpret_cast <std::string *> (reinterpret_cast <std::pair <const json *, const json *> *> (it.src)->first->src);
                auto value = new json (*reinterpret_cast <std::pair <const json *, const json *> *> (it.src)->second);

                reinterpret_cast <json::OBJECT *> (src)->insert({*key, value});
            }
        }
        else
        {
            for (auto it = data.begin(); it != data.end(); it++)
            {
                if (it->type == MANAPI_JSON_PAIR)
                {
                    // we need to resolve pair to array

                    auto first  = new json (*reinterpret_cast <std::pair <const json *, const json *> *> (it->src)->first);
                    auto second = new json (*reinterpret_cast <std::pair <const json *, const json *> *> (it->src)->second);

                    auto element = new json (json::array());

                    try
                    {
                        reinterpret_cast <json::ARRAY *> (element->src)->push_back(first);
                        reinterpret_cast <json::ARRAY *> (element->src)->push_back(second);

                        reinterpret_cast <json::ARRAY *> (src)->push_back(element);
                    }
                    catch (const json_parse_exception &e)
                    {
                        // bcz element will delete its elements
                        switch (element->size()) {
                            case 0:
                                // can not import first
                                delete first;
                            case 1:
                                // can not import second
                                delete second;
                        }

                        // clean
                        delete element;

                        throw json_parse_exception(e);
                    }
                }
                else
                {
                    auto element = new json(*it);

                    try
                    {
                        reinterpret_cast <json::ARRAY *> (src)->push_back(element);
                    }
                    catch (const json_parse_exception &e)
                    {
                        // clean
                        delete element;

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

void manapi::utils::json::parse(const ssize_t &num) {
    type    = MANAPI_JSON_NUMBER;
    src     = new ssize_t (num);
}

void manapi::utils::json::parse(const int &num) {
    type    = MANAPI_JSON_NUMBER;
    src     = new ssize_t (num);
}

void manapi::utils::json::parse(const double &num) {
    type    = MANAPI_JSON_DECIMAL;
    src     = new double long (num);
}

void manapi::utils::json::parse(const double long &num) {
    type    = MANAPI_JSON_DECIMAL;
    src     = new double long (num);
}

void manapi::utils::json::parse(const bigint &num) {
    type    = MANAPI_JSON_BIGINT;
    src     = new bigint(num);
}

void manapi::utils::json::parse(const std::string &plain_text) {
    this->parse(str4to32(plain_text));
}

void manapi::utils::json::parse(const nullptr_t &n) {
    type    = MANAPI_JSON_NULL;
    src     = nullptr;
}

void manapi::utils::json::parse(const std::u32string &plain_text, const bool &use_bigint, const size_t &bigint_precision, size_t start) {
    if (plain_text.empty())
        error_unexpected_end (0);

    start_cut       = start;
    end_cut         = plain_text.size() - 1;

    for (size_t i = start_cut; i <= end_cut; i++) {
        if (is_space_symbol(plain_text[i]))
        {
            continue;
        }

        switch (plain_text[i]) {
            case '{':
                type = MANAPI_JSON_MAP;
                break;
            case '[':
                type = MANAPI_JSON_ARRAY;
                break;
            case '"':
                type = MANAPI_JSON_STRING;
                break;
            default:
                type = MANAPI_JSON_NUMERIC;
                break;
        }

        start_cut = i;

        break;
    }

    std::u32string sub_plain_text;
    std::u32string key;
    bool opened_quote;

    switch (type) {
        case MANAPI_JSON_MAP:
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

                        auto *obj = new json();
                        obj->root = false;
                        obj->parse(plain_text, use_bigint, bigint_precision, i + 1);
                        reinterpret_cast<json::OBJECT *>(src)->insert({str32to4(key), obj});

                        i = obj->get_end_cut() + 1;

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

                        key = std::u32string();
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
        case MANAPI_JSON_ARRAY:
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

                auto *obj = new json ();
                obj->root = false;
                obj->parse(plain_text, use_bigint, bigint_precision, i);
                i = obj->get_end_cut() + 1;

                if (i > end_cut)
                {
                    error_unexpected_end(end_cut);
                }

                // append
                reinterpret_cast<json::ARRAY  *> (src)->push_back(obj);

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
        case MANAPI_JSON_STRING:
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
        case MANAPI_JSON_NUMERIC:

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
                type = MANAPI_JSON_NUMBER;

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
                                if (type == MANAPI_JSON_DECIMAL)
                                    error_invalid_char(plain_text, i);

                                // its decimal
                                type            = MANAPI_JSON_DECIMAL;
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

                    type    = MANAPI_JSON_BIGINT;
                }
                else {

                    if (type == MANAPI_JSON_NUMBER)
                        // numeric
                        src = new ssize_t(std::stoll(str32to4(sub_plain_text)));
                    else {
                        if (plain_text[end_cut] == '.')
                            error_invalid_char(plain_text, end_cut);
                        // decimal
                        src = new double long(std::stold(str32to4(sub_plain_text)));
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
                    type    = MANAPI_JSON_BOOLEAN;
                    break;
                }

                // false
                if (sub_plain_text == JSON_W_FALSE) {
                    src     = new bool (false);
                    type    = MANAPI_JSON_BOOLEAN;
                    break;
                }

                // null
                if (sub_plain_text == JSON_W_NULL) {
                    src     = nullptr;
                    type    = MANAPI_JSON_NULL;
                    break;
                }

                // otherwise -> null
                src         = nullptr;
                type        = MANAPI_JSON_NULL;

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
    this->parse (static_cast<ssize_t> (num));
}

std::string manapi::utils::json::dump(const size_t &spaces, const size_t &first_spaces) const {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) str += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else str += ' ';
#define JSON_DUMP_NEED_SPACES   for (size_t z = 0; z < total_spaces; z++) str += ' ';
#define JSON_DUMP_LAST_SPACES   for (size_t z = 0; z < first_spaces; z++) str += ' ';
    std::string str;

    if (type == MANAPI_JSON_STRING)
        // TODO: what the hell is that: str32to4(foo(str4to32(...)))
        str = '"' + str32to4(escape_string(str4to32(*reinterpret_cast<std::string *> (src)))) + '"';

    else if (type == MANAPI_JSON_DECIMAL)
        str = std::to_string(*reinterpret_cast<double long *> (src));

    else if (type == MANAPI_JSON_NUMBER)
        str = std::to_string(*reinterpret_cast<ssize_t *> (src));

    else if (type == MANAPI_JSON_BIGINT)
        str = reinterpret_cast <bigint *> (src)->stringify();

    else if (type == MANAPI_JSON_BOOLEAN)
        str = *reinterpret_cast<bool *> (src) == 1 ? JSON_TRUE : JSON_FALSE;

    else if (type == MANAPI_JSON_NULL)
        str = JSON_NULL;

    else if (type == MANAPI_JSON_MAP) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto map = reinterpret_cast<json::OBJECT *> (src);
        str += '{';

        JSON_DUMP_NEED_NEW_LINE

        if (!map->empty()) {
            auto it = map->begin();

            // first
            if (it != map->end()) {
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += str32to4(escape_string(str4to32(it->first))) + "\": " + it->second->dump(spaces, total_spaces);
                ++it;
            }

            // others
            for (; it != map->end(); ++it) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += str32to4(escape_string(str4to32(it->first))) + "\": " + it->second->dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE

        JSON_DUMP_LAST_SPACES

        str += '}';
    }

    else if (type == MANAPI_JSON_ARRAY) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto arr = reinterpret_cast<json::ARRAY  *> (src);
        str += '[';

        JSON_DUMP_NEED_NEW_LINE


        if (!arr->empty()) {
            // dump items
            size_t i = 0;

            // first
            JSON_DUMP_NEED_SPACES
            str += arr->at(i)->dump(spaces, total_spaces);

            i++;

            // others
            for (; i < arr->size(); i++) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += arr->at(i)->dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE
        JSON_DUMP_LAST_SPACES

        str += ']';
    }

    else if (type == MANAPI_JSON_PAIR)
    {
        THROW_MANAPI_EXCEPTION("A bug has been detected: {}", "type = MANAPI_JSON_PAIR");
    }

    return str;
}

size_t manapi::utils::json::get_start_cut() const {
    return start_cut;
}

size_t manapi::utils::json::get_end_cut() const {
    return end_cut;
}

void manapi::utils::json::error_invalid_char(const std::u32string &plain_text, const size_t &i) {
    throw json_parse_exception(std::format("Invalid char '{}' at {}", str32to4(plain_text[i]), i + 1));
}

void manapi::utils::json::error_unexpected_end(const size_t &i) {
    throw json_parse_exception(std::format("Unexpected end of JSON input at {}", i + 1));
}

void manapi::utils::json::delete_value() {
    delete_value_static(type, src);
}

manapi::utils::json &manapi::utils::json::operator[](const std::string &key) const {
    return at (key);
}

manapi::utils::json &manapi::utils::json::operator[](const size_t &index) const {
    return at (index);
}

manapi::utils::json &manapi::utils::json::at(const std::u32string &key) const {
    return at (str32to4(key));
}

manapi::utils::json &manapi::utils::json::at(const std::string &key) const {
    if (type != MANAPI_JSON_MAP)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto map = reinterpret_cast <json::OBJECT *> (src);

    if (!map->contains(key))
    {
        THROW_MANAPI_EXCEPTION("No such key. ({})", escape_string(key));
    }

    return *map->at(key);
}

manapi::utils::json &manapi::utils::json::at(const size_t &index) const {
    if (type != MANAPI_JSON_ARRAY)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto arr = reinterpret_cast <json::ARRAY  *> (src);

    if (arr->size() <= index)
    {
        THROW_MANAPI_EXCEPTION("Out of range. Index: {}. Size: {}", index, arr->size());
    }

    return *arr->at(index);
}

manapi::utils::json& manapi::utils::json::operator=(const std::string &str) {
    delete_value ();

    type    = MANAPI_JSON_STRING;
    src     = new std::string (str);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const bool &b) {
    delete_value();

    type    = MANAPI_JSON_BOOLEAN;
    src     = new bool (b);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const ssize_t &num) {
    delete_value();

    type    = MANAPI_JSON_NUMBER;
    src     = new ssize_t (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const double &num) {
    delete_value();

    type    = MANAPI_JSON_DECIMAL;
    src     = new double long (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const double long &num) {
    delete_value();

    type    = MANAPI_JSON_DECIMAL;
    src     = new double long (num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const long long &num) {
    return this->operator=((ssize_t) num);
}

manapi::utils::json &manapi::utils::json::operator=(nullptr_t const &n) {
    delete_value();

    type    = MANAPI_JSON_NULL;
    src     = nullptr;

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const std::u32string &str) {
    return this->operator=(str32to4(str));
}

manapi::utils::json &manapi::utils::json::operator=(const char *str) {
    return this->operator=(std::string(str));
}

manapi::utils::json &manapi::utils::json::operator=(const int &num) {
    return this->operator=(static_cast<ssize_t> (num));
}

manapi::utils::json &manapi::utils::json::operator=(const manapi::utils::bigint &num) {
    delete_value();

    type    = MANAPI_JSON_BIGINT;
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
            case MANAPI_JSON_STRING:
                src = new std::string (*reinterpret_cast<std::string *> (obj.src));
            break;
            case MANAPI_JSON_NUMBER:
                src = new ssize_t (*reinterpret_cast<ssize_t *> (obj.src));
            break;
            case MANAPI_JSON_DECIMAL:
                src = new double long (*reinterpret_cast<double long *> (obj.src));
            break;
            case MANAPI_JSON_BIGINT:
                src = new bigint(*reinterpret_cast<bigint *> (obj.src));
            break;
            case MANAPI_JSON_ARRAY:
            {
                const auto arr_oth = reinterpret_cast <json::ARRAY  *> (obj.src);

                src = new json::ARRAY (arr_oth->size());

                const auto arr = reinterpret_cast <json::ARRAY  *> (src);

                for (size_t i = 0; i < arr_oth->size(); i++) {
                    auto n_json     = new json ();
                    *n_json         = *arr_oth->at(i);
                    n_json->root    = false;

                    arr->at(i)      = n_json;
                }
                break;
            }
            case MANAPI_JSON_MAP:
            {
                const auto map_oth = reinterpret_cast <json::OBJECT  *> (obj.src);

                src = new json::OBJECT();

                const auto map = reinterpret_cast <json::OBJECT  *> (src);

                for (const auto &item: *map_oth) {
                    auto n_json     = new json;
                    *n_json         = *item.second;
                    n_json->root    = false;

                    map->insert({item.first, n_json});
                }
                break;
            }
            case MANAPI_JSON_BOOLEAN:
                src = new bool(*reinterpret_cast<bool *> (obj.src));
            break;
            case MANAPI_JSON_NULL:
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

void manapi::utils::json::insert(const std::u32string &key, const manapi::utils::json &obj) {
    insert (str32to4(key), obj);
}

void manapi::utils::json::insert(const std::string &key, const manapi::utils::json &obj) {
    if (type != MANAPI_JSON_MAP)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto map = reinterpret_cast <json::OBJECT *> (src);

    if (map->contains(key))
    {
        THROW_MANAPI_EXCEPTION("duplicate key: {}", escape_string(key));
    }

    auto item   = new json;
    *item       = obj;

    item->root  = false;

    map->insert({key, item});
}

void manapi::utils::json::push_back(const manapi::utils::json &obj) {
    if (type != MANAPI_JSON_ARRAY)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto arr = reinterpret_cast <json::ARRAY  *> (src);

    auto item   = new json;
    *item       = obj;

    item->root  = false;

    arr->push_back(item);
}

void manapi::utils::json::pop_back() {
    if (type != MANAPI_JSON_ARRAY)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    const auto arr = reinterpret_cast <json::ARRAY  *> (src);

    arr->pop_back();
}

void manapi::utils::json::insert(const std::u32string &key, const std::u32string &arg)
{
    this->insert (str32to4(key), str32to4(arg));
}

void manapi::utils::json::push_back(const std::u32string &arg) {
    push_back(str32to4(arg));
}

void manapi::utils::json::push_back(const std::string &arg) {
    json Json;

    Json.src        = new std::string (arg);
    Json.type       = MANAPI_JSON_STRING;

    this->push_back(Json);
}

void manapi::utils::json::push_back(const ssize_t &arg) {
    json Json (arg);
    this->push_back(Json);
}

manapi::utils::json manapi::utils::json::object() {
    json obj;

    obj.type    = MANAPI_JSON_MAP;
    obj.src     = new json::OBJECT;

    return obj;
}

manapi::utils::json manapi::utils::json::array() {
    // rvalue, arr not be destroyed
    json arr;

    arr.type    = MANAPI_JSON_ARRAY;
    arr.src     = new json::ARRAY ;

    return arr;
}

manapi::utils::json manapi::utils::json::array(const std::initializer_list<json> &data) {
    // rvalue, arr not be destroyed
    auto arr = json::array();

    for (const auto &it: data)
    {
        reinterpret_cast <json::ARRAY *> (arr.src)->push_back(new json (it));
    }

    return arr;
}

bool manapi::utils::json::contains(const std::u32string &key) const {
    return contains(str32to4(key));
}

bool manapi::utils::json::contains(const std::string &key) const {
    if (type != MANAPI_JSON_MAP)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    return reinterpret_cast <json::OBJECT *> (src)->contains(key);
}

void manapi::utils::json::erase(const std::u32string &key) {
    erase(str32to4(key));
}

void manapi::utils::json::erase(const std::string &key) {
    if (type != MANAPI_JSON_MAP)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    reinterpret_cast <json::OBJECT *> (src)->erase(key);
}

bool manapi::utils::json::is_object() const {
    return type == MANAPI_JSON_MAP;
}

bool manapi::utils::json::is_array() const {
    return type == MANAPI_JSON_ARRAY;
}

bool manapi::utils::json::is_string() const {
    return type == MANAPI_JSON_STRING;
}

bool manapi::utils::json::is_number() const {
    return type == MANAPI_JSON_NUMBER;
}

bool manapi::utils::json::is_null() const {
    return type == MANAPI_JSON_NULL;
}

bool manapi::utils::json::is_decimal() const {
    return type == MANAPI_JSON_DECIMAL;
}

bool manapi::utils::json::is_bigint() const {
    return type == MANAPI_JSON_BIGINT;
}

bool manapi::utils::json::is_bool() const {
    return type == MANAPI_JSON_BOOLEAN;
}

size_t manapi::utils::json::size() const {
    if (is_array())
    {
        return reinterpret_cast <json::ARRAY *> (src)->size();
    }
    else if (is_string())
    {
        return str4to32(*reinterpret_cast <std::string *> (src)).size();
    }
    else if (is_object())
    {
        return reinterpret_cast <json::OBJECT *> (src)->size();
    }

    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::utils::json &manapi::utils::json::operator+(const ssize_t &num) {
    if (is_bigint())
    {
        reinterpret_cast <bigint *> (src)->operator+(num);
    }
    else if (is_number())
    {
        (*reinterpret_cast <ssize_t *> (src)) += num;
    }
    else if (is_decimal())
    {
        (*reinterpret_cast <long double *> (src)) += num;
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

manapi::utils::json &manapi::utils::json::operator-(const ssize_t &num) {
    return this->operator+(-num);
}

manapi::utils::json &manapi::utils::json::operator-(const int &num) {
    return this->operator+(-num);
}

void manapi::utils::json::delete_value_static(const short &type, void *src) {
    switch (type) {
        case MANAPI_JSON_NULL:
            break;
        case MANAPI_JSON_ARRAY:
            json::ARRAY *arr;

            arr = reinterpret_cast<json::ARRAY  *> (src);

            for (const auto &item: *arr)
            {
                delete item;
            }

            delete arr;

            break;
        case MANAPI_JSON_MAP:
            json::OBJECT* map;

            map = reinterpret_cast<json::OBJECT *> (src);
            for (const auto &child: *map)
            {
                delete child.second;
            }
            delete map;

            break;
        case MANAPI_JSON_BOOLEAN:
            delete reinterpret_cast<bool *> (src);
            break;
        case MANAPI_JSON_NUMBER:
            delete reinterpret_cast<ssize_t *> (src);
            break;
        case MANAPI_JSON_STRING:
            delete reinterpret_cast<std::string *> (src);
            break;
        case MANAPI_JSON_DECIMAL:
            delete reinterpret_cast<double long*> (src);
            break;
        case MANAPI_JSON_BIGINT:
            delete reinterpret_cast<manapi::utils::bigint *> (src);
            break;
        case MANAPI_JSON_PAIR:
            delete reinterpret_cast<std::pair <const json *, const json *> *> (src);
            break;

    }
}

void manapi::utils::json::throw_could_not_use_func(const std::string &func) const
{
    THROW_MANAPI_EXCEPTION("json object with type {} could not use func: {}", type, func);
}


// Exceptions

manapi::utils::json_parse_exception::json_parse_exception(const std::string &msg) {
    message = msg;
}

const char *manapi::utils::json_parse_exception::what() const noexcept {
    return message.data();
}