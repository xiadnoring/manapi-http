#include <memory.h>
#include <format>
#include <utility>

#include "ManapiDebug.hpp"
#include "ManapiBigint.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "json/ManapiJson.hpp"
#include "json/ManapiJsonBuilder.hpp"

#include "../include/ManapiUtils.hpp"
#include "../include/std/ManapiBeforeDelete.hpp"
#include "../include/ManapiJsonMaskUtils.hpp"

static constexpr std::string_view json_true_ = "true";
static constexpr std::string_view json_false_ = "false";
static constexpr std::string_view json_null_ = "null";

#define RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) manapi::json_parse_exception (errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_MISSING_FUNCTION throw RETHROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNSUPPORTED_TYPE, "json object with type {}({}) could not use func: {}", \
    json_type_to_str(this->type), static_cast <int> (this->type), __FUNCTION__)
#define THROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) throw RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, __VA_ARGS__)

static void delete_value_static(int type, void *src) MANAPIHTTP_NOEXCEPT {
    switch (type) {
        case manapi::json::type_null:
            break;
        case manapi::json::type_number:
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s", "json(bug)","type_number is a complex type");
        assert(false && "type_number is a complex type");
        break;
        case manapi::json::type_array:
            delete static_cast<manapi::json::ARRAY  *> (src);
        break;
        case manapi::json::type_object:
            delete static_cast<manapi::json::OBJECT *> (src);
        break;
        case manapi::json::type_boolean:
            delete static_cast<manapi::json::BOOLEAN *> (src);
        break;
        case manapi::json::type_integer:
            delete static_cast<manapi::json::INTEGER *> (src);
        break;
        case manapi::json::type_string:
            delete static_cast<manapi::json::STRING *> (src);
        break;
        case manapi::json::type_decimal:
            delete static_cast<manapi::json::DECIMAL *> (src);
        break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case manapi::json::type_bigint:
            delete static_cast<manapi::json::BIGINT *> (src);
        break;
#endif
        case manapi::json::type_pair:
            delete static_cast<manapi::json::PAIR *> (src);
        break;
        default:
            manapi_log_error("%s: %s(%d)", "json(bug)", "invalid data type to delete", static_cast<int>(type));
        assert(false && "invalid data type to delete");
    }
}

manapi::json::json() = default;

manapi::json::json(STRING_VIEW str, bool parse) {
    if (parse)
    {
        this->parse_(str).unwrap();
    }
    else
    {
        json_builder::_valid_utf_string(str);
        set_string_(str);
    }
}

manapi::json::json(STRING str) {
    json_builder::_valid_utf_string(str);
    set_string_(std::move(str));
}

manapi::json::json(INTEGER num) {
    this->parse_(num);
}

manapi::json::json(const manapi::json &other) {
    *this = other;
}

manapi::json::json(json &&other) noexcept {
    if (&other != this) {
        this->delete_value();

        this->src = std::exchange(other.src, nullptr);
        this->type = std::exchange(other.type, types::type_null);

        debug_symb_reinit_();
    }
}

manapi::json::json(const char *plain_text, bool parse)
{
    if (parse)
    {
        this->parse_(STRING_VIEW (plain_text)).unwrap();
    }
    else
    {
        set_string_(STRING_VIEW{plain_text});
    }
}

