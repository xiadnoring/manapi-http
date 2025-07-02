#include <memory.h>
#include <format>
#include <utility>

#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiDebug.hpp"

#include "ManapiBeforeDelete.hpp"
#include "ManapiBigint.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "ManapiJsonBuilder.hpp"
#include "include/ManapiJsonMaskUtils.hpp"

constexpr char JSON_TRUE[] = "true";
constexpr char JSON_FALSE[] = "false";
constexpr char JSON_NULL[] = "null";

#define RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) manapi::json_parse_exception (errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_MISSING_FUNCTION throw RETHROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNSUPPORTED_TYPE, "json object with type {}({}) could not use func: {}", \
    json_type_to_str(this->type), static_cast <int> (this->type), __FUNCTION__)
#define THROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) throw RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, __VA_ARGS__)

manapi::json::json() = default;

manapi::json::json(STRING_VIEW str, bool parse) {
    if (parse)
    {
        this->_parse(str);
    }
    else
    {
        json_builder::_valid_utf_string(str);
        _set_string(str);
    }
}

manapi::json::json(STRING str) {
    json_builder::_valid_utf_string(str);
    _set_string(std::move(str));
}

manapi::json::json(const UNICODE_STRING &str, bool parse) {
    if (parse)
    {
        this->_parse(str);
    }
    else
    {
        _set_string(unicode::str32to4(str));
    }
}

manapi::json::json(INTEGER num) {
    this->_parse(num);
}

manapi::json::json(const manapi::json &other) {
    *this = other;
}

manapi::json::json(json &&other) noexcept {
    if (&other != this) {
        this->delete_value();

        this->src = std::exchange(other.src, nullptr);
        this->type = std::exchange(other.type, types::type_null);

        _debug_symb_reinit();
    }
}

manapi::json::json(const char *plain_text, bool parse)
{
    if (parse)
    {
        this->_parse(STRING_VIEW (plain_text));
    }
    else
    {
        _set_string(STRING_VIEW{plain_text});
    }
}

manapi::json::json(DECIMAL num)
{
    this->_parse(num);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::json(BIGINT num)
{
    this->_parse(std::move(num));
}
#endif

manapi::json::json(BOOLEAN value)
{
    this->_parse(value);
}

manapi::json::json(OBJECT obj) {
    this->_parse(std::move(obj));
}

manapi::json::json(ARRAY arr) {
    this->_parse(std::move(arr));
}

manapi::json::json(const nullptr_t &n)
{
    this->_parse(n);
}

manapi::json::json (const std::initializer_list<json> &data) {
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
            arr.push_back(static_cast <PAIR *> (value.src)->first);
            arr.push_back(static_cast <PAIR *> (value.src)->second);

            value = arr;
        }

        // do not need to malloc json
        _set_pair(*key, std::move(value));
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
            *this = json::object(data);
        }
        else
        {
            *this = json::array(data);
        }
    }
}

manapi::json::~json() {
    delete_value();
}

void manapi::json::_parse(INTEGER num) {
    _set_integer(num);
}

void manapi::json::_parse(int num) {
    this->_parse(static_cast<INTEGER> (num));
}

void manapi::json::_parse(double num) {
    this->_parse(static_cast<DECIMAL> (num));
}

void manapi::json::_parse(DECIMAL num) {
    _set_decimal(num);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::_parse(BIGINT num) {
    _set_bigint(std::move(num));
}
#endif

void manapi::json::_parse(OBJECT obj) {
    _set_object(std::move(obj));
}

void manapi::json::_parse(ARRAY arr) {
    _set_array(std::move(arr));
}

void manapi::json::_parse(BOOLEAN val) {
    _set_bool(val);
}

void manapi::json::_parse(const UNICODE_STRING &plain_text) {
    this->_parse(unicode::str32to4(plain_text));
}

void manapi::json::_parse(const NULLPTR &n) {
    _set_nullptr();
}
#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::_parse(STRING_VIEW plain_text, bool use_bigint, size_t bigint_precision) {
    json_builder builder (json_mask(nullptr), use_bigint, bigint_precision);
    auto res = builder.parse(plain_text);
    if (!res.ok())
        res.unwrap();
    auto rhs = builder.get();
    if (!rhs.ok())
        rhs.unwrap();
    *this = std::move(rhs.value());
}
#else
void manapi::json::_parse(STRING_VIEW plain_text) {
    json_builder builder (json_mask(nullptr));
    auto res = builder.parse(plain_text);
    if (!res.ok())
        res.unwrap();
    auto rhs = builder.get();
    if (!rhs.ok())
        rhs.unwrap();
    *this = std::move(rhs.value());
}
#endif

