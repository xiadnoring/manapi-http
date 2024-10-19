#include <memory.h>
#include <format>

#include "ManapiJson.hpp"
#include "ManapiBigint.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJsonBuilder.hpp"

const static std::string JSON_TRUE   = "true";
const static std::string JSON_FALSE  = "false";
const static std::string JSON_NULL   = "null";

#define THROW_MANAPI_JSON_MISSING_FUNCTION this->throw_could_not_use_func(__FUNCTION__)
#define THROW_MANAPI_JSON_ERROR(errnum, msg, ...) throw manapi::json_parse_exception (errnum, std::format(msg, __VA_ARGS__));

manapi::json::json() = default;

manapi::json::json(const STRING_VIEW &str, const bool &to_parse) {
    if (to_parse)
    {
        this->parse(str);
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

manapi::json::json(const UNICODE_STRING &str, const bool &to_parse) {
    if (to_parse)
    {
        this->parse(str);
    }
    else
    {
        _set_string(net::utils::str32to4(str));
    }
}

manapi::json::json(const NUMBER &num) {
    this->parse(num);
}

manapi::json::json(const size_t &num) {
    this->parse(num);
}

manapi::json::json(const manapi::json &other) {
    *this = other;
}

manapi::json::json(json &&other) noexcept {
    src = other.src;
    type = other.type;

    _debug_symb_reinit();

    other._set_nullptr();
}

manapi::json::json(const char *plain_text, const bool &to_parse)
{
    if (to_parse)
    {
        this->parse(STRING_VIEW (plain_text));
    }
    else
    {
        _set_string(plain_text);
    }
}

manapi::json::json(const int &num)
{
    this->parse(num);
}

manapi::json::json(const double &num)
{
    this->parse(num);
}

manapi::json::json(const DECIMAL &num)
{
    this->parse(num);
}

manapi::json::json(const BIGINT &num)
{
    this->parse(num);
}

manapi::json::json(const BOOLEAN &value)
{
    this->parse(value);
}

manapi::json::json(const OBJECT &obj) {
    this->parse(obj);
}

manapi::json::json(const ARRAY &arr) {
    this->parse(arr);
}

manapi::json::json(const nullptr_t &n)
{
    this->parse(n);
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

void manapi::json::parse(const NUMBER &num) {
    _set_number(num);
}

void manapi::json::parse(const int &num) {
    this->parse(static_cast<NUMBER> (num));
}

void manapi::json::parse(const double &num) {
    this->parse(static_cast<DECIMAL> (num));
}

void manapi::json::parse(const DECIMAL &num) {
    _set_decimal(num);
}

void manapi::json::parse(const BIGINT &num) {
    _set_bigint(num);
}

void manapi::json::parse(const OBJECT &obj) {
    _set_object(obj);
}

void manapi::json::parse(const ARRAY &arr) {
    _set_array(arr);
}

void manapi::json::parse(const BOOLEAN &val) {
    _set_bool(val);
}

void manapi::json::parse(const UNICODE_STRING &plain_text) {
    this->parse(net::utils::str32to4(plain_text));
}

void manapi::json::parse(const NULLPTR &n) {
    type    = type_null;
    src     = nullptr;
}

void manapi::json::parse(const STRING_VIEW &plain_text, const bool &use_bigint, const size_t &bigint_precision) {
    json_builder builder (json_mask(nullptr), use_bigint, bigint_precision);
    builder << plain_text;
    *this = builder.get();
}

void manapi::json::parse(const size_t &num) {
    this->parse (static_cast<NUMBER> (num));
}

std::string manapi::json::dump(const size_t &spaces, const size_t &first_spaces) const {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) str += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else str += ' ';
#define JSON_DUMP_NEED_SPACES   for (size_t z = 0; z < total_spaces; z++) str += ' ';
#define JSON_DUMP_LAST_SPACES   for (size_t z = 0; z < first_spaces; z++) str += ' ';
    std::string str;

    if (type == type_string)
    {
        str = '"' + net::utils::escape_string(as_string()) + '"';
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
                str += net::utils::escape_string(it->first) + "\": " + it->second.dump(spaces, total_spaces);
                ++it;
            }

            // others
            for (; it != map.end(); ++it) {
                str += ',';

                JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                JSON_DUMP_NEED_SPACES

                str += '"';
                str += net::utils::escape_string(it->first) + "\": " + it->second.dump(spaces, total_spaces);
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
        THROW_MANAPI_JSON_ERROR (ERR_JSON_BUG, "Bug has been deteceted: {}", "type = type_pair");
    }

    return str;
}

size_t manapi::json::get_start_cut() const {
    return start_cut;
}

size_t manapi::json::get_end_cut() const {
    return end_cut;
}

void manapi::json::error_invalid_char(const UNICODE_STRING &plain_text, const size_t &i) {
    THROW_MANAPI_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", net::utils::str32to4(plain_text[i]), i + 1);
}

void manapi::json::error_invalid_char(const STRING_VIEW &plain_text, const size_t &i) {
    THROW_MANAPI_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", plain_text[i], i + 1);
}

void manapi::json::error_unexpected_end(const size_t &i) {
    THROW_MANAPI_JSON_ERROR(ERR_JSON_UNEXPECTED_END, "Unexpected end of JSON input at {}", i + 1);
}

void manapi::json::delete_value() {
    delete_value_static(type, src);
}

void manapi::json::_set_object() {
    this->type = types::type_object;
    this->src = new OBJECT ();
    _debug_symb_reinit();
}

void manapi::json::_set_bool() {
    this->type = types::type_boolean;
    this->src = new BOOLEAN ();
    _debug_symb_reinit();
}

void manapi::json::_set_array() {
    this->type = types::type_array;
    this->src = new ARRAY ();
    _debug_symb_reinit();
}

void manapi::json::_set_string() {
    this->type = types::type_string;
    this->src = new STRING ();
    _debug_symb_reinit();
}

void manapi::json::_set_number() {
    this->type = types::type_number;
    this->src = new NUMBER ();
    _debug_symb_reinit();
}

void manapi::json::_set_decimal() {
    this->type = types::type_decimal;
    this->src = new DECIMAL ();
    _debug_symb_reinit();
}

void manapi::json::_set_bigint() {
    this->type = types::type_bigint;
    this->src = new BIGINT ();
    _debug_symb_reinit();
}

void manapi::json::_set_nullptr() {
    this->type = types::type_null;
    this->src = nullptr;
    _debug_symb_reinit();
}

void manapi::json::_set_pair() {
    this->type = types::type_pair;
    this->src = new PAIR();
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

void manapi::json::_set_number(const NUMBER &val) {
    _set_number();
    get<NUMBER>() = val;
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
    return std::ref(this->at(key));
}

manapi::json &manapi::json::operator[](const UNICODE_STRING &key) {
    return std::ref(this->at(key));
}

manapi::json &manapi::json::operator[](const size_t &index) {
    return std::ref(this->at(index));
}

manapi::json & manapi::json::operator[](const int &index) {
    return std::ref(this->at(index));
}

const manapi::json & manapi::json::operator[](const STRING &key) const {
    return std::ref(this->at(key));
}

const manapi::json & manapi::json::operator[](const UNICODE_STRING &key) const {
    return std::ref(this->at(key));
}

const manapi::json & manapi::json::operator[](const size_t &index) const {
    return std::ref(this->at(index));
}

const manapi::json & manapi::json::operator[](const int &index) const {
    return std::ref(this->at(index));
}

manapi::json &manapi::json::at(const UNICODE_STRING &key)  {
    return std::ref(this->at(net::utils::str32to4(key)));
}

manapi::json &manapi::json::at(const std::string &key)  {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &map = this->get<OBJECT>();

    // if (!map.contains(key))
    // {
    //     THROW_MANAPI_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", net::utils::escape_string(key));
    // }

    return map[key];
}

manapi::json &manapi::json::at(const size_t &index)  {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

    if (arr.size() <= index)
    {
        THROW_MANAPI_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

manapi::json & manapi::json::at(const int &index) {
    return std::ref(this->at(static_cast<size_t> (index)));
}

const manapi::json & manapi::json::at(const std::string &key) const {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &map = this->get<OBJECT>();

    if (!map.contains(key))
    {
        THROW_MANAPI_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", net::utils::escape_string(key));
    }

    return map.at(key);
}

const manapi::json & manapi::json::at(const UNICODE_STRING &key) const {
    return std::ref(this->at(net::utils::str32to4(key)));
}

const manapi::json & manapi::json::at(const size_t &index) const {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    auto &arr = this->get<ARRAY>();

    if (arr.size() <= index)
    {
        THROW_MANAPI_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());
    }

    return arr.at(index);
}

const manapi::json & manapi::json::at(const int &index) const {
    return std::ref(this->at(static_cast<size_t> (index)));
}

manapi::json& manapi::json::operator=(const std::string &str) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_string(str);

    return *this;
}

manapi::json &manapi::json::operator=(const bool &b) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_bool(b);

    return *this;
}

manapi::json &manapi::json::operator=(const ssize_t &num) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_number(num);

    return *this;
}

manapi::json &manapi::json::operator=(const double &num) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_decimal(num);

    return *this;
}