manapi::json::json(DECIMAL num)
{
    this->parse_(num);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::json(BIGINT num)
{
    this->parse_(std::move(num));
}
#endif

manapi::json::json(BOOLEAN value)
{
    this->parse_(value);
}

manapi::json::json(OBJECT obj) {
    this->parse_(std::move(obj));
}

manapi::json::json(ARRAY arr) {
    this->parse_(std::move(arr));
}

manapi::json::json(const nullptr_t &n)
{
    this->parse_(n);
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
        set_pair_(*key, std::move(value));
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

void manapi::json::parse_(INTEGER num) {
    set_integer_(num);
}

void manapi::json::parse_(int num) {
    this->parse_(static_cast<INTEGER> (num));
}

void manapi::json::parse_(double num) {
    this->parse_(static_cast<DECIMAL> (num));
}

void manapi::json::parse_(DECIMAL num) {
    set_decimal_(num);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::parse_(BIGINT num) {
    set_bigint_(std::move(num));
}
#endif

void manapi::json::parse_(OBJECT obj) {
    set_object_(std::move(obj));
}

void manapi::json::parse_(ARRAY arr) {
    set_array_(std::move(arr));
}

void manapi::json::parse_(BOOLEAN val) {
    set_bool_(val);
}

void manapi::json::parse_(const NULLPTR &n) {
    set_nullptr_();
}
#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::error::status manapi::json::parse_(STRING_VIEW plain_text, bool use_bigint, size_t bigint_precision) {
    json_builder builder (json_mask(nullptr), use_bigint, bigint_precision);
    auto res = builder.parse(plain_text);
    if (!res.ok())
        return std::move(res);
    auto rhs = builder.get();
    if (!rhs.ok())
        return std::move(rhs.err());
    *this = std::move(rhs.unwrap());
    return error::status_ok();
}
#else
manapi::error::status manapi::json::parse_(STRING_VIEW plain_text) {
    json_builder builder (json_mask(nullptr));
    auto res = builder.parse(plain_text);
    if (!res.ok())
        return std::move(res);
    auto rhs = builder.get();
    if (!rhs.ok())
        return std::move(rhs.err());
    *this = rhs.unwrap();
    return error::status_ok();
}
#endif

void manapi::json::parse_(size_t num) {
    this->parse_ (static_cast<INTEGER> (num));
}

void json_dump_ (std::string &res, const manapi::json *n, int spaces, int first_spaces, bool root = true) {
#define JSON_DUMP_NEED_NEW_LINE if (spaces_enabled) res += '\n';
#define JSON_DUMP_NEED_NEW_LINE_OR_SPACE    JSON_DUMP_NEED_NEW_LINE \
                                            else {/**res += ' '**/};
#define JSON_DUMP_NEED_SPACES   for (int z = 0; z < total_spaces; z++) res += ' ';
#define JSON_DUMP_LAST_SPACES   for (int z = 0; z < first_spaces; z++) res += ' ';
    const int total_spaces = first_spaces + spaces;

    if (root) {
        JSON_DUMP_LAST_SPACES
    }

    switch (n->data_type()) {
        case manapi::json::type_string:
            res += manapi::unicode::escape_string(n->as_string());
        break;
        case manapi::json::type_decimal:
            res += std::to_string(n->as_decimal());
        break;
        case manapi::json::type_integer:
            res += std::to_string(n->as_integer());
        break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case manapi::json::type_bigint:
            res += '"';
            res += n->as_bigint().stringify();
            res += '"';
        break;
#endif
        case manapi::json::type_boolean:
            if (n->as_bool())
                res.append(json_true_);
            else
                res.append(json_false_);
        break;
        case manapi::json::type_null:
            res.append(json_null_);
        break;
        case manapi::json::type_object: {
            const bool spaces_enabled  = spaces > 0;

            auto &map = n->as_object();

            res += '{';

            JSON_DUMP_NEED_NEW_LINE

            if (!map.empty()) {
                auto it = map.begin();

                goto skip;;
                // first
                for (;it != map.end();++it) {
                    res += ',';

                    JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                    skip:
                    JSON_DUMP_NEED_SPACES

                    res += manapi::unicode::escape_string(it->first);
                    res += ":";
                    json_dump_(res, &it->second, spaces, total_spaces, false);
                }
            }

            JSON_DUMP_NEED_NEW_LINE

            JSON_DUMP_LAST_SPACES

            res += '}';
            break;
        }
        case manapi::json::type_array: {
            const bool spaces_enabled = spaces > 0;

            auto &arr = n->as_array();

            res += '[';

            JSON_DUMP_NEED_NEW_LINE


            if (!arr.empty()) {
                // dump items
                size_t i = 0;

                // first
                JSON_DUMP_NEED_SPACES
                json_dump_ (res, &arr.at(i), spaces, total_spaces, false);
                i++;

                // others
                for (; i < arr.size(); i++) {
                    res += ',';

                    JSON_DUMP_NEED_NEW_LINE_OR_SPACE
                    JSON_DUMP_NEED_SPACES

                    json_dump_ (res, &arr.at(i), spaces, total_spaces, false);
                }
            }

            JSON_DUMP_NEED_NEW_LINE
            JSON_DUMP_LAST_SPACES

            res += ']';
            break;
        }
        case manapi::json::type_pair: {
            res += '[';
            json_dump_ (res, &n->first(), spaces, total_spaces, false);
            json_dump_ (res, &n->second(), spaces, total_spaces, false);
            res += ']';
            break;
        }

    }
}

std::string manapi::json::dump(int spaces, int first_spaces) const {
    std::string res;
    json_dump_(res, this, spaces, first_spaces);
    return std::move(res);
}

void manapi::json::error_invalid_char(const STRING_VIEW &plain_text, size_t i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_INVALID_CHAR, "Invalid char '{}' at {}", plain_text[i], i + 1);
}

void manapi::json::error_unexpected_end(size_t i) {
    THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNEXPECTED_END, "Unexpected end of JSON input at {}", i + 1);
}