void manapi::json::_parse(size_t num) {
    this->_parse (static_cast<INTEGER> (num));
}

std::string manapi::json::dump(int spaces, int first_spaces) const {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) str += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else str += ' ';
#define JSON_DUMP_NEED_SPACES   for (int z = 0; z < total_spaces; z++) str += ' ';
#define JSON_DUMP_LAST_SPACES   for (int z = 0; z < first_spaces; z++) str += ' ';
    std::string str;

    if (type == type_string)
    {
        str = '"' + unicode::escape_string(as_string()) + '"';
    }

    else if (type == type_decimal)
    {
        str = std::to_string(as_decimal());
    }

    else if (type == type_integer)
    {
        str = std::to_string(as_integer());
    }

#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (type == type_bigint)
    {
        str = '"' + as_bigint().stringify() + '"';
    }
#endif
    else if (type == type_boolean)
    {
        str = as_bool() ? JSON_TRUE : JSON_FALSE;
    }

    else if (type == type_null)
    {
        str = JSON_NULL;
    }

    else if (type == type_object) {
        const bool spaces_enabled  = spaces > 0;
        const int total_spaces = first_spaces + spaces;

        auto map = as_object();
        str += '{';

        JSON_DUMP_NEED_NEW_LINE

        if (!map.empty()) {
            auto it = map.begin();

            // first
            if (it != map.end()) {
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += unicode::escape_string(it->first) + "\": " + it->second.dump(spaces, total_spaces);
                ++it;
            }

            // others
            for (; it != map.end(); ++it) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += unicode::escape_string(it->first) + "\": " + it->second.dump(spaces, total_spaces);
            }
        }

        JSON_DUMP_NEED_NEW_LINE

        JSON_DUMP_LAST_SPACES

        str += '}';
    }

    else if (type == type_array) {
        const bool spaces_enabled = spaces > 0;
        const int total_spaces = first_spaces + spaces;

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
        THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_BUG, "Bug has been deteceted: {}", "type = type_pair");
    }

    return str;
}

void manapi::json::error_invalid_char(const UNICODE_STRING &plain_text, size_t i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", unicode::str32to4(plain_text[i]), i + 1);
}

void manapi::json::error_invalid_char(const STRING_VIEW &plain_text, size_t i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", plain_text[i], i + 1);
}

void manapi::json::error_unexpected_end(size_t i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNEXPECTED_END, "Unexpected end of JSON input at {}", i + 1);
}

void manapi::json::delete_value() {
    delete_value_static(
        std::exchange(this->type, types::type_null), std::exchange(this->src, nullptr));
}

void manapi::json::_set_object() {
    delete_value_static(this->type, std::exchange(this->src, new OBJECT ()));
    this->type = types::type_object;
    _debug_symb_reinit();
}

void manapi::json::_set_bool() {
    delete_value_static(this->type, std::exchange(this->src, new BOOLEAN ()));
    this->type = types::type_boolean;
    _debug_symb_reinit();
}

void manapi::json::_set_array() {
    delete_value_static(this->type, std::exchange(this->src, new ARRAY ()));
    this->type = types::type_array;
    _debug_symb_reinit();
}

void manapi::json::_set_string() {
    delete_value_static(this->type, std::exchange(this->src, new STRING ()));
    this->type = types::type_string;
    _debug_symb_reinit();
}

void manapi::json::_set_integer() {
    delete_value_static(this->type, std::exchange(this->src, new INTEGER ()));
    this->type = types::type_integer;
    _debug_symb_reinit();
}