manapi::json &manapi::json::operator=(const json::DECIMAL &num) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_decimal(num);

    return *this;
}

manapi::json &manapi::json::operator=(const long long &num) {
    return this->operator=(static_cast<NUMBER> (num));
}

manapi::json &manapi::json::operator=(nullptr_t const &n) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_nullptr();

    return *this;
}

manapi::json &manapi::json::operator=(const UNICODE_STRING &str) {
    return this->operator=(net::utils::str32to4(str));
}

manapi::json &manapi::json::operator=(const char *str) {
    return this->operator=(STRING(str));
}

manapi::json &manapi::json::operator=(const int &num) {
    return this->operator=(static_cast<NUMBER> (num));
}

manapi::json &manapi::json::operator=(const manapi::bigint &num) {
    net::utils::before_delete clean ([type = this->type, src = this->src] () -> void { delete_value_static(type, src); });

    _set_bigint(num);

    return *this;
}

manapi::json &manapi::json::operator=(const manapi::json &obj) {
    if (&obj != this)
    {
        // temp values
        short   temp_type = this->type;
        void    *temp_src = this->src;

        this->start_cut             = 0;
        this->root                  = true;

        switch (obj.type) {
            case type_string:
                _set_string(obj.as_string());
                break;
            case type_number:
                _set_number(obj.as_number());
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

        delete_value_static (temp_type, temp_src);
    }
    return *this;
}

manapi::json & manapi::json::operator=(json &&obj) {
    std::swap(src, obj.src);
    std::swap(type, obj.type);

    obj._debug_symb_reinit();
    _debug_symb_reinit();

    return *this;
}

manapi::json &manapi::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::json (data);
}

