#include <memory.h>
#include <format>
#include "ManapiJson.h"
#include "ManapiUtils.h"
#include "ManapiDecimal.h"

const static std::string JSON_TRUE   = "true";
const static std::string JSON_FALSE  = "false";
const static std::string JSON_NULL   = "null";

typedef std::map<std::string, manapi::utils::json *>    MANAPI_JSON_TYPEDEF_MAP;
typedef std::vector<manapi::utils::json *>              MANAPI_JSON_TYPEDEF_ARR;

manapi::utils::json::json() = default;

manapi::utils::json::json(const std::string &plain_text) {
    this->parse(plain_text);
}

manapi::utils::json::json(const ssize_t &num) {
    this->parse(num);
}

manapi::utils::json::json(const manapi::utils::json &other) {
    *this = other;
}

manapi::utils::json::~json() {
    delete_value();
}

void manapi::utils::json::parse(const ssize_t &num) {
    type    = MANAPI_JSON_NUMBER;
    src     = new ssize_t (num);
}

void manapi::utils::json::parse(const std::string &plain_text, const bool &bigint, const size_t &bigint_precision, size_t start) {
    if (plain_text.empty())
        error_unexpected_end (0);

    use_bigint      = bigint;

    start_cut       = start;
    end_cut         = plain_text.size() - 1;

    for (size_t i = start_cut; i <= end_cut; i++) {
        if (is_space_symbol(plain_text[i]))
            continue;

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
                type = MANAPI_JSON_NUMBER;
                break;
        }

        start_cut = i;

        break;
    }

    std::string sub_plain_text;
    std::string key;
    bool opened_quote;

    switch (type) {
        case MANAPI_JSON_MAP:
            src = new MANAPI_JSON_TYPEDEF_MAP ();

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
                        obj->parse(plain_text, bigint, bigint_precision, i + 1);
                        reinterpret_cast<MANAPI_JSON_TYPEDEF_MAP *>(src)->insert({key, obj});

                        i = obj->get_end_cut() + 1;

                        if (i > end_cut)
                            error_invalid_char(plain_text, end_cut);

                        // skip to ',' or '}'

                        for (; i <= end_cut; i++) {
                            if (is_space_symbol(plain_text[i]))
                                continue;

                            else if (plain_text[i] == '}') {
                                end_cut = i;
                                break;
                            }

                            else if (plain_text[i] == ',')
                                break;

                            else
                                error_invalid_char(plain_text, i);
                        }

                        key = "";
                    }
                    else if (plain_text[i] == '}') {
                        if (!key.empty())
                            error_invalid_char(plain_text, i);

                        end_cut = i;
                        break;
                    } else {
                        if (is_space_symbol(plain_text[i]) || plain_text[i] == '{')
                            continue;

                        error_invalid_char(plain_text, i);
                    }
                }
            }

            if (plain_text[end_cut] != '}')
                error_invalid_char(plain_text, end_cut);

            break;
        case MANAPI_JSON_ARRAY:
            src = new std::vector<json *> ();

            // bcz we know that is array -> skip first char ('[')

            for (size_t i = start_cut + 1; i <= end_cut; i++) {
                if (plain_text[i] == ']') {
                    // the end
                    end_cut = i;
                    break;
                }

                auto *obj = new json ();
                obj->root = false;
                obj->parse(plain_text, bigint, bigint_precision, i);
                i = obj->get_end_cut() + 1;

                if (i > end_cut)
                    error_unexpected_end(end_cut);

                // append
                reinterpret_cast<MANAPI_JSON_TYPEDEF_ARR  *> (src)->push_back(obj);

                // skip all chars to ','
                for (; i <= end_cut; i++) {
                    if (is_space_symbol(plain_text[i]))
                        continue;

                    else if (plain_text[i] == ',')
                        break;

                    else if (plain_text[i] == ']') {
                        // the end
                        end_cut = i;
                        break;
                    }

                    else
                        error_invalid_char(plain_text, i);
                }
            }

            if (plain_text[end_cut] != ']')
                error_unexpected_end(end_cut);

            break;
        case MANAPI_JSON_STRING:
            opened_quote = false;

            for (size_t i = start_cut; i <= end_cut; i++) {
                if (plain_text[i] == '\\') {
                    i++;
                    if (i > end_cut)
                        error_unexpected_end(end_cut);

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

            src = new std::string(sub_plain_text);

            break;
        case MANAPI_JSON_NUMBER:

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
                type = MANAPI_JSON_NUMERIC;

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

                if (bigint) {
                    // bigint for all type of number
                    src     = new decimal(sub_plain_text, bigint_precision);

                    type    = MANAPI_JSON_BIGINT;
                }
                else {

                    if (type == MANAPI_JSON_NUMERIC)
                        // numeric
                        src = new ssize_t(std::stoll(sub_plain_text));
                    else {
                        if (plain_text[end_cut] == '.')
                            error_invalid_char(plain_text, end_cut);
                        // decimal
                        src = new double(std::stold(sub_plain_text));
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
                if (sub_plain_text == JSON_TRUE) {
                    src     = new bool (true);
                    type    = MANAPI_JSON_BOOLEAN;
                    break;
                }

                // false
                if (sub_plain_text == JSON_FALSE) {
                    src     = new bool (false);
                    type    = MANAPI_JSON_BOOLEAN;
                    break;
                }

                // null
                if (sub_plain_text == JSON_NULL) {
                    src     = nullptr;
                    type    = MANAPI_JSON_NULL;
                    break;
                }

                // otherwise -> null
                src         = nullptr;
                type        = MANAPI_JSON_NULL;

                // TODO escape sub_plain_text
                throw json_parse_exception (std::format("Invalid string: '{}' ({}, {})", sub_plain_text, start_cut + 1, end_cut + 1));
            }

            break;
    }

    if (root) {
        for (size_t i = end_cut + 1; i < plain_text.size(); i++) {
            if (is_space_symbol(plain_text[i]))
                continue;

            error_invalid_char(plain_text, i);
        }
    }
}

std::string manapi::utils::json::dump(const size_t &spaces, const size_t &first_spaces) {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) str += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else str += ' ';
#define JSON_DUMP_NEED_SPACES   for (size_t z = 0; z < total_spaces; z++) str += ' ';
#define JSON_DUMP_LAST_SPACES   for (size_t z = 0; z < first_spaces; z++) str += ' ';
    std::string str;

    if (type == MANAPI_JSON_STRING)
        str = '"' + *reinterpret_cast<std::string *> (src) + '"';

    else if (type == MANAPI_JSON_DECIMAL)
        str = std::to_string(*reinterpret_cast<double *> (src));

    else if (type == MANAPI_JSON_NUMERIC)
        str = std::to_string(*reinterpret_cast<ssize_t *> (src));

    else if (type == MANAPI_JSON_BIGINT)
        str = reinterpret_cast <decimal *> (src)->stringify();

    else if (type == MANAPI_JSON_BOOLEAN)
        str = *reinterpret_cast<bool *> (src) == 1 ? JSON_TRUE : JSON_FALSE;

    else if (type == MANAPI_JSON_NULL)
        str = JSON_NULL;

    else if (type == MANAPI_JSON_MAP) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto map = reinterpret_cast<MANAPI_JSON_TYPEDEF_MAP *> (src);
        str += '{';

        JSON_DUMP_NEED_NEW_LINE

        if (!map->empty()) {
            auto it = map->begin();

            // first
            if (it != map->end()) {
                JSON_DUMP_NEED_SPACES

                str += '"' + it->first + "\": " + it->second->dump(spaces, total_spaces);
                ++it;
            }

            // others
            for (; it != map->end(); ++it) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += "\"" + it->first + "\": " + it->second->dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE

        JSON_DUMP_LAST_SPACES

        str += '}';
    }

    else if (type == MANAPI_JSON_ARRAY) {
        const bool      spaces_enabled  = spaces > 0;
        const size_t    total_spaces    = first_spaces + spaces;

        auto arr = reinterpret_cast<MANAPI_JSON_TYPEDEF_ARR  *> (src);
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

    return str;
}

size_t manapi::utils::json::get_start_cut() const {
    return start_cut;
}

size_t manapi::utils::json::get_end_cut() const {
    return end_cut;
}

void manapi::utils::json::error_invalid_char(const std::string &plain_text, const size_t &i) {
    throw json_parse_exception(std::format("Invalid char '{}' at {}", plain_text[i], i + 1));
}

void manapi::utils::json::error_unexpected_end(const size_t &i) {
    throw json_parse_exception(std::format("Unexpected end of JSON input at {}", i + 1));
}

void manapi::utils::json::delete_value() {
    switch (type) {
        case MANAPI_JSON_NULL:
            break;
        case MANAPI_JSON_ARRAY:
            MANAPI_JSON_TYPEDEF_ARR *arr;

            arr = reinterpret_cast<MANAPI_JSON_TYPEDEF_ARR  *> (src);

            for (const auto &item: *arr)
                delete item;

            delete arr;

            break;
        case MANAPI_JSON_MAP:
            MANAPI_JSON_TYPEDEF_MAP* map;

            map = reinterpret_cast<MANAPI_JSON_TYPEDEF_MAP *> (src);
            for (const auto &child: *map) {
                delete child.second;
            }
            delete map;

            break;
        case MANAPI_JSON_BOOLEAN:
            delete reinterpret_cast<bool *>             (src);
            break;
        case MANAPI_JSON_NUMERIC:
            delete reinterpret_cast<ssize_t *>    (src);
            break;
    }
}

manapi::utils::json &manapi::utils::json::operator[](const std::string &key) const {
    return at (key);
}

manapi::utils::json &manapi::utils::json::operator[](const size_t &index) const {
    return at (index);
}

manapi::utils::json &manapi::utils::json::at(const std::string &key) const {
    if (type != MANAPI_JSON_MAP)
        throw manapi_exception ("invalid operator");

    const auto map = reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP *> (src);

    if (!map->contains(key))
        throw manapi_exception ("no such key");

    return *map->at(key);
}

manapi::utils::json &manapi::utils::json::at(const size_t &index) const {
    if (type != MANAPI_JSON_ARRAY)
        throw manapi_exception ("invalid operator");

    const auto arr = reinterpret_cast <MANAPI_JSON_TYPEDEF_ARR  *> (src);

    if (arr->size() <= index)
        throw manapi_exception ("out of range");

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

    if (use_bigint) {
        type    = MANAPI_JSON_BIGINT;
        src     = new decimal(num, 64);
    }
    else {
        type    = MANAPI_JSON_NUMERIC;
        src     = new ssize_t (num);
    }

    return *this;
}


manapi::utils::json &manapi::utils::json::operator=(const double &num) {
    delete_value();

    if (use_bigint) {
        type    = MANAPI_JSON_BIGINT;
        src     = new decimal(num, 128);
    }
    else {
        type    = MANAPI_JSON_DECIMAL;
        src     = new double (num);
    }

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(nullptr_t const &n) {
    delete_value();

    type    = MANAPI_JSON_NULL;
    src     = nullptr;

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const char *str) {
    return this->operator=(std::string (str));
}

manapi::utils::json &manapi::utils::json::operator=(const int &num) {
    return this->operator=((ssize_t) num);
}

manapi::utils::json &manapi::utils::json::operator=(const manapi::utils::decimal &num) {
    delete_value();

    type    = MANAPI_JSON_BIGINT;
    src     = new decimal(num);

    return *this;
}

manapi::utils::json &manapi::utils::json::operator=(const manapi::utils::json &obj) {
    delete_value();

    this->use_bigint            = obj.use_bigint;
    this->start_cut             = 0;
    this->type                  = obj.type;
    this->root                  = true;

    switch (type) {
        case MANAPI_JSON_STRING:
            src = new std::string (*reinterpret_cast<std::string *> (obj.src));
            break;
        case MANAPI_JSON_NUMERIC:
            src = new ssize_t (*reinterpret_cast<ssize_t *> (obj.src));
            break;
        case MANAPI_JSON_DECIMAL:
            src = new double long (*reinterpret_cast<double long *> (obj.src));
            break;
        case MANAPI_JSON_BIGINT:
            src = new decimal(*reinterpret_cast<decimal *> (obj.src));
            break;
        case MANAPI_JSON_ARRAY:
            const MANAPI_JSON_TYPEDEF_ARR* arr_oth;
            arr_oth = reinterpret_cast <MANAPI_JSON_TYPEDEF_ARR  *> (obj.src);

            src = new MANAPI_JSON_TYPEDEF_ARR (arr_oth->size());

            MANAPI_JSON_TYPEDEF_ARR  * arr;
            arr = reinterpret_cast <MANAPI_JSON_TYPEDEF_ARR  *> (src);

            for (size_t i = 0; i < arr_oth->size(); i++) {
                auto n_json     = new json;
                *n_json         = *arr_oth->at(i);

                arr->at(i)  = n_json;
            }
            break;
        case MANAPI_JSON_MAP:
            const MANAPI_JSON_TYPEDEF_MAP * map_oth;
            map_oth = reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP  *> (obj.src);

            src = new MANAPI_JSON_TYPEDEF_MAP();

            MANAPI_JSON_TYPEDEF_MAP  * map;
            map = reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP  *> (src);

            for (const auto &item: *map_oth) {
                auto n_json = new json;
                *n_json     = *item.second;

                map->insert({item.first, n_json});
            }
            break;
        case MANAPI_JSON_BOOLEAN:
            src = new bool(*reinterpret_cast<bool *> (obj.src));
            break;
        case MANAPI_JSON_NULL:
            src = nullptr;
            break;
    }


    return *this;
}

void manapi::utils::json::insert(const std::string &key, const manapi::utils::json &obj) {
    if (type != MANAPI_JSON_MAP)
        throw manapi_exception ("invalid type");

    auto map = reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP *> (src);

    if (map->contains(key))
        throw manapi_exception ("duplicate key");

    auto item   = new json;
    *item       = obj;

    item->root  = false;

    map->insert({key, item});
}

void manapi::utils::json::push_back(const manapi::utils::json &obj) {
    if (type != MANAPI_JSON_ARRAY)
        throw manapi_exception ("invalid type");

    const auto arr = reinterpret_cast <MANAPI_JSON_TYPEDEF_ARR  *> (src);

    auto item   = new json;
    *item       = obj;

    item->root  = false;

    arr->push_back(item);
}

void manapi::utils::json::pop_back() {
    if (type != MANAPI_JSON_ARRAY)
        throw manapi_exception ("invalid type");

    const auto arr = reinterpret_cast <MANAPI_JSON_TYPEDEF_ARR  *> (src);

    arr->pop_back();
}

void manapi::utils::json::insert(const std::string &key, const std::string &arg) {
    json Json;

    Json.src        = new std::string (arg);
    Json.type       = MANAPI_JSON_STRING;
    Json.use_bigint = use_bigint;

    this->insert(key, Json);
}

void manapi::utils::json::insert(const std::string &key, const ssize_t &arg) {
    json Json (arg);
    this->insert(key, Json);
}

void manapi::utils::json::push_back(const std::string &arg) {
    json Json;

    Json.src        = new std::string (arg);
    Json.type       = MANAPI_JSON_STRING;
    Json.use_bigint = use_bigint;

    this->push_back(Json);
}

void manapi::utils::json::push_back(const ssize_t &arg) {
    json Json (arg);
    this->push_back(Json);
}

manapi::utils::json manapi::utils::json::object() {
    json obj;

    obj.type    = MANAPI_JSON_MAP;
    obj.src     = new MANAPI_JSON_TYPEDEF_MAP;

    return obj;
}

manapi::utils::json manapi::utils::json::array() {
    json arr;

    arr.type    = MANAPI_JSON_ARRAY;
    arr.src     = new MANAPI_JSON_TYPEDEF_ARR ;

    return arr;
}

bool manapi::utils::json::contains(const std::string &key) {
    if (type != MANAPI_JSON_MAP)
        throw manapi_exception ("invalid type");

    return reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP *> (src)->contains(key);
}

void manapi::utils::json::erase(const std::string &key) {
    if (type != MANAPI_JSON_MAP)
        throw manapi_exception ("invalid type");

    reinterpret_cast <MANAPI_JSON_TYPEDEF_MAP *> (src)->erase(key);
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

size_t manapi::utils::json::size() {
    if (!is_array())
        throw manapi_exception ("its not array to use size()");

    return reinterpret_cast <std::vector <json *> *> (src)->size();
}


// Exceptions

manapi::utils::json_parse_exception::json_parse_exception(const std::string &msg) {
    message = msg;
}

const char *manapi::utils::json_parse_exception::what() const noexcept {
    return message.data();
}