void manapi::json::_set_decimal() {
    delete_value_static(this->type, std::exchange(this->src, new DECIMAL ()));
    this->type = types::type_decimal;
    _debug_symb_reinit();
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::_set_bigint() {
    delete_value_static(this->type, std::exchange(this->src, new BIGINT ()));
    this->type = types::type_bigint;
    _debug_symb_reinit();
}
#endif

void manapi::json::_set_nullptr() {
    delete_value_static(this->type, std::exchange(this->src, nullptr));
    this->type = types::type_null;
    _debug_symb_reinit();
}

void manapi::json::_set_pair() {
    delete_value_static(this->type, std::exchange(this->src, new PAIR ()));
    this->type = types::type_pair;
    _debug_symb_reinit();
}

void manapi::json::_set_object(OBJECT val) {
    _set_object();
    as_object() = std::move(val);
}

void manapi::json::_set_bool(BOOLEAN val) {
    _set_bool();
    as_bool() = val;
}

void manapi::json::_set_array(ARRAY val) {
    _set_array();
    as_array() = std::move(val);
}

void manapi::json::_set_string(STRING val) {
    _set_string();
    as_string() = std::move(val);
}

void manapi::json::_set_string(STRING_VIEW val) {
    _set_string();
    as_string() = val;
}

void manapi::json::_set_integer(INTEGER val) {
    _set_integer();
    as_integer() = val;
}

void manapi::json::_set_decimal(DECIMAL val) {
    _set_decimal();
    as_decimal() = val;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::_set_bigint(BIGINT val) {
    _set_bigint();
    as_bigint() = std::move(val);
}
#endif

void manapi::json::_set_pair(json first, json second) {
    _set_pair();

    _as_pair().first = std::move(first);
    _as_pair().second = std::move(second);
}

manapi::json &manapi::json::operator[](const STRING &key) {
    return this->at(key);
}

manapi::json &manapi::json::operator[](const UNICODE_STRING &key) {
    return this->at(key);
}

manapi::json &manapi::json::operator[](size_t index) {
    return this->at(index);
}

const manapi::json & manapi::json::operator[](const STRING &key) const {
    return this->at(key);
}

const manapi::json & manapi::json::operator[](const UNICODE_STRING &key) const {
    return this->at(key);
}

const manapi::json & manapi::json::operator[](size_t index) const {
    return this->at(index);
}

manapi::json &manapi::json::at(const UNICODE_STRING &key)  {
    return this->at(unicode::str32to4(key));
}

manapi::json &manapi::json::at(const std::string &key)  {
    if (type != type_object)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &map = this->as_object();

    // if (!map.contains(key))
    // {
    //     THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));
    // }

    return map[key];
}

manapi::json &manapi::json::at(size_t index)  {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->as_array();

    if (arr.size() <= index)
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

const manapi::json & manapi::json::at(const std::string &key) const {
    if (type != type_object)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &map = this->as_object();

    if (!map.contains(key))
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));
    }

    return map.at(key);
}

const manapi::json & manapi::json::at(const UNICODE_STRING &key) const {
    return this->at(unicode::str32to4(key));
}