void manapi::json::delete_value() MANAPIHTTP_NOEXCEPT {
    delete_value_static(
        std::exchange(this->type, types::type_null), std::exchange(this->src, nullptr));
}

void manapi::json::set_object_() {
    delete_value_static(this->type, std::exchange(this->src, new OBJECT ()));
    this->type = types::type_object;
    debug_symb_reinit_();
}

void manapi::json::set_bool_() {
    delete_value_static(this->type, std::exchange(this->src, new BOOLEAN ()));
    this->type = types::type_boolean;
    debug_symb_reinit_();
}

void manapi::json::set_array_() {
    delete_value_static(this->type, std::exchange(this->src, new ARRAY ()));
    this->type = types::type_array;
    debug_symb_reinit_();
}

void manapi::json::set_string_() {
    delete_value_static(this->type, std::exchange(this->src, new STRING ()));
    this->type = types::type_string;
    debug_symb_reinit_();
}

void manapi::json::set_integer_() {
    delete_value_static(this->type, std::exchange(this->src, new INTEGER ()));
    this->type = types::type_integer;
    debug_symb_reinit_();
}

void manapi::json::set_decimal_() {
    delete_value_static(this->type, std::exchange(this->src, new DECIMAL ()));
    this->type = types::type_decimal;
    debug_symb_reinit_();
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::set_bigint_() {
    delete_value_static(this->type, std::exchange(this->src, new BIGINT ()));
    this->type = types::type_bigint;
    debug_symb_reinit_();
}
#endif

void manapi::json::set_nullptr_() {
    delete_value_static(this->type, std::exchange(this->src, nullptr));
    this->type = types::type_null;
    debug_symb_reinit_();
}

void manapi::json::set_pair_() {
    delete_value_static(this->type, std::exchange(this->src, new PAIR ()));
    this->type = types::type_pair;
    debug_symb_reinit_();
}

void manapi::json::set_object_(OBJECT val) {
    set_object_();
    as_object() = std::move(val);
}

void manapi::json::set_bool_(BOOLEAN val) {
    set_bool_();
    as_bool() = val;
}

void manapi::json::set_array_(ARRAY val) {
    set_array_();
    as_array() = std::move(val);
}

void manapi::json::set_string_(STRING val) {
    set_string_();
    as_string() = std::move(val);
}

void manapi::json::set_string_(STRING_VIEW val) {
    set_string_();
    as_string() = val;
}

void manapi::json::set_integer_(INTEGER val) {
    set_integer_();
    as_integer() = val;
}

void manapi::json::set_decimal_(DECIMAL val) {
    set_decimal_();
    as_decimal() = val;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
void manapi::json::set_bigint_(BIGINT val) {
    set_bigint_();
    as_bigint() = std::move(val);
}
#endif

void manapi::json::set_pair_(json first, json second) {
    set_pair_();

    as_pair_().first = std::move(first);
    as_pair_().second = std::move(second);
}

manapi::json &manapi::json::operator[](const STRING &key) {
    return this->at(key);
}

manapi::json &manapi::json::operator[](size_t index) {
    return this->at(index);
}

const manapi::json & manapi::json::operator[](const STRING &key) const {
    return this->at(key);
}

const manapi::json & manapi::json::operator[](size_t index) const {
    return this->at(index);
}

manapi::json &manapi::json::at(const std::string &key)  {
    if (this->type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &map = this->as_object();

    // if (!map.contains(key))
    // {
    //     THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));
    // }

    return map[key];
}

manapi::json &manapi::json::at(size_t index)  {
    if (this->type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &arr = this->as_array();

    if (arr.size() <= index)
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size())

    return arr.at(index);
}

const manapi::json & manapi::json::at(const std::string &key) const {
    if (this->type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &map = this->as_object();

    if (!map.contains(key))
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));

    return map.at(key);
}

const manapi::json & manapi::json::at(size_t index) const {
    if (this->type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &arr = this->as_array();

    if (arr.size() <= index)
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());

    return arr.at(index);
}

manapi::json& manapi::json::operator=(STRING str) {
    set_string_(std::move(str));
    debug_symb_reinit_();

    return *this;
}

manapi::json &manapi::json::operator=(BOOLEAN b) {
    set_bool_(b);

    return *this;
}

manapi::json &manapi::json::operator=(INTEGER num) {
    set_integer_(num);

    return *this;
}

manapi::json &manapi::json::operator=(DECIMAL num) {
    set_decimal_(num);

    return *this;
}

// manapi::json &manapi::json::operator=(const long long &num) {
//     return this->operator=(static_cast<INTEGER> (num));
// }

manapi::json &manapi::json::operator=(nullptr_t const &n) {
    set_nullptr_();

    return *this;
}

manapi::json &manapi::json::operator=(const char *str) {
    this->operator=(STRING(str));
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator=(BIGINT num) {
    set_bigint_(std::move(num));

    return *this;
}
#endif

manapi::json &manapi::json::operator=(const manapi::json &obj) {
    if (&obj != this)
    {

        switch (obj.type) {
            case type_string:
                set_string_(obj.as_string());
                break;
            case type_integer:
                set_integer_(obj.as_integer());
                break;
            case type_decimal:
                set_decimal_(obj.as_decimal());
                break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case type_bigint:
                set_bigint_(obj.as_bigint());
                break;
#endif
            case type_array:
            {
                set_array_(obj.as_array());
                break;
            }
            case type_object:
            {
                set_object_(obj.as_object());
                break;
            }
            case type_boolean:
                set_bool_(obj.as_bool());
                break;
            case type_null:
                set_nullptr_();
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

        obj.debug_symb_reinit_();
        debug_symb_reinit_();
    }

    return *this;
}

manapi::json &manapi::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::json (data);
}

manapi::json manapi::json::operator*(INTEGER num) const {
    auto n = *this;
    if (n.is_integer())
        n.as_integer() *= num;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
        n.as_bigint() *= num;
#endif
    else if (n.is_decimal())
        n.as_decimal() *= num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    return std::move(n);
}

manapi::json manapi::json::operator*(DECIMAL num) const {
    auto n = *this;

    if (n.is_integer())
        n.as_integer() += static_cast<INTEGER>(num);

#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (n.is_bigint())
        n.as_bigint() += num;

#endif
    else if (n.is_decimal())
        n.as_decimal() += num;

    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    return std::move(n);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json manapi::json::operator*(const BIGINT &num) const {
    auto n = *this;

    if (n.is_integer())
        n.as_integer() += num.integerify();
    else if (n.is_bigint())
        n.as_bigint() += num;
    else if (n.is_decimal())
        n.as_decimal() += num.decimalify();
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    return std::move(n);
}
#endif

manapi::json &manapi::json::operator*=(INTEGER num) {
    if (this->is_integer())
        this->as_integer() += num;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() += num;
#endif
    else if (this->is_decimal())
        this->as_decimal() += num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

manapi::json &manapi::json::operator*=(DECIMAL num) {
    if (this->is_integer())
        this->as_integer() += static_cast<INTEGER>(num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() += num;
#endif
    else if (this->is_decimal())
        this->as_decimal() += num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator*=(const BIGINT &num) {
    if (this->is_integer())
        this->as_integer() += num.integerify();
    else if (this->is_bigint())
        this->as_bigint() += num;
    else if (this->is_decimal())
        this->as_decimal() += num.decimalify();
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}
#endif

std::pair<std::map<std::string, manapi::json>::iterator, bool> manapi::json::insert(const OBJECT::value_type &v) {
    return this->insert ({v.first, v.second});
}

std::pair<std::map<std::string, manapi::json>::iterator, bool> manapi::json::insert(OBJECT::value_type &&v) {
    if (this->type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    return as_object().insert(std::forward<decltype(v)>(v));
}

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const STRING &key, manapi::json obj) {
    return this->insert ({key, obj});
}

void manapi::json::push_back(manapi::json::ARRAY::const_iterator begin, manapi::json::ARRAY::const_iterator end) {
    if (this->type != type_array) { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    for (auto it = begin; it != end; ++it)
        push_back(*it);
}

void manapi::json::push_back(manapi::json obj) {
    if (this->type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    as_array().push_back(std::move(obj));
}

void manapi::json::pop_back() {
    if (this->type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    as_array().pop_back();
}

int manapi::json::data_type() const MANAPIHTTP_NOEXCEPT {
    return this->type;
}

manapi::json manapi::json::object() {
    json obj;

    obj.set_object_();

    return std::move(obj);
}

manapi::json manapi::json::array() {
    // rvalue, arr not be destroyed
    json arr;

    arr.set_array_();

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

manapi::error::status_or<manapi::json> manapi::json::parse(STRING_VIEW data) {
    manapi::json b;
    auto res = b.parse_(data);
    if (!res.ok())
        return std::move(res);
    return std::move(b);
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

// std::map<std::string, manapi::json>::iterator manapi::json::find(const STRING &key) {
//     return this->as_object().find(key);
// }

std::map<std::string, manapi::json, std::less<>>::iterator manapi::json::find(STRING_VIEW key) {
    return this->as_object().find(key);
}

std::map<std::string, manapi::json>::const_iterator manapi::json::find(STRING_VIEW key) const {
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

manapi::json & manapi::json::first() {
    return this->as_pair_().first;
}

manapi::json & manapi::json::second() {
    return this->as_pair_().second;
}

const manapi::json & manapi::json::first() const {
    return this->as_pair_().first;
}

const manapi::json & manapi::json::second() const {
    return this->as_pair_().second;
}

bool manapi::json::contains(const std::string &key) const {
    return this->as_object().contains(key);
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

bool manapi::json::is_object() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_object;
}

bool manapi::json::is_array() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_array;
}

bool manapi::json::is_string() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_string;
}

bool manapi::json::is_integer() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_integer;
}

bool manapi::json::is_null() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_null;
}

bool manapi::json::is_decimal() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_decimal;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
bool manapi::json::is_bigint() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_bigint;
}
#endif

bool manapi::json::is_bool() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_boolean;
}

bool manapi::json::is_pair() const MANAPIHTTP_NOEXCEPT {
    return this->type == type_pair;
}

manapi::json::OBJECT & manapi::json::as_object_() const {
    if (this->type == type_object) {
        return *static_cast<json::OBJECT *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::OBJECT & manapi::json::as_object() const {
    return this->as_object_();
}

manapi::json::OBJECT & manapi::json::as_object() {
    return this->as_object_();
}

manapi::json::ARRAY & manapi::json::as_array_() const {
    if (this->type == type_array) {
        return *static_cast<json::ARRAY *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::ARRAY & manapi::json::as_array() const {
    return this->as_array_();
}

manapi::json::ARRAY & manapi::json::as_array() {
    return this->as_array_();
}

manapi::json::STRING & manapi::json::as_string_() const {
    if (this->type == type_string) {
        return *static_cast<json::STRING *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::STRING & manapi::json::as_string() const {
    return this->as_string_();
}

manapi::json::STRING & manapi::json::as_string() {
    return this->as_string_();
}

manapi::json::INTEGER & manapi::json::as_integer_() const {
    if (this->type == type_integer) {
        return *static_cast<json::INTEGER *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::INTEGER & manapi::json::as_integer() const {
    return this->as_integer_();
}

manapi::json::INTEGER & manapi::json::as_integer() {
    return this->as_integer_();
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT & manapi::json::as_bigint_() const {
    if (this->type == type_bigint) {
        return *static_cast<json::BIGINT *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}
#endif

manapi::json::PAIR & manapi::json::as_pair_() const {
    if (this->type == type_pair) {
        return *static_cast<json::PAIR *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
const manapi::json::BIGINT & manapi::json::as_bigint() const {
    return this->as_bigint_();
}
#endif

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT & manapi::json::as_bigint() {
    return this->as_bigint_();
}
#endif

manapi::json::BOOLEAN & manapi::json::as_bool_() const {
    if (this->type == type_boolean) {
        return *static_cast<json::BOOLEAN *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::BOOLEAN & manapi::json::as_bool() const {
    return this->as_bool_();
}

manapi::json::BOOLEAN & manapi::json::as_bool() {
    return this->as_bool_();
}

manapi::json::DECIMAL & manapi::json::as_decimal_() const {
    if (this->type == type_decimal) {
        return *static_cast<json::DECIMAL *> (src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

const manapi::json::DECIMAL & manapi::json::as_decimal() const {
    return this->as_decimal_();
}

manapi::json::DECIMAL & manapi::json::as_decimal() {
    return this->as_decimal_();
}

manapi::json::NULLPTR manapi::json::as_null() const {
    if (this->type == type_null) {
        return nullptr;
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::OBJECT manapi::json::as_object_cast() const {
    if (this->type == type_object) {
        return as_object();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::ARRAY manapi::json::as_array_cast() const {
    if (this->type == type_array) {
        return as_array();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::STRING manapi::json::as_string_cast() const {
    if (this->type == type_string) {
        return as_string();
    }
    if (this->type == type_integer) {
        return std::to_string(as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (this->type == type_bigint) {
        return as_bigint().stringify();
    }
#endif
    if (this->type == type_decimal) {
        return std::to_string(as_decimal());
    }
    if (this->type == type_null) {
        return "0";
    }
    if (this->type == type_boolean) {
        return std::to_string(static_cast<int>(this->as_bool()));
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::INTEGER manapi::json::as_integer_cast() const {
    if (this->type == type_string) {
        return std::stoll(as_string());
    }
    if (this->type == type_integer) {
        return as_integer();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (this->type == type_bigint) {
        return as_bigint().integerify();
    }
#endif
    if (this->type == type_decimal) {
        // long double to long long
        return static_cast <json::INTEGER> (as_decimal());
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::NULLPTR manapi::json::as_null_cast() const {
    if (this->type == type_null) {
        return as_null();
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json::DECIMAL manapi::json::as_decimal_cast() const {
    if (this->type == type_decimal) {
        return as_decimal();
    }
    if (this->type == type_integer) {
        return static_cast<json::DECIMAL> (as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (this->type == type_bigint) {
        return as_bigint().decimalify();
    }
#endif
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT manapi::json::as_bigint_cast() const {
    if (this->type == type_bigint) {
        return as_bigint();
    }
    if (this->type == type_integer) {
        return std::move(bigint (as_integer()));
    }
    if (this->type == type_decimal) {
        return std::move(bigint (as_decimal()));
    }
    if (this->type == type_string) {
        return std::move(bigint (as_string()));
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}
#endif

manapi::json::BOOLEAN manapi::json::as_bool_cast() const {
    if (this->type == type_boolean) {
        return as_bool();
    }
    if (this->type == type_array) {
        return !as_array().empty();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (this->type == type_bigint) {
        return as_bigint() != 0;
    }
#endif
    if (this->type == type_object) {
        return !as_object().empty();
    }
    if (this->type == type_string) {
        return !as_string().empty();
    }
    if (this->type == type_decimal) {
        return as_decimal() != 0;
    }
    if (this->type == type_integer) {
        return as_integer() != 0;
    }
    if (this->type == type_null) {
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
    if (this->type != x.type)
    {
        return false;
    }

    switch (this->type)
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
#ifdef MANAPIHTTP_BIGINT_SUPPORT
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

#endif

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