void manapi::json::insert(const UNICODE_STRING &key, const manapi::json &obj) {
    insert (net::utils::str32to4(key), obj);
}

void manapi::json::insert(const STRING &key, const manapi::json &obj) {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }


    if (get<OBJECT>().contains(key))
    {
        THROW_MANAPI_JSON_ERROR(ERR_JSON_DUPLICATE_KEY, "duplicate key: {}", net::utils::escape_string(key));
    }

    json item = obj;
    item.root = false;

    get<OBJECT>().insert({key, std::move(item)});
}

void manapi::json::push_back(manapi::json obj) {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    obj.root = false;

    get<ARRAY>().push_back(std::move(obj));
}

void manapi::json::pop_back() {
    if (type != type_array)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
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
            catch (const json_parse_exception &e)
            {
                throw json_parse_exception(e);
            }
        }
        else
        {
            try
            {
                arr.get<ARRAY>().push_back(it);
            }
            catch (const json_parse_exception &e)
            {
                throw json_parse_exception(e);
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
    return contains(net::utils::str32to4(key));
}

bool manapi::json::contains(const std::string &key) const {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    return get<OBJECT>().contains(key);
}

void manapi::json::erase(const UNICODE_STRING &key) {
    erase(net::utils::str32to4(key));
}

void manapi::json::erase(const std::string &key) {
    if (type != type_object)
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    get<OBJECT>().erase(key);
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

bool manapi::json::is_number() const {
    return type == type_number;
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

const manapi::json::OBJECT & manapi::json::as_object() const {
    if (type == type_object) {
        return *static_cast<json::OBJECT *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::ARRAY & manapi::json::as_array() const {
    if (type == type_array) {
        return *static_cast<json::ARRAY *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::STRING & manapi::json::as_string() const {
    if (type == type_string) {
        return *static_cast<json::STRING *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::NUMBER & manapi::json::as_number() const {
    if (type == type_number) {
        return *static_cast<json::NUMBER *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::BIGINT & manapi::json::as_bigint() const {
    if (type == type_bigint) {
        return *static_cast<json::BIGINT *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::BOOLEAN & manapi::json::as_bool() const {
    if (type == type_boolean) {
        return *static_cast<json::BOOLEAN *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

const manapi::json::DECIMAL & manapi::json::as_decimal() const {
    if (type == type_decimal) {
        return *static_cast<json::DECIMAL *> (src);
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::NULLPTR manapi::json::as_null() const {
    if (type == type_null) {
        return nullptr;
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::OBJECT manapi::json::as_object_cast() const {
    if (type == type_object) {
        return as_object();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::ARRAY manapi::json::as_array_cast() const {
    if (type == type_array) {
        return as_array();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::STRING manapi::json::as_string_cast() const {
    if (type == type_string) {
        return as_string();
    }
    if (type == type_number) {
        return std::move(std::to_string(as_number()));
    }
    if (type == type_bigint) {
        return std::move(as_bigint().stringify());
    }
    if (type == type_decimal) {
        return std::move(std::to_string(as_decimal()));
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::NUMBER manapi::json::as_number_cast() const {
    if (type == type_number) {
        return as_number();
    }
    if (type == type_bigint) {
        return as_bigint().numberify();
    }
    if (type == type_decimal) {
        // long double to long long
        return static_cast <json::NUMBER> (as_decimal());
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::NULLPTR manapi::json::as_null_cast() const {
    if (type == type_null) {
        return as_null();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::DECIMAL manapi::json::as_decimal_cast() const {
    if (type == type_decimal) {
        return as_decimal();
    }
    if (type == type_number) {
        return static_cast<json::DECIMAL> (as_number());
    }
    if (type == type_bigint) {
        return as_bigint().decimalify();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::BIGINT manapi::json::as_bigint_cast() const {
    if (type == type_bigint) {
        return as_bigint();
    }
    if (type == type_number) {
        return std::move(bigint (as_number()));
    }
    if (type == type_decimal) {
        return std::move(bigint (as_decimal()));
    }
    if (type == type_string) {
        return std::move(bigint (as_string()));
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json::BOOLEAN manapi::json::as_bool_cast() const {
    if (type == type_boolean) {
        return as_bool();
    }
    THROW_MANAPI_JSON_MISSING_FUNCTION;
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

    THROW_MANAPI_JSON_MISSING_FUNCTION;
}

manapi::json manapi::json::operator+(const ssize_t &num) {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_number())
    {
        n.get<NUMBER>() += num;
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num;
    }
    else
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }

    return std::move(n);
}

manapi::json manapi::json::operator+(const int &num) {
    return std::move(this->operator+(static_cast<NUMBER> (num)));
}

manapi::json manapi::json::operator+(const DECIMAL &num) {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_number())
    {
        n.get<NUMBER>() += static_cast<NUMBER> (num);
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num;
    }
    else {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const double &num) {
    return std::move(this->operator+(static_cast<DECIMAL> (num)));
}

manapi::json manapi::json::operator+(const BIGINT &num) {
    auto n = *this;
    if (n.is_bigint())
    {
        n.get<BIGINT>() += num;
    }
    else if (n.is_number())
    {
        n.get<NUMBER>() += num.numberify();
    }
    else if (n.is_decimal())
    {
        n.get<DECIMAL>() += num.decimalify();
    }
    else {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const STRING &str) {
    auto n = *this;
    if (n.is_string())
    {
        n.get<STRING>() += str;
    }
    else
    {
        THROW_MANAPI_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

void manapi::json::operator+=(const STRING &str) {
    *this = this->operator+(str);
}

void manapi::json::operator-=(const NUMBER &num) {
    *this = this->operator-(num);
}

void manapi::json::operator-=(const int &num) {
    *this = this->operator-(num);
}

void manapi::json::operator-=(const DECIMAL &num) {
    *this = this->operator-(num);
}

void manapi::json::operator-=(const double &num) {
    *this = this->operator-(num);
}

void manapi::json::operator-=(const BIGINT &num) {
    *this = this->operator-(num);
}

void manapi::json::operator+=(const NUMBER &num) {
    *this = this->operator+(num);
}

void manapi::json::operator+=(const int &num) {
    *this = this->operator+(num);
}

void manapi::json::operator+=(const DECIMAL &num) {
    *this = this->operator+(num);
}

void manapi::json::operator+=(const double &num) {
    *this = this->operator+(num);
}

void manapi::json::operator+=(const BIGINT &num) {
    *this = this->operator+(num);
}

bool manapi::json::operator==(const json &x) const {
    if (type != x.type)
    {
        return false;
    }

    switch (type)
    {
        case type_number:
            return as_number() == x.as_number();
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

manapi::json manapi::json::operator-(const ssize_t &num) {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const int &num) {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const DECIMAL &num) {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const double &num) {
    return std::move(this->operator+(-num));
}

manapi::json manapi::json::operator-(const BIGINT &num) {
    return std::move(this->operator+(-num));
}

void manapi::json::delete_value_static(const short &type, void *src) {
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
            THROW_MANAPI_JSON_ERROR(ERR_JSON_BUG, "JSON BUG: Invalid type of data to delete: {}", static_cast<int> (type_pair));
    }
}

void manapi::json::throw_could_not_use_func(const std::string &func) const
{
    THROW_MANAPI_JSON_ERROR(ERR_JSON_UNSUPPORTED_TYPE, "json object with type {} could not use func: {}", static_cast <int> (type), func);
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