const manapi::json & manapi::json::at(size_t index) const {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->as_array();

    if (arr.size() <= index)
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

manapi::json& manapi::json::operator=(STRING str) {
    _set_string(std::move(str));

    return *this;
}

manapi::json &manapi::json::operator=(BOOLEAN b) {
    _set_bool(b);

    return *this;
}

manapi::json &manapi::json::operator=(INTEGER num) {
    _set_integer(num);

    return *this;
}

manapi::json &manapi::json::operator=(DECIMAL num) {
    _set_decimal(num);

    return *this;
}

// manapi::json &manapi::json::operator=(const long long &num) {
//     return this->operator=(static_cast<INTEGER> (num));
// }

manapi::json &manapi::json::operator=(nullptr_t const &n) {
    _set_nullptr();

    return *this;
}

manapi::json &manapi::json::operator=(const UNICODE_STRING &str) {
    this->operator=(unicode::str32to4(str));
    return *this;
}

manapi::json &manapi::json::operator=(const char *str) {
    this->operator=(STRING(str));
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator=(BIGINT num) {
    _set_bigint(std::move(num));

    return *this;
}
#endif

manapi::json &manapi::json::operator=(const manapi::json &obj) {
    if (&obj != this)
    {
        this->root                  = true;

        switch (obj.type) {
            case type_string:
                _set_string(obj.as_string());
                break;
            case type_integer:
                _set_integer(obj.as_integer());
                break;
            case type_decimal:
                _set_decimal(obj.as_decimal());
                break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case type_bigint:
                _set_bigint(obj.as_bigint());
                break;
#endif
            case type_array:
            {
                _set_array(obj.as_array());
                break;
            }
            case type_object:
            {
                _set_object(obj.as_object());
                break;
            }
            case type_boolean:
                _set_bool(obj.as_bool());
                break;
            case type_null:
                _set_nullptr();
                break;
        }
    }
    return *this;
}

manapi::json & manapi::json::operator=(json &&obj) noexcept {
    if (&obj != this) {

        std::swap(obj.src, this->src);
        std::swap(obj.type, this->type);

        obj.delete_value();

        obj._debug_symb_reinit();
        _debug_symb_reinit();
    }

    return *this;
}

manapi::json &manapi::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::json (data);
}

manapi::json manapi::json::operator*(INTEGER num) const {
    auto n = *this;
    if (n.is_integer())
    {
        n.as_integer() *= num;
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
    {
        n.as_bigint() *= num;
    }
#endif
    else if (n.is_decimal())
    {
        n.as_decimal() *= num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator*(DECIMAL num) const {
    auto n = *this;

    if (n.is_integer())
    {
        n.as_integer() += static_cast<INTEGER>(num);
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
    {
        n.as_bigint() += num;
    }
#endif
    else if (n.is_decimal())
    {
        n.as_decimal() += num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json manapi::json::operator*(const BIGINT &num) const {
    auto n = *this;

    if (n.is_integer())
    {
        n.as_integer() += num.integerify();
    }
    else if (n.is_bigint())
    {
        n.as_bigint() += num;
    }
    else if (n.is_decimal())
    {
        n.as_decimal() += num.decimalify();
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}
#endif

manapi::json &manapi::json::operator*=(INTEGER num) {
    if (this->is_integer())
        { this->as_integer() += num; }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        { this->as_bigint() += num; }
#endif
    else if (this->is_decimal())
        { this->as_decimal() += num; }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}

manapi::json &manapi::json::operator*=(DECIMAL num) {
    if (this->is_integer())
        { this->as_integer() += static_cast<INTEGER>(num); }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        { this->as_bigint() += num; }
#endif
    else if (this->is_decimal())
        { this->as_decimal() += num; }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator*=(const BIGINT &num) {
    if (this->is_integer())
        { this->as_integer() += num.integerify(); }
    else if (this->is_bigint())
        { this->as_bigint() += num; }
    else if (this->is_decimal())
        { this->as_decimal() += num.decimalify(); }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}
#endif

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const UNICODE_STRING &key, manapi::json obj) {
    return this->insert ({unicode::str32to4(key), std::move(obj)});
}

std::pair<std::map<std::string, manapi::json>::iterator, bool> manapi::json::insert(const OBJECT::value_type &v) {
    return this->insert ({v.first, v.second});
}

std::pair<std::map<std::string, manapi::json>::iterator, bool> manapi::json::insert(OBJECT::value_type &&v) {
    if (type != type_object) {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    v.second.root=false;

    return as_object().insert(std::forward<decltype(v)>(v));
}

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const STRING &key, manapi::json obj) {
    return this->insert ({key, obj});
}

void manapi::json::push_back(manapi::json::ARRAY::const_iterator begin, manapi::json::ARRAY::const_iterator end) {
    if (type != type_array) { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    for (auto it = begin; it != end; ++it) {
        push_back(*it);
    }
}

void manapi::json::push_back(manapi::json obj) {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    obj.root = false;

    as_array().push_back(std::move(obj));
}

void manapi::json::pop_back() {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    as_array().pop_back();
}

int manapi::json::data_type() const {
    return this->type;
}

manapi::json manapi::json::object() {
    json obj;

    obj._set_object();

    return std::move(obj);
}

manapi::json manapi::json::array() {
    // rvalue, arr not be destroyed
    json arr;

    arr._set_array();

    return std::move(arr);
}

manapi::json manapi::json::object(const std::initializer_list<json> &data) {
    auto obj = json::object();

    if (data.size() == 1
        && data.begin()->is_object()) {
        obj = *data.begin();
    }
    else {
        for (const auto & it : data)
        {
            if (it.type == manapi::json::type_pair) {
                auto &key = static_cast <PAIR *> (it.src)->first.as_string();
                json value = static_cast <PAIR *> (it.src)->second;
                obj.as_object().insert({key, std::move(value)});
            }
            else {
                THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNSUPPORTED_TYPE,
                    "a pair and an object are supported for processing in json::object(...), but got {}({}) type", json_type_to_str(it.type), static_cast<int>(it.type));
            }
        }
    }

    return std::move(obj);
}

manapi::json manapi::json::array(manapi::json data) {
    if (data.type == type_pair)
        return json::array({std::move(data.first()), std::move(data.second())});
    return json::array({std::move(data)});
}

manapi::json manapi::json::parse(STRING_VIEW data) {
    return std::move(json(data, true));
}

std::string manapi::json::stringify(const json &n, int spaces) {
    return std::move(n.dump(spaces));
}

manapi::json manapi::json::array(const std::initializer_list<json> &data) {
    // rvalue, arr not be destroyed
    auto arr = json::array();

    for (const auto & it : data)
    {
        if (it.type == type_pair)
        {
            // we need to resolve pair to array

            json first = static_cast <PAIR *> (it.src)->first;
            json second = static_cast <PAIR *> (it.src)->second;

            json element (json::array());

            try
            {
                element.as_array().push_back(std::move(first));
                element.as_array().push_back(std::move(second));

                arr.as_array().push_back(std::move(element));
            }
            catch (json_parse_exception &e)
            {
                throw json_parse_exception(std::move(e));
            }
        }
        else
        {
            try
            {
                arr.as_array().push_back(it);
            }
            catch (json_parse_exception &e)
            {
                throw json_parse_exception(std::move(e));
            }
        }
    }

    return std::move(arr);
}

std::map<std::string, manapi::json>::iterator manapi::json::find(const STRING &key) {
    return this->as_object().find(key);
}

std::map<std::string, manapi::json>::const_iterator manapi::json::find(const STRING &key) const {
    return this->as_object().find(key);
}

const manapi::json::ARRAY & manapi::json::each() const {
    return as_array();
}

const manapi::json::OBJECT & manapi::json::entries() const {
    return as_object();
}

manapi::json::ARRAY & manapi::json::each() {
    return as_array();
}

manapi::json::OBJECT & manapi::json::entries() {
    return as_object();
}

bool manapi::json::contains(const UNICODE_STRING &key) const {
    return contains(unicode::str32to4(key));
}

manapi::json & manapi::json::first() {
    return this->_as_pair().first;
}

manapi::json & manapi::json::second() {
    return this->_as_pair().second;
}

const manapi::json & manapi::json::first() const {
    return this->_as_pair().first;
}

const manapi::json & manapi::json::second() const {
    return this->_as_pair().second;
}

bool manapi::json::contains(const std::string &key) const {
    return this->as_object().contains(key);
}

void manapi::json::erase(const UNICODE_STRING &key) {
    erase(unicode::str32to4(key));
}

std::vector<manapi::json>::iterator manapi::json::erase(ARRAY::iterator it) {
    return this->as_array().erase(it);
}

std::map<std::string, manapi::json>::iterator manapi::json::erase(OBJECT::iterator it) {
    return this->as_object().erase(it);
}

std::vector<manapi::json>::const_iterator manapi::json::erase(ARRAY::const_iterator it) {
    return this->as_array().erase(it);
}

std::map<std::string, manapi::json>::const_iterator manapi::json::erase(OBJECT::const_iterator it) {
    return this->as_object().erase(it);
}

void manapi::json::erase(const std::string &key) {
    this->as_object().erase(key);
}

bool manapi::json::is_object() const {
    return type == type_object;
}

bool manapi::json::is_array() const {
    return type == type_array;
}

bool manapi::json::is_string() const {
    return type == type_string;
}

bool manapi::json::is_integer() const {
    return type == type_integer;
}

bool manapi::json::is_null() const {
    return type == type_null;
}

bool manapi::json::is_decimal() const {
    return type == type_decimal;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
bool manapi::json::is_bigint() const {
    return type == type_bigint;
}
#endif

bool manapi::json::is_bool() const {
    return type == type_boolean;
}

bool manapi::json::is_pair() const {
    return this->type == type_pair;
}

manapi::json::OBJECT & manapi::json::_as_object() const {
    if (type == type_object) {
        return *static_cast<json::OBJECT *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::OBJECT & manapi::json::as_object() const {
    return this->_as_object();
}

manapi::json::OBJECT & manapi::json::as_object() {
    return this->_as_object();
}

manapi::json::ARRAY & manapi::json::_as_array() const {
    if (type == type_array) {
        return *static_cast<json::ARRAY *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::ARRAY & manapi::json::as_array() const {
    return this->_as_array();
}

manapi::json::ARRAY & manapi::json::as_array() {
    return this->_as_array();
}

manapi::json::STRING & manapi::json::_as_string() const {
    if (type == type_string) {
        return *static_cast<json::STRING *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::STRING & manapi::json::as_string() const {
    return this->_as_string();
}

manapi::json::STRING & manapi::json::as_string() {
    return this->_as_string();
}

manapi::json::INTEGER & manapi::json::_as_integer() const {
    if (type == type_integer) {
        return *static_cast<json::INTEGER *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::INTEGER & manapi::json::as_integer() const {
    return this->_as_integer();
}

manapi::json::INTEGER & manapi::json::as_integer() {
    return this->_as_integer();
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT & manapi::json::_as_bigint() const {
    if (type == type_bigint) {
        return *static_cast<json::BIGINT *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}
#endif

manapi::json::PAIR & manapi::json::_as_pair() const {
    if (this->type == type_pair) {
        return *static_cast<json::PAIR *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
const manapi::json::BIGINT & manapi::json::as_bigint() const {
    return this->_as_bigint();
}
#endif

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT & manapi::json::as_bigint() {
    return this->_as_bigint();
}
#endif

manapi::json::BOOLEAN & manapi::json::_as_bool() const {
    if (type == type_boolean) {
        return *static_cast<json::BOOLEAN *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::BOOLEAN & manapi::json::as_bool() const {
    return this->_as_bool();
}

manapi::json::BOOLEAN & manapi::json::as_bool() {
    return this->_as_bool();
}

manapi::json::DECIMAL & manapi::json::_as_decimal() const {
    if (type == type_decimal) {
        return *static_cast<json::DECIMAL *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::DECIMAL & manapi::json::as_decimal() const {
    return this->_as_decimal();
}

manapi::json::DECIMAL & manapi::json::as_decimal() {
    return this->_as_decimal();
}

manapi::json::NULLPTR manapi::json::as_null() const {
    if (type == type_null) {
        return nullptr;
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::OBJECT manapi::json::as_object_cast() const {
    if (type == type_object) {
        return as_object();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::ARRAY manapi::json::as_array_cast() const {
    if (type == type_array) {
        return as_array();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::STRING manapi::json::as_string_cast() const {
    if (type == type_string) {
        return as_string();
    }
    if (type == type_integer) {
        return std::to_string(as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == type_bigint) {
        return as_bigint().stringify();
    }
#endif
    if (type == type_decimal) {
        return std::to_string(as_decimal());
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::INTEGER manapi::json::as_integer_cast() const {
    if (type == type_string) {
        return std::stoll(as_string());
    }
    if (type == type_integer) {
        return as_integer();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == type_bigint) {
        return as_bigint().integerify();
    }
#endif
    if (type == type_decimal) {
        // long double to long long
        return static_cast <json::INTEGER> (as_decimal());
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::NULLPTR manapi::json::as_null_cast() const {
    if (type == type_null) {
        return as_null();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::DECIMAL manapi::json::as_decimal_cast() const {
    if (type == type_decimal) {
        return as_decimal();
    }
    if (type == type_integer) {
        return static_cast<json::DECIMAL> (as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == type_bigint) {
        return as_bigint().decimalify();
    }
#endif
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT manapi::json::as_bigint_cast() const {
    if (type == type_bigint) {
        return as_bigint();
    }
    if (type == type_integer) {
        return std::move(bigint (as_integer()));
    }
    if (type == type_decimal) {
        return std::move(bigint (as_decimal()));
    }
    if (type == type_string) {
        return std::move(bigint (as_string()));
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}
#endif

manapi::json::BOOLEAN manapi::json::as_bool_cast() const {
    if (type == type_boolean) {
        return as_bool();
    }
    if (type == type_array) {
        return !as_array().empty();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == type_bigint) {
        return as_bigint() != 0;
    }
#endif
    if (type == type_object) {
        return !as_object().empty();
    }
    if (type == type_string) {
        return !as_string().empty();
    }
    if (type == type_decimal) {
        return as_decimal() != 0;
    }
    if (type == type_integer) {
        return as_integer() != 0;
    }
    if (type == type_null) {
        return false;
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

size_t manapi::json::size() const {
    if (is_array())
    {
        return as_array().size();
    }
    if (is_string())
    {
        return as_string().size();
    }
    if (is_object())
    {
        return as_object().size();
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

bool manapi::json::empty () const {
    return this->size() == 0;
}

manapi::json manapi::json::operator+(ssize_t num) const {
    auto n = *this;
    if (n.is_integer())
    {
        n.as_integer() += num;
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
    {
        n.as_bigint() += num;
    }
#endif
    else if (n.is_decimal())
    {
        n.as_decimal() += num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator+(DECIMAL num) const {
    auto n = *this;
    if (n.is_integer())
    {
        n.as_integer() += static_cast<INTEGER> (num);
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
    {
        n.as_bigint() += num;
    }
#endif
    else if (n.is_decimal())
    {
        n.as_decimal() += num;
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json manapi::json::operator+(const BIGINT &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.as_bigint() += num;
    }
    else if (n.is_integer())
    {
        n.as_integer() += num.integerify();
    }
    else if (n.is_decimal())
    {
        n.as_decimal() += num.decimalify();
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}
#endif

manapi::json manapi::json::operator+(const STRING &str) const {
    auto n = *this;
    if (n.is_string())
    {
        n.as_string() += str;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const char *str) const {
    auto n = *this;
    if (n.is_string()) {
        n.as_string() += str;
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json & manapi::json::operator+=(const STRING &str) {
    *this = this->operator+(str);
    return *this;
}

manapi::json & manapi::json::operator+=(const char *str) {
    *this = this->operator+(str);
    return *this;
}

manapi::json & manapi::json::operator-=(INTEGER num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator-=(int num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator-=(DECIMAL num) {
    *this = this->operator-(num);
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json & manapi::json::operator-=(const BIGINT &num) {
    *this = this->operator-(num);
    return *this;
}
#endif

manapi::json & manapi::json::operator+=(INTEGER num) {
    *this = this->operator+(num);
    return *this;
}

manapi::json & manapi::json::operator+=(DECIMAL num) {
    *this = this->operator+(num);
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json & manapi::json::operator+=(const BIGINT &num) {
    *this = this->operator+(num);
    return *this;
}
#endif

bool manapi::json::operator==(const json &x) const {
    if (type != x.type)
    {
        return false;
    }

    switch (type)
    {
        case type_integer:
            return as_integer() == x.as_integer();
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case type_bigint:
            return as_bigint() == x.as_bigint();
#endif
        case type_string:
            return as_string() == x.as_string();
        case type_boolean:
            return as_bool() == x.as_bool();
        case type_decimal:
            return as_decimal() == x.as_decimal();
        case type_null:
            return true;
        case type_object:
            if (size() != x.size())
            {
                return false;
            }

            for (const auto &item: entries())
            {
                if (!x.contains(item.first) || (x.at(item.first) != item.second))
                {
                    return false;
                }
            }
            return true;
        case type_array:
            if (size() != x.size())
            {
                return false;
            }
            for (size_t i = 0; i < size(); i++)
            {
                if (at(i) != x.at(i))
                {
                    return false;
                }
            }
            return true;
        default:
            return false;
    }

    return false;
}

bool manapi::json::operator==(BOOLEAN n) const {
    return this->as_bool_cast() == n;
}

bool manapi::json::operator==(const char *n) const {
    return this->as_string() == n;
}

bool manapi::json::operator==(const STRING_VIEW &n) const {
    return std::string_view(this->as_string()) == n;
}

bool manapi::json::operator==(const STRING &n) const {
    return this->as_string() == n;
}

bool manapi::json::operator==(INTEGER n) const {
    return this->as_integer_cast() == n;
}

bool manapi::json::operator==(DECIMAL n) const {
    return this->as_decimal_cast() == n;
}

bool manapi::json::operator>(INTEGER n) const {
    return this->as_integer_cast() > n;
}

bool manapi::json::operator>(DECIMAL n) const {
    return this->as_decimal_cast() > n;
}

bool manapi::json::operator>=(INTEGER n) const {
    return this->as_integer_cast() >= n;
}

bool manapi::json::operator>=(DECIMAL n) const {
    return this->as_decimal_cast() >= n;
}

bool manapi::json::operator<(INTEGER n) const {
    return this->as_integer_cast() < n;
}

bool manapi::json::operator<(DECIMAL n) const {
    return this->as_decimal_cast() < n;
}

bool manapi::json::operator<=(INTEGER n) const {
    return this->as_integer_cast() <= n;
}

bool manapi::json::operator<=(DECIMAL n) const {
    return this->as_decimal_cast() <= n;
}

bool manapi::json::operator==(const NULLPTR &n) const {
    return this->is_null();
}

bool manapi::json::operator!=(const BIGINT &n) const {
    return !this->operator==(n);
}

bool manapi::json::operator==(const BIGINT &n) const {
    if (this->is_integer())
        return n == this->as_integer();
    if (this->is_decimal())
        return n == static_cast<double>(this->as_decimal());
    if (this->is_bigint())
        return this->as_bigint() == n;

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

bool manapi::json::operator<(const BIGINT &n) const {
    if (this->is_integer())
        return n > this->as_integer();
    if (this->is_decimal())
        return n > static_cast<double>(this->as_decimal());
    if (this->is_bigint())
        return this->as_bigint() < n;

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

bool manapi::json::operator>(const BIGINT &n) const {
    if (this->is_integer())
        return n < this->as_integer();
    if (this->is_decimal())
        return n < static_cast<double>(this->as_decimal());
    if (this->is_bigint())
        return this->as_bigint() > n;

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

bool manapi::json::operator<=(const BIGINT &n) const {
    if (this->is_integer())
        return n >= this->as_integer();
    if (this->is_decimal())
        return n >= static_cast<double>(this->as_decimal());
    if (this->is_bigint())
        return this->as_bigint() <= n;

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

bool manapi::json::operator>=(const BIGINT &n) const {
    if (this->is_integer())
        return n <= this->as_integer();
    if (this->is_decimal())
        return n <= static_cast<double>(this->as_decimal());
    if (this->is_bigint())
        return this->as_bigint() >= n;

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

}

manapi::json manapi::json::operator-(INTEGER num) const {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(DECIMAL num) const {
    return std::move(this->operator+(-num));
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json manapi::json::operator-(const BIGINT &num) const {
    return std::move(this->operator+(-num));
}
#endif

void manapi::json::delete_value_static(short type, void *src) {
    switch (type) {
        case type_null:
            break;
        case type_number:
            THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "type_number is a complex type");
            break;
        case type_array:
            delete static_cast<ARRAY  *> (src);
            break;
        case type_object:
            delete static_cast<OBJECT *> (src);
            break;
        case type_boolean:
            delete static_cast<BOOLEAN *> (src);
            break;
        case type_integer:
            delete static_cast<INTEGER *> (src);
            break;
        case type_string:
            delete static_cast<STRING *> (src);
            break;
        case type_decimal:
            delete static_cast<DECIMAL *> (src);
            break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case type_bigint:
            delete static_cast<BIGINT *> (src);
            break;
#endif
        case type_pair:
            delete static_cast<PAIR *> (src);
            break;
        default:
            THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_BUG, "JSON BUG: Invalid type of data to delete: {}", static_cast<int> (type_pair));
    }
}


// Exceptions

manapi::json_parse_exception::json_parse_exception(const json_err_num &errnum, const std::string &msg) {
    this->message = std::format ("{}. json errnum = {}", msg, static_cast<int>(errnum));
    this->errnum = errnum;
}

const manapi::json_err_num &manapi::json_parse_exception::err_num () const {
    return this->errnum;
}

const char *manapi::json_parse_exception::what() const noexcept {
    return message.data();
}