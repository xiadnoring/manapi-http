#include <memory.h>
#include <format>

#include "ManapiJson.hpp"

#include <utility>

#include "ManapiBeforeDelete.hpp"
#include "ManapiBigint.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJsonBuilder.hpp"

const static std::string JSON_TRUE   = "true";
const static std::string JSON_FALSE  = "false";
const static std::string JSON_NULL   = "null";

#define RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) manapi::json_parse_exception (errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_MISSING_FUNCTION throw RETHROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNSUPPORTED_TYPE, "json object with type {} could not use func: {}", static_cast <int> (this->type), __FUNCTION__)
#define THROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) throw RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, __VA_ARGS__)

manapi::json::json() = default;

manapi::json::json(const STRING_VIEW &str, const bool &parse) {
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

manapi::json::json(const STRING &str) {
    json_builder::_valid_utf_string(str);
    _set_string(str);
}

manapi::json::json(const UNICODE_STRING &str, const bool &parse) {
    if (parse)
    {
        this->_parse(str);
    }
    else
    {
        _set_string(unicode::str32to4(str));
    }
}

manapi::json::json(const INTEGER &num) {
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

manapi::json::json(const char *plain_text, const bool &parse)
{
    if (parse)
    {
        this->_parse(STRING_VIEW (plain_text));
    }
    else
    {
        _set_string(plain_text);
    }
}

manapi::json::json(const DECIMAL &num)
{
    this->_parse(num);
}

manapi::json::json(const BIGINT &num)
{
    this->_parse(num);
}

manapi::json::json(const BOOLEAN &value)
{
    this->_parse(value);
}

manapi::json::json(const OBJECT &obj) {
    this->_parse(obj);
}

manapi::json::json(const ARRAY &arr) {
    this->_parse(arr);
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

void manapi::json::_parse(const INTEGER &num) {
    _set_integer(num);
}

void manapi::json::_parse(const int &num) {
    this->_parse(static_cast<INTEGER> (num));
}

void manapi::json::_parse(const double &num) {
    this->_parse(static_cast<DECIMAL> (num));
}

void manapi::json::_parse(const DECIMAL &num) {
    _set_decimal(num);
}

void manapi::json::_parse(const BIGINT &num) {
    _set_bigint(num);
}

void manapi::json::_parse(const OBJECT &obj) {
    _set_object(obj);
}

void manapi::json::_parse(const ARRAY &arr) {
    _set_array(arr);
}

void manapi::json::_parse(const BOOLEAN &val) {
    _set_bool(val);
}

void manapi::json::_parse(const UNICODE_STRING &plain_text) {
    this->_parse(unicode::str32to4(plain_text));
}

void manapi::json::_parse(const NULLPTR &n) {
    _set_nullptr();
}

void manapi::json::_parse(const STRING_VIEW &plain_text, const bool &use_bigint, const size_t &bigint_precision) {
    json_builder builder (json_mask(nullptr), use_bigint, bigint_precision);
    builder << plain_text;
    *this = builder.get();
}

void manapi::json::_parse(const size_t &num) {
    this->_parse (static_cast<INTEGER> (num));
}

std::string manapi::json::dump(const int &spaces, const int &first_spaces) const {
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

void manapi::json::error_invalid_char(const UNICODE_STRING &plain_text, const size_t &i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", unicode::str32to4(plain_text[i]), i + 1);
}

void manapi::json::error_invalid_char(const STRING_VIEW &plain_text, const size_t &i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", plain_text[i], i + 1);
}

void manapi::json::error_unexpected_end(const size_t &i) {
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

void manapi::json::_set_bigint() {
    delete_value_static(this->type, std::exchange(this->src, new BIGINT ()));
    this->type = types::type_bigint;
    _debug_symb_reinit();
}

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

void manapi::json::_set_object(const OBJECT &val) {
    _set_object();
    get<OBJECT>() = val;
}

void manapi::json::_set_bool(const BOOLEAN &val) {
    _set_bool();
    get<BOOLEAN>() = val;
}

void manapi::json::_set_array(const ARRAY &val) {
    _set_array();
    get<ARRAY>() = val;
}

void manapi::json::_set_string(const STRING_VIEW &val) {
    _set_string();
    get<STRING>() = val;
}

void manapi::json::_set_integer(const INTEGER &val) {
    _set_integer();
    get<INTEGER>() = val;
}

void manapi::json::_set_decimal(const DECIMAL &val) {
    _set_decimal();
    get<DECIMAL>() = val;
}

void manapi::json::_set_bigint(const BIGINT &val) {
    _set_bigint();
    get<BIGINT>() = val;
}

void manapi::json::_set_pair(json first, json second) {
    _set_pair();

    get<PAIR>().first = std::move(first);
    get<PAIR>().second = std::move(second);
}

manapi::json &manapi::json::operator[](const STRING &key) {
    return this->at(key);
}

manapi::json &manapi::json::operator[](const UNICODE_STRING &key) {
    return this->at(key);
}

manapi::json &manapi::json::operator[](const size_t &index) {
    return this->at(index);
}

const manapi::json & manapi::json::operator[](const STRING &key) const {
    return this->at(key);
}

const manapi::json & manapi::json::operator[](const UNICODE_STRING &key) const {
    return this->at(key);
}

const manapi::json & manapi::json::operator[](const size_t &index) const {
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

    auto &map = this->get<OBJECT>();

    // if (!map.contains(key))
    // {
    //     THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));
    // }

    return map[key];
}

manapi::json &manapi::json::at(const size_t &index)  {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

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

    auto &map = this->get<OBJECT>();

    if (!map.contains(key))
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));
    }

    return map.at(key);
}

const manapi::json & manapi::json::at(const UNICODE_STRING &key) const {
    return this->at(unicode::str32to4(key));
}

const manapi::json & manapi::json::at(const size_t &index) const {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

    if (arr.size() <= index)
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

manapi::json& manapi::json::operator=(const std::string &str) {
    _set_string(str);

    return *this;
}

manapi::json &manapi::json::operator=(const bool &b) {
    _set_bool(b);

    return *this;
}

manapi::json &manapi::json::operator=(const ssize_t &num) {
    _set_integer(num);

    return *this;
}

manapi::json &manapi::json::operator=(const json::DECIMAL &num) {
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

manapi::json &manapi::json::operator=(const manapi::bigint &num) {
    _set_bigint(num);

    return *this;
}

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
            case type_bigint:
                _set_bigint(obj.as_bigint());
                break;
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
        this->delete_value();

        this->src = std::exchange(obj.src, nullptr);
        this->type = std::exchange(obj.type, types::type_null);

        obj._debug_symb_reinit();
        _debug_symb_reinit();
    }

    return *this;
}

manapi::json &manapi::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::json (data);
}

manapi::json manapi::json::operator*(const INTEGER &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() *= num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() *= num;
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() *= num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator*(const DECIMAL &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() += static_cast<INTEGER>(num);
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator*(const BIGINT &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() += num.integerify();
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num.decimalify();
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json &manapi::json::operator*=(const INTEGER &num) {
    if (this->is_bigint())
        { this->get<BIGINT>() += num; }
    else if (this->is_integer())
        { this->get<INTEGER>() += num; }
    else if (this->is_decimal())
        { this->get<DECIMAL>() += num; }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}

manapi::json &manapi::json::operator*=(const DECIMAL &num) {
    if (this->is_bigint())
        { this->get<BIGINT>() += num; }
    else if (this->is_integer())
        { this->get<INTEGER>() += static_cast<INTEGER>(num); }
    else if (this->is_decimal())
        { this->get<DECIMAL>() += num; }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}

manapi::json &manapi::json::operator*=(const BIGINT &num) {
    if (this->is_bigint())
        { this->get<BIGINT>() += num; }
    else if (this->is_integer())
        { this->get<INTEGER>() += num.integerify(); }
    else if (this->is_decimal())
        { this->get<DECIMAL>() += num.decimalify(); }
    else
        { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    return *this;
}

void manapi::json::insert(const UNICODE_STRING &key, manapi::json obj) {
    insert (unicode::str32to4(key), std::move(obj));
}

void manapi::json::insert(const STRING &key, manapi::json obj) {
    if (type != type_object)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }


    if (get<OBJECT>().contains(key))
    {
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_DUPLICATE_KEY, "duplicate key: \"{}\"", unicode::escape_string(key));
    }

    obj.root=false;

    get<OBJECT>().insert({key, std::move(obj)});
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

    get<ARRAY>().push_back(std::move(obj));
}

void manapi::json::pop_back() {
    if (type != type_array)
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    get<ARRAY>().pop_back();
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

    for (const auto & it : data)
    {
        auto &key = static_cast <PAIR *> (it.src)->first.get<STRING>();
        json value = static_cast <PAIR *> (it.src)->second;
        obj.get<OBJECT>().insert({key, std::move(value)});
    }

    return std::move(obj);
}

manapi::json manapi::json::parse(const std::string &data) {
    return std::move(json(data, true));
}

std::string manapi::json::stringify(const json &n, const int &spaces) {
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
                element.get<ARRAY>().push_back(std::move(first));
                element.get<ARRAY>().push_back(std::move(second));

                arr.get<ARRAY>().push_back(std::move(element));
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
                arr.get<ARRAY>().push_back(it);
            }
            catch (json_parse_exception &e)
            {
                throw json_parse_exception(std::move(e));
            }
        }
    }

    return std::move(arr);
}

const manapi::json::ARRAY & manapi::json::each() const {
    return as_array();
}

const manapi::json::OBJECT & manapi::json::entries() const {
    return as_object();
}

manapi::json::ARRAY & manapi::json::each() {
    return get<ARRAY>();
}

manapi::json::OBJECT & manapi::json::entries() {
    return get<OBJECT>();
}

bool manapi::json::contains(const UNICODE_STRING &key) const {
    return contains(unicode::str32to4(key));
}

bool manapi::json::contains(const std::string &key) const {
    return this->as_object().contains(key);
}

void manapi::json::erase(const UNICODE_STRING &key) {
    erase(unicode::str32to4(key));
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

bool manapi::json::is_bigint() const {
    return type == type_bigint;
}

bool manapi::json::is_bool() const {
    return type == type_boolean;
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

manapi::json::BIGINT & manapi::json::_as_bigint() const {
    if (type == type_bigint) {
        return *static_cast<json::BIGINT *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::BIGINT & manapi::json::as_bigint() const {
    return this->_as_bigint();
}

manapi::json::BIGINT & manapi::json::as_bigint() {
    return this->_as_bigint();
}

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
        return std::move(std::to_string(as_integer()));
    }
    if (type == type_bigint) {
        return std::move(as_bigint().stringify());
    }
    if (type == type_decimal) {
        return std::move(std::to_string(as_decimal()));
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::INTEGER manapi::json::as_integer_cast() const {
    if (type == type_integer) {
        return as_integer();
    }
    if (type == type_bigint) {
        return as_bigint().integerify();
    }
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
    if (type == type_bigint) {
        return as_bigint().decimalify();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

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

manapi::json::BOOLEAN manapi::json::as_bool_cast() const {
    if (type == type_boolean) {
        return as_bool();
    }
    if (type == type_array) {
        return !as_array().empty();
    }
    if (type == type_bigint) {
        return as_bigint() != 0;
    }
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

manapi::json manapi::json::operator+(const ssize_t &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() += num;
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num;
    }
    else
    {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator+(const DECIMAL &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() += static_cast<INTEGER> (num);
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num;
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const BIGINT &num) const {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_integer())
    {
        n.get<INTEGER>() += num.integerify();
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num.decimalify();
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const STRING &str) const {
    auto n = *this;
    if (n.is_string())
    {
        n.get<STRING>() += str;
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
        n.get<STRING>() += str;
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

manapi::json & manapi::json::operator-=(const INTEGER &num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator-=(const int &num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator-=(const DECIMAL &num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator-=(const BIGINT &num) {
    *this = this->operator-(num);
    return *this;
}

manapi::json & manapi::json::operator+=(const INTEGER &num) {
    *this = this->operator+(num);
    return *this;
}

manapi::json & manapi::json::operator+=(const DECIMAL &num) {
    *this = this->operator+(num);
    return *this;
}

manapi::json & manapi::json::operator+=(const BIGINT &num) {
    *this = this->operator+(num);
    return *this;
}

bool manapi::json::operator==(const json &x) const {
    if (type != x.type)
    {
        return false;
    }

    switch (type)
    {
        case type_integer:
            return as_integer() == x.as_integer();
        case type_bigint:
            return as_bigint() == x.as_bigint();
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

bool manapi::json::operator==(const BOOLEAN &n) const {
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

bool manapi::json::operator==(const INTEGER &n) const {
    return this->as_integer() == n;
}

bool manapi::json::operator==(const DECIMAL &n) const {
    return this->as_decimal() == n;
}

bool manapi::json::operator==(const NULLPTR &n) const {
    return this->is_null();
}

manapi::json manapi::json::operator-(const INTEGER &num) const {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const DECIMAL &num) const {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const BIGINT &num) const {
    return std::move(this->operator+(-num));
}

void manapi::json::delete_value_static(const short &type, void *src) {
    switch (type) {
        case type_null:
            break;
        case type_number:
            THROW_MANAPIHTTP_EXCEPTION2(ERR_BUG, "type_number is a complex type");
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
        case type_bigint:
            delete static_cast<BIGINT *> (src);
            break;
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

const manapi::json_err_num &manapi::json_parse_exception::get_err_num () const {
    return this->errnum;
}

const char *manapi::json_parse_exception::what() const noexcept {
    return message.data();
}