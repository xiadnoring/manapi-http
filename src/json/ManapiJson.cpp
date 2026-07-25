#include <memory.h>
#include <format>
#include <utility>

#include "ManapiDebug.hpp"
#include "ManapiBigint.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "json/ManapiJson.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "std/ManapiSlice.hpp"

#include "../include/ManapiUtils.hpp"
#include "../include/std/ManapiBeforeDelete.hpp"
#include "../include/ManapiJsonMaskUtils.hpp"

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 5232)
#endif

#define RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) manapi::json_parse_exception (errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type__) throw RETHROW_MANAPIHTTP_JSON_ERROR(manapi::ERR_JSON_UNSUPPORTED_TYPE, "json object with type {}({}) could not use func: {}", \
manapi::json_type_to_str(m_type__), static_cast <int> (m_type__), __FUNCTION__)
#define THROW_MANAPIHTTP_JSON_MISSING_FUNCTION THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(this->m_type)
#define THROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) throw RETHROW_MANAPIHTTP_JSON_ERROR(errnum, msg, __VA_ARGS__)

static void json_delete_value_static(int type, void *src) MANAPIHTTP_NOEXCEPT {
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
        case manapi::json::type_slice:
            delete static_cast<manapi::slice *> (src);
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
        case manapi::json::type_source:
            delete static_cast<manapi::json_source *> (src);
        break;
        default:
            manapi_log_error("%s: %s(%d)", "json(bug)", "invalid data type to delete", static_cast<int>(type));
            assert(false && "invalid data type to delete");
    }
}

static void json_delete_value(int &m_type, manapi::json::data_t &m_data) MANAPIHTTP_NOEXCEPT {
    ::json_delete_value_static(
        std::exchange(m_type, manapi::json::types::type_null), std::exchange(m_data.src, nullptr));
}

static void json_set_source_(manapi::json_source *z, int &m_type, manapi::json::data_t &m_data) {
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_source;
}

static void json_set_object_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::OBJECT();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_object;
}

static void json_set_bool_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::BOOLEAN();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_boolean;

}

static void json_set_array_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::ARRAY();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_array;

}

static void json_set_string_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::STRING();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_string;

}

static void json_set_integer_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::INTEGER();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_integer;

}

static void json_set_decimal_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::DECIMAL();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_decimal;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
static void json_set_bigint_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::BIGINT();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_bigint;
}
#endif

static void json_set_nullptr_(int &m_type, manapi::json::data_t &m_data) {
    ::json_delete_value(m_type, m_data);
    m_data.src = nullptr;
    m_type = manapi::json::types::type_null;
}

static void json_set_pair_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::json::PAIR ();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_pair;
}

static void json_set_slice_(int &m_type, manapi::json::data_t &m_data) {
    auto const z = new manapi::slice ();
    ::json_delete_value(m_type, m_data);
    m_data.src = z;
    m_type = manapi::json::types::type_slice;
}

static void json_set_object_(int &m_type, manapi::json::data_t &m_data, manapi::json::OBJECT val) {
    ::json_set_object_(m_type, m_data);
    *m_data.object_src_ = std::move(val);
}

static void json_set_bool_(int &m_type, manapi::json::data_t &m_data, manapi::json::BOOLEAN val) {
    ::json_set_bool_(m_type, m_data);
    *m_data.bool_src_ = val;
}

static void json_set_array_(int &m_type, manapi::json::data_t &m_data, manapi::json::ARRAY val) {
    ::json_set_array_(m_type, m_data);
    *m_data.array_src_ = std::move(val);
}

static void json_set_string_(int &m_type, manapi::json::data_t &m_data, manapi::json::STRING val) {
    ::json_set_string_(m_type, m_data);
    *m_data.string_src_ = std::move(val);
}

static void json_set_string_(int &m_type, manapi::json::data_t &m_data, manapi::json::STRING_VIEW val) {
    ::json_set_string_(m_type, m_data);
    *m_data.string_src_ = val;
}

static void json_set_integer_(int &m_type, manapi::json::data_t &m_data, manapi::json::INTEGER val) {
    ::json_set_integer_(m_type, m_data);
    *m_data.integer_src_ = val;
}

static void json_set_decimal_(int &m_type, manapi::json::data_t &m_data, manapi::json::DECIMAL val) {
    ::json_set_decimal_(m_type, m_data);
    *m_data.decimal_src_ = val;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
static void json_set_bigint_(int &m_type, manapi::json::data_t &m_data, manapi::json::BIGINT val) {
    ::json_set_bigint_(m_type, m_data);
    *m_data.bigint_src_ = std::move(val);
}
#endif

static void json_set_pair_(int &m_type, manapi::json::data_t &m_data, manapi::json &&first, manapi::json &&second) {
    ::json_set_pair_(m_type, m_data);

    m_data.pair_src_->first = std::forward<decltype(first)>(first);
    m_data.pair_src_->second = std::forward<decltype(second)>(second);
}

static void json_set_pair_(int &m_type, manapi::json::data_t &m_data, const manapi::json &first, const manapi::json &second) {
    ::json_set_pair_(m_type, m_data);

    m_data.pair_src_->first = (first);
    m_data.pair_src_->second = (second);
}

static void json_set_slice_(int &m_type, manapi::json::data_t &m_data, manapi::slice &&n) {
    ::json_set_slice_(m_type, m_data);

    *m_data.slice_src_ = std::forward<decltype(n)>(n);
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::INTEGER num) {
    ::json_set_integer_(m_type, m_data, num);
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,int num) {
    ::json_parse_(m_type, m_data, static_cast<manapi::json::INTEGER> (num));
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::DECIMAL num) {
    ::json_set_decimal_(m_type, m_data, num);
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,double num) {
    ::json_parse_(m_type, m_data, static_cast<manapi::json::DECIMAL> (num));
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::BIGINT num) {
    ::json_set_bigint_(m_type, m_data, std::move(num));
}
#endif

static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::OBJECT obj) {
    ::json_set_object_(m_type, m_data, std::move(obj));
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::ARRAY arr) {
    ::json_set_array_(m_type, m_data, std::move(arr));
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data,manapi::json::BOOLEAN val) {
    ::json_set_bool_(m_type, m_data, val);
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data, const manapi::json::NULLPTR &) {
    ::json_set_nullptr_(m_type, m_data);
}

static void json_parse_(int &m_type, manapi::json::data_t &m_data, std::size_t num) {
    ::json_parse_ (m_type, m_data, static_cast<manapi::json::INTEGER> (num));
}

static manapi::json::OBJECT & json_as_object_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_object) {
        return *static_cast<manapi::json::OBJECT *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::json_source & json_as_source_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_source) {
        return *static_cast<manapi::json_source *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::json::ARRAY & json_as_array_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_array) {
        return *static_cast<manapi::json::ARRAY *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::json::STRING & json_as_string_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_string) {
        return *static_cast<manapi::json::STRING *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::json::INTEGER & json_as_integer_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_integer) {
        return *static_cast<manapi::json::INTEGER *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}
#ifdef MANAPIHTTP_BIGINT_SUPPORT
static manapi::json::BIGINT & json_as_bigint_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_bigint) {
        return *static_cast<manapi::json::BIGINT *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}
#endif

static manapi::json::PAIR & json_as_pair_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_pair) {
        return *static_cast<manapi::json::PAIR *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}
static manapi::json::BOOLEAN & json_as_bool_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_boolean) {
        return *static_cast<manapi::json::BOOLEAN *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::slice & json_as_slice_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_slice) {
        return *static_cast<manapi::slice *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static manapi::json::DECIMAL & json_as_decimal_(const int &m_type, const manapi::json::data_t &m_data) {
    if (m_type == manapi::json::type_decimal) {
        return *static_cast<manapi::json::DECIMAL *> (m_data.src);
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(m_type);
}

static void json_copy_value(int &m_type, manapi::json::data_t &m_data, int o_type, const manapi::json::data_t &o_data) {
    switch (o_type) {
        case manapi::json::type_string:
            ::json_set_string_(m_type, m_data,*o_data.string_src_);
        break;
        case manapi::json::type_integer:
            ::json_set_integer_(m_type, m_data,*o_data.integer_src_);
        break;
        case manapi::json::type_decimal:
            ::json_set_decimal_(m_type, m_data,*o_data.decimal_src_);
        break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case manapi::json::type_bigint:
            ::json_set_bigint_(m_type, m_data,*o_data.bigint_src_);
        break;
#endif
        case manapi::json::type_array: {
            ::json_set_array_(m_type, m_data,*o_data.array_src_);
            break;
        }
        case manapi::json::type_object: {
            ::json_set_object_(m_type, m_data,*o_data.object_src_);
            break;
        }
        case manapi::json::type_boolean:
            ::json_set_bool_(m_type, m_data, *o_data.bool_src_);
        break;
        case manapi::json::type_null:
            ::json_set_nullptr_(m_type, m_data);
        break;
        case manapi::json::type_pair:
            ::json_set_pair_(m_type, m_data, o_data.pair_src_->first, o_data.pair_src_->second);
        break;
        case manapi::json::type_slice: {
            ::json_set_slice_(m_type, m_data, o_data.slice_src_->copy());
            break;
        }
        case manapi::json::type_source:
            ::json_set_source_(o_data.source_src_->copy(), m_type, m_data);
        break;
        default:
            break;
    }
}

manapi::json::STRING json_as_string_cast(const manapi::json&v) {
    const auto type = v.data_type();

    if (type == manapi::json::type_string) {
        return v.as_string();
    }
    if (type == manapi::json::type_slice) {
        return v.as_slice().to_string();
    }
    if (type == manapi::json::type_integer) {
        return std::to_string(v.as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == manapi::json::type_bigint) {
        return v.as_bigint().stringify();
    }
#endif
    if (type == manapi::json::type_decimal) {
        return std::to_string(v.as_decimal());
    }
    if (type == manapi::json::type_null) {
        return "null";
    }
    if (type == manapi::json::type_boolean) {
        if (v.as_bool())
            return "true";
        return "false";
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

static manapi::json::INTEGER json_as_integer_cast(const manapi::json&v) {
    const auto type = v.data_type();

    if (type == manapi::json::type_string) {
        return std::stoll(v.as_string());
    }
    if (type == manapi::json::type_integer) {
        return v.as_integer();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == manapi::json::type_bigint) {
        return v.as_bigint().integerify();
    }
#endif
    if (type == manapi::json::type_decimal) {
        // long double to long long
        return static_cast <manapi::json::INTEGER> (v.as_decimal());
    }
    if (type == manapi::json::type_boolean) {
        return static_cast <manapi::json::INTEGER> (v.as_bool());
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

static manapi::json::NULLPTR json_as_null_cast(const manapi::json&v) {
    const auto type = v.data_type();

    if (type == manapi::json::type_null) {
        return v.as_null();
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

static manapi::json::DECIMAL json_as_decimal_cast(const manapi::json&v) {
    const auto type = v.data_type();

    if (type == manapi::json::type_decimal) {
        return v.as_decimal();
    }
    if (type == manapi::json::type_integer) {
        return static_cast<manapi::json::DECIMAL> (v.as_integer());
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == manapi::json::type_bigint) {
        return v.as_bigint().decimalify();
    }
#endif

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
static manapi::bigint json_as_bigint_cast(const manapi::json&v) {
    const auto type = v.data_type();

    if (type == manapi::json::type_bigint) {
        return v.as_bigint();
    }
    if (type == manapi::json::type_integer) {
        return manapi::bigint (v.as_integer());
    }
    if (type == manapi::json::type_decimal) {
        return manapi::bigint (v.as_decimal());
    }
    if (type == manapi::json::type_string) {
        return manapi::bigint (v.as_string());
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}
#endif

static bool json_as_bool_cast(const manapi::json &v) {
    auto const type = v.data_type();
    if (type == manapi::json::type_boolean) {
        return v.as_bool();
    }
    if (type == manapi::json::type_array) {
        return !v.as_array().empty();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == manapi::json::type_bigint) {
        return v.as_bigint() != 0;
    }
#endif
    if (type == manapi::json::type_object) {
        return !v.as_object().empty();
    }
    if (type == manapi::json::type_string) {
        return !v.as_string().empty();
    }
    if (type == manapi::json::type_decimal) {
        return v.as_decimal() != 0;
    }
    if (type == manapi::json::type_integer) {
        return v.as_integer() != 0;
    }
    if (type == manapi::json::type_null) {
        return false;
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

static manapi::slice json_as_slice_cast(const manapi::json &v) {
    auto const type = v.data_type();
    if (type == manapi::json::type_slice) {
        return v.as_slice().copy();
    }
    if (type == manapi::json::type_string) {
        return manapi::slice (v.as_string());
    }
    if (type == manapi::json::type_integer) {
        return manapi::slice(std::to_string(v.as_integer()));
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == manapi::json::type_bigint) {
        return manapi::slice(v.as_bigint().stringify());
    }
#endif
    if (type == manapi::json::type_decimal) {
        return manapi::slice(std::to_string(v.as_decimal()));
    }
    if (type == manapi::json::type_null) {
        return manapi::slice("null");
    }
    if (type == manapi::json::type_boolean) {
        if (v.as_bool())
            return manapi::slice("true");
        return manapi::slice("false");
    }

    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION2(type);
}

manapi::json::json() : m_data{} {
    this->m_data.src = nullptr;
    this->m_type = type_null;
}

manapi::json::json(STRING_VIEW str) : json() {
    // manapi::json_builder_valid_utf_string(str).unwrap();
    ::json_set_string_(this->m_type, this->m_data, str);
}

manapi::json::json(STRING str) : json() {
    // json_builder_valid_utf_string(str).unwrap();
    ::json_set_string_(this->m_type, this->m_data, std::move(str));
}

manapi::json::json(manapi::slice &&sv) : json() {
    ::json_set_slice_(this->m_type, this->m_data, std::move(sv));
}

manapi::json::json(const manapi::slice_base &sv) : json() {
    auto nsv = manapi::async::memory_fabric()->slice(sv.size()).unwrap();
    nsv.copy_from(sv, 0, 0, sv.size()).unwrap();
    ::json_set_slice_(this->m_type, this->m_data, std::move(nsv));
}

manapi::json::json(INTEGER num) : json() {
    ::json_parse_(this->m_type, this->m_data, num);
}

manapi::json::json(const manapi::json &other) : json() {
    *this = other;
}

manapi::json::json(json &&other) MANAPIHTTP_NOEXCEPT : json() {
    ::json_delete_value(this->m_type, this->m_data);

    this->m_data.src = std::exchange(other.m_data.src, nullptr);
    this->m_type = std::exchange(other.m_type, types::type_null);


}

manapi::json::json(const char *plain_text) : json() {
    ::json_set_string_(this->m_type, this->m_data, STRING_VIEW{plain_text});
}

manapi::json::json(DECIMAL num) : json() {
    ::json_parse_(this->m_type, this->m_data, num);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::json(BIGINT num) : json() {
    ::json_parse_(this->m_type, this->m_data, std::move(num));
}
#endif

manapi::json::json(BOOLEAN value) : json() {
    ::json_parse_(this->m_type, this->m_data, value);
}

manapi::json::json(OBJECT obj) : json() {
    ::json_parse_(this->m_type, this->m_data, std::move(obj));
}

manapi::json::json(ARRAY arr) : json() {
    ::json_parse_(this->m_type, this->m_data, std::move(arr));
}

manapi::json::json(const nullptr_t &n) : json() {
    ::json_parse_(this->m_type, this->m_data, n);
}

manapi::json::json (const std::initializer_list<json> &data) : json() {
    if (data.size() == 0)
        return;
    if (data.size() == 1 && data.begin()->m_type != type_pair)
        this->operator=(*data.begin());
    else if (data.size() == 2 && data.begin()->is_string()) {
        auto it = data.begin();
        auto key = it++;
        auto value = manapi::json(*it);
        if (value.m_type == type_pair) {
            json arr = json::array ();
            arr.push_back(std::move(static_cast <PAIR *> (value.m_data.src)->first));
            arr.push_back(std::move(static_cast <PAIR *> (value.m_data.src)->second));
            value = std::move(arr);
        }
        ::json_set_pair_(this->m_type, this->m_data, manapi::json(*key), std::move(value));
    }
    else {
        bool map = true;
        for (const auto &it: data) {
            if (it.m_type != type_pair) {
                map = false;
                break;
            }
        }
        if (map) *this = json::object(data);
        else *this = json::array(data);
    }
}

manapi::json::~json() {
    ::json_delete_value(this->m_type, this->m_data);
}

static std::size_t json_dump_int_calc_size (manapi::json::INTEGER z) {
    if (z) {
        return static_cast<std::size_t>(std::log10(std::abs(z))) + 1 + (z < 0);
    }
    return 1;
}

static std::size_t json_dump_decimal_calc_size (manapi::json::DECIMAL z) {
    std::size_t l = 0;

    if (z < 0) {
        l++;
        z = -z;
    }

    auto const cz = static_cast<manapi::json::INTEGER>(z);
    l += json_dump_int_calc_size(cz);

    z -= static_cast<manapi::json::DECIMAL>(cz);
    l ++;

    uint32_t zeros = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        z *= 10;
        auto d = static_cast<int>(z);
        if (d) zeros = 0;
        else zeros++;
        l++;
        z -= d;
    }

    l -= zeros;

    return l;
}

static void json_dump_int (manapi::json_dump_buffer *p, manapi::json::INTEGER z) {
    static_assert( sizeof (manapi::json::INTEGER) <= 8 );

    char buff[64];
    auto pb = buff;

    if (!z) {
        p->push_back('0');
        return;
    }

    if (z < 0) {
        p->push_back('-');
        z = -z;
    }

    while (z) {
        *pb++ = static_cast<char>('0' + z % 10);
        z /= 10;
    }

    while (pb != buff) {
        p->push_back(*(--pb));
    }
}

static void json_dump_decimal (manapi::json_dump_buffer *p, manapi::json::DECIMAL z) {
    std::size_t l = 0;

    if (z < 0) {
        p->push_back('-');
        z = -z;
    }

    auto in = static_cast<manapi::json::INTEGER> (z);
    z -= in;

    ::json_dump_int(p, in);

    if (z > 0) {
        char g[6];
        uint32_t zeros = 0;

        for (int i = 0; i < 6; i++) {
            z *= 10;
            auto d = static_cast<int> (z);
            if (!d) zeros++;
            else zeros = 0;
            g[i] = static_cast<char>('0' + d);
            z -= d;
        }

        if (zeros != 6) {
            p->push_back('.');
            zeros = 6 - zeros;
            for (uint32_t i = 0; i < zeros; i++) {
                p->push_back(g[i]);
            }
        }
    }
}

static std::size_t json_dump_arr_calc_size (manapi::json_dump_data_t *data) {
    std::size_t sz = 0;
    auto *b = &data->paths.back();

    if (data->flags & manapi::JSON_DUMP_FLAG_INIT) {
        sz++; // [
        b->i = 0;

        if (b->p->empty()) {
            return sz;
        }
    }

    if (b->i < b->p->size()) {

        if (!(data->flags & manapi::JSON_DUMP_FLAG_INIT)) {
            sz++; // ,
        }

        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS) {
            sz++; // \n
            data->shift += data->spaces;
        }

        data->paths.push_back(manapi::json_dump_path_t {
            .p = &b->p->at(b->i)
        });

        data->flags |= manapi::JSON_DUMP_FLAG_PUSHED|manapi::JSON_DUMP_FLAG_SHIFT;

        b = &data->paths[data->paths.size() - 2];

        b->i++;
    }
    else {
        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS && !b->p->empty()) {
            sz ++; // \n
            sz += data->shift;
        }
        data->shift -= data->spaces;
        sz++; // ]
        data->paths.pop_back();
    }

    return sz;
}

static std::size_t json_dump_obj_calc_size (manapi::json_dump_data_t *data) {
    std::size_t sz = 0;

    auto *b = &data->paths.back();
    if (data->flags & manapi::JSON_DUMP_FLAG_INIT) {
        sz++; // {
        b->it = b->p->as_object().begin();

        if (b->p->empty()) {
            return sz;
        }
    }

    if (b->it != b->p->as_object().end()) {
        if (!(data->flags & manapi::JSON_DUMP_FLAG_INIT)) {
            sz++; // ,
        }

        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS) {
            sz ++; // \n
            data->shift += data->spaces;
            sz += data->shift;
        }

        sz += manapi::unicode::escape_string_size(b->it->first);
        sz++; // :
        data->paths.push_back(manapi::json_dump_path_t {
            .p = &b->it->second
        });
        data->flags |= manapi::JSON_DUMP_FLAG_PUSHED;

        b = &data->paths[data->paths.size() - 2];
        ++b->it;
    }
    else {
        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS && !b->p->empty()) {
            sz ++; // \n
            sz += data->shift;
        }
        data->shift -= data->spaces;

        sz++; // }
        data->paths.pop_back();
    }

    return sz;
}

static void json_dump_str (manapi::json_dump_buffer *p, std::string_view str) {
    manapi::unicode::escape_string(str, p);
}

static void json_dump_arr (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) {
    if (data->flags & manapi::JSON_DUMP_FLAG_INIT) {
        p->push_back( '[');
        data->paths.back().i = 0;

        if (data->paths.back().p->empty()) {
            return;
        }
    }

    auto *b = &data->paths.back();

    if (b->i < b->p->size()) {

        if (!(data->flags & manapi::JSON_DUMP_FLAG_INIT)) {
            p->push_back(',');
        }

        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS) {
            data->shift += data->spaces;
            p->push_back('\n');
        }

        data->paths.push_back(manapi::json_dump_path_t {
            .p = &b->p->at(b->i)
        });
        data->flags |= manapi::JSON_DUMP_FLAG_PUSHED|manapi::JSON_DUMP_FLAG_SHIFT;

        b = &data->paths[data->paths.size() - 2];

        b->i++;
    }
    else {
        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS && !b->p->empty()) {
            p->push_back('\n');
            for (uint32_t i = 0; i < data->shift; i++)
                p->push_back(' ');
        }
        data->shift -= data->spaces;
        p->push_back(']');
        data->paths.pop_back();
    }
}

static void json_dump_obj (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) {
    auto *b = &data->paths.back();
    if (data->flags & manapi::JSON_DUMP_FLAG_INIT) {
        p->push_back('{');
        b->it = b->p->as_object().begin();

        if (data->paths.back().p->empty()) {
            return;
        }
    }

    if (b->it != b->p->as_object().end()) {
        if (!(data->flags & manapi::JSON_DUMP_FLAG_INIT)) {
            p->push_back( ',');
        }
        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS) {
            data->shift += data->spaces;
            p->push_back('\n');
            for (uint32_t i =0 ; i < data->shift; i++)
                p->push_back(' ');
        }
        ::json_dump_str(p, b->it->first);
        p->push_back(':');
        data->paths.push_back(manapi::json_dump_path_t {
            .p = &b->it->second
        });
        data->flags |= manapi::JSON_DUMP_FLAG_PUSHED;

        b = &data->paths[data->paths.size() - 2];
        ++b->it;
    }
    else {
        if (data->flags & manapi::JSON_DUMP_FLAG_GAPS && !b->p->empty()) {
            p->push_back('\n');
            for (uint32_t i = 0; i < data->shift; i++)
                p->push_back(' ');
        }
        data->shift -= data->spaces;
        p->push_back('}');
        data->paths.pop_back();
    }
}


static void json_dump_bool (manapi::json_dump_buffer *p, bool data) {
    if (data) p->push_back("true", sizeof ("true") - 1);
    else p->push_back("false", sizeof ("false") - 1);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
static void json_dump_bigint (manapi::json_dump_buffer *p, const manapi::bigint&data) {
    auto z = data.stringify();
    p->push_back(z.data(), z.size());
}
static std::size_t json_dump_bigint_calc_size (const manapi::bigint&data) {
    auto z = data.stringify();
    return z.size();
}
#endif

static void json_dump_null (manapi::json_dump_buffer *p) {
    p->push_back("null", sizeof("null")-1);
}

static void json_dump_calc_size (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) {
    auto &b = data->paths.back();

    if (data->flags & manapi::JSON_DUMP_FLAG_SHIFT) {
        data->sz += data->shift;
        data->flags ^= manapi::JSON_DUMP_FLAG_SHIFT;
    }

    switch (b.p->data_type()) {
        case manapi::json::type_null: data->sz += sizeof ("null") - 1; break;
        case manapi::json::type_number: throw manapi::exception (manapi::ERR_UNIMPLEMENTED, "json::dump():type_number");
        case manapi::json::type_string: data->sz += manapi::unicode::escape_string_size(b.p->as_string()); break;
        case manapi::json::type_decimal: data->sz += ::json_dump_decimal_calc_size(b.p->as_decimal()); break;
        case manapi::json::type_boolean: data->sz += (b.p->as_bool() ? sizeof ("true") - 1 : sizeof ("false") - 1); break;
        case manapi::json::type_object: data->sz += ::json_dump_obj_calc_size (data); return;
        case manapi::json::type_array: data->sz += ::json_dump_arr_calc_size (data); return;
        case manapi::json::type_integer: data->sz += ::json_dump_int_calc_size(b.p->as_integer()); break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case manapi::json::type_bigint: data->sz += ::json_dump_bigint_calc_size(b.p->as_bigint());  break;
#endif
        case manapi::json::type_slice: data->sz += manapi::unicode::escape_string_size(&b.p->as_slice()); break;
        case manapi::json::type_source: data->sz += b.p->as_source().dump_size(p, data); return;
        default: break;
    }

    data->paths.pop_back();
    data->shift -= data->spaces;
}

static void json_dump_stringify (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) {
    auto &b = data->paths.back();

    if (data->flags & manapi::JSON_DUMP_FLAG_SHIFT) {
        for (uint32_t i = 0; i < data->shift; i++)
            p->push_back(' ');
        data->flags ^= manapi::JSON_DUMP_FLAG_SHIFT;
    }

    switch (b.p->data_type()) {
        case manapi::json::type_null: ::json_dump_null(p); break;
        case manapi::json::type_string: ::json_dump_str(p, b.p->as_string()); break;
        case manapi::json::type_decimal: ::json_dump_decimal(p, b.p->as_decimal()); break;
        case manapi::json::type_boolean: ::json_dump_bool(p, b.p->as_bool()); break;
        case manapi::json::type_object: ::json_dump_obj(p, data); return;
        case manapi::json::type_array: ::json_dump_arr(p, data); return;
        case manapi::json::type_integer: ::json_dump_int(p, b.p->as_integer()); break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case manapi::json::type_bigint: ::json_dump_bigint(p, b.p->as_bigint());break;
#endif
        case manapi::json::type_source: b.p->as_source().dump(p, data); return;
        case manapi::json::type_slice: manapi::unicode::escape_string(&b.p->as_slice(), p); break;
        default: break;
    }

    data->paths.pop_back();
    data->shift -= data->spaces;
}

static void manapi_json_dump (manapi::json_dump_data_t *data, const manapi::json *n, std::string *zs, manapi::slice *zsv) {
    void (*cb) (manapi::json_dump_buffer *, manapi::json_dump_data_t *) = json_dump_calc_size;
    manapi::json_dump_buf_str str(nullptr);
    manapi::json_dump_buf_prealloc_sv sv(nullptr);
    manapi::json_dump_buffer *buf = nullptr;
    auto const shift = data->shift;

    if (data->spaces || data->shift)
        data->flags |= manapi::JSON_DUMP_FLAG_GAPS;

    while (true) {
        data->paths.push_back(manapi::json_dump_path_t {
            .p = n
        });

        data->flags |= manapi::JSON_DUMP_FLAG_PUSHED|manapi::JSON_DUMP_FLAG_SHIFT;

        while (!data->paths.empty()) {
            if (data->flags & manapi::JSON_DUMP_FLAG_PUSHED) {
                data->flags = (data->flags | manapi::JSON_DUMP_FLAG_INIT) ^ manapi::JSON_DUMP_FLAG_PUSHED;

                cb (buf, data);

                data->flags ^= manapi::JSON_DUMP_FLAG_INIT;
            }
            else {
                cb (buf, data);
            }
        }

        if (cb == ::json_dump_stringify)
            break;

        cb = ::json_dump_stringify;
        data->shift = shift;

        if (zs) {
            zs->reserve(data->sz);
            str.set(zs);
            buf = &str;

        }
        else if (zsv) {
            zsv->resize(data->sz).unwrap();
            sv.set(zsv);
            buf = &sv;
        }
        else {
            assert(false && "invalid argument");
        }
    }
}

std::string manapi::json::dump(uint32_t spaces, uint32_t shift) const {
    std::string res;
    manapi::json_dump_data_t data{};
    data.spaces = spaces;
    data.shift = shift;
    ::manapi_json_dump (&data, this, &res, nullptr);
    return std::move(res);
}

void manapi::json::slice(manapi::slice *sv, uint32_t spaces, uint32_t shift) const {
    manapi::json_dump_data_t data{};
    data.spaces = spaces;
    data.shift = shift;
    ::manapi_json_dump(&data, this, nullptr, sv);
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
    if (this->m_type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &map = this->as_object();
    return map[key];
}

manapi::json &manapi::json::at(size_t index)  {
    if (this->m_type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &arr = this->as_array();

    if (arr.size() <= index)
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size())

    return arr.at(index);
}

const manapi::json & manapi::json::at(const std::string &key) const {
    if (this->m_type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &map = this->as_object();

    if (!map.contains(key))
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_NO_SUCH_KEY, "No such key. ({})", unicode::escape_string(key));

    return map.at(key);
}

const manapi::json & manapi::json::at(size_t index) const {
    if (this->m_type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    auto &arr = this->as_array();

    if (arr.size() <= index)
        THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_OUT_OF_RANGE, "Out of range. Index: {}. Size: {}", index, arr.size());

    return arr.at(index);
}

manapi::json& manapi::json::operator=(STRING str) {
    ::json_set_string_(this->m_type, this->m_data, std::move(str));

    return *this;
}

manapi::json &manapi::json::operator=(BOOLEAN b) {
    ::json_set_bool_(this->m_type, this->m_data, b);

    return *this;
}

manapi::json &manapi::json::operator=(INTEGER num) {
    ::json_set_integer_(this->m_type, this->m_data, num);

    return *this;
}

manapi::json &manapi::json::operator=(DECIMAL num) {
    ::json_set_decimal_(this->m_type, this->m_data, num);

    return *this;
}

// manapi::json &manapi::json::operator=(const long long &num) {
//     return this->operator=(static_cast<INTEGER> (num));
// }

manapi::json &manapi::json::operator=(nullptr_t const &n) {
    ::json_set_nullptr_(this->m_type, this->m_data);

    return *this;
}

manapi::json &manapi::json::operator=(const char *str) {
    this->operator=(STRING(str));
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator=(BIGINT num) {
    ::json_set_bigint_(this->m_type, this->m_data, std::move(num));

    return *this;
}
#endif

manapi::json &manapi::json::operator=(const manapi::json &obj) {
    if (&obj != this) {
        ::json_copy_value(this->m_type, this->m_data, obj.m_type, obj.m_data);
    }

    return *this;
}

manapi::json & manapi::json::operator=(json &&obj) MANAPIHTTP_NOEXCEPT {
    if (&obj != this) {
        manapi::json tmp (std::forward<decltype(obj)>(obj));

        ::json_delete_value(this->m_type, this->m_data);

        this->m_data.src = tmp.m_data.src;
        this->m_type = tmp.m_type;

        tmp.m_data.src = nullptr;
        tmp.m_type = type_null;
    }

    return *this;
}

manapi::json &manapi::json::operator=(const std::initializer_list <json> &data) {
    return *this = manapi::json (data);
}

manapi::json & manapi::json::operator=(manapi::slice &&sv) {
    ::json_set_slice_(this->m_type, this->m_data, std::move(sv));
    return *this;
}

manapi::json & manapi::json::operator=(const manapi::slice_base &sv) {
    auto nsv = manapi::async::memory_fabric()->slice(sv.size()).unwrap();
    nsv.copy_from(sv, 0, 0, sv.size()).unwrap();
    ::json_set_slice_(this->m_type, this->m_data, std::move(nsv));
    return *this;
}

manapi::json manapi::json::operator*(INTEGER num) const {
    auto n = *this;
    n *= num;
    return std::move(n);
}

manapi::json manapi::json::operator*(DECIMAL num) const {
    auto n = *this;
    n *= num;
    return std::move(n);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json manapi::json::operator*(const BIGINT &num) const {
    auto n = *this;
    n *= num;
    return std::move(n);
}
#endif

manapi::json &manapi::json::operator*=(INTEGER num) {
    if (this->is_integer())
        this->as_integer() *= num;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() *= num;
#endif
    else if (this->is_decimal())
        this->as_decimal() *= num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

manapi::json &manapi::json::operator*=(DECIMAL num) {
    if (this->is_integer())
        this->as_integer() *= static_cast<INTEGER>(num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() *= num;
#endif
    else if (this->is_decimal())
        this->as_decimal() *= num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

manapi::json manapi::json::operator/(INTEGER num) const {
    auto n = *this;
    n /= num;
    return std::move(n);
}

manapi::json manapi::json::operator/(DECIMAL num) const {
    auto n = *this;
    n /= num;
    return std::move(n);
}

manapi::json & manapi::json::operator/=(INTEGER num) {
    if (this->is_integer())
        this->as_integer() /= static_cast<INTEGER>(num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() /= num;
#endif
    else if (this->is_decimal())
        this->as_decimal() /= num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

manapi::json & manapi::json::operator/=(DECIMAL num) {
    if (this->is_integer())
        this->as_integer() /= static_cast<INTEGER>(num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    else if (this->is_bigint())
        this->as_bigint() /= num;
#endif
    else if (this->is_decimal())
        this->as_decimal() /= num;
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json &manapi::json::operator*=(const BIGINT &num) {
    if (this->is_integer())
        this->as_integer() *= num.integerify();
    else if (this->is_bigint())
        this->as_bigint() *= num;
    else if (this->is_decimal())
        this->as_decimal() *= num.decimalify();
    else
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    return *this;
}
#endif

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const OBJECT::value_type &v) {
    return this->insert ({v.first, v.second});
}

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(OBJECT::value_type &&v) {
    if (this->m_type != type_object)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    return as_object().insert(std::forward<decltype(v)>(v));
}

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const STRING &key, manapi::json&& obj) {
    return this->insert ({key, std::forward<decltype(obj)>(obj)});
}

std::pair<manapi::json::OBJECT::iterator, bool> manapi::json::insert(const STRING &key, const manapi::json& obj) {
    return this->insert ({key, (obj)});
}

void manapi::json::push_back(manapi::json::ARRAY::const_iterator begin, manapi::json::ARRAY::const_iterator end) {
    if (this->m_type != type_array) { THROW_MANAPIHTTP_JSON_MISSING_FUNCTION; }
    for (auto it = begin; it != end; ++it)
        push_back(*it);
}

void manapi::json::push_back(manapi::json obj) {
    this->as_array().push_back(std::move(obj));
}

void manapi::json::pop_back() {
    if (this->m_type != type_array)
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;

    as_array().pop_back();
}

int manapi::json::data_type() const MANAPIHTTP_NOEXCEPT {
    return this->m_type;
}

manapi::json manapi::json::object() {
    json obj;

    ::json_set_object_(obj.m_type, obj.m_data);

    return std::move(obj);
}

manapi::json manapi::json::array() {
    // rvalue, arr not be destroyed
    json arr;

    ::json_set_array_(arr.m_type, arr.m_data);

    return std::move(arr);
}

manapi::json manapi::json::object(const std::initializer_list<json> &data) {
    auto obj = json::object();

    if (data.size() == 1 && data.begin()->is_object()) {
        obj = *data.begin();
    }
    else {
        for (const auto & it : data) {
            if (it.m_type == manapi::json::type_pair) {
                auto &key = static_cast <PAIR *> (it.m_data.src)->first.as_string();
                obj.insert({key, std::move(static_cast <PAIR *> (it.m_data.src)->second)});
            }
            else {
                throw manapi::json_parse_exception(ERR_JSON_UNSUPPORTED_TYPE,
                    std::format("a pair and an object are supported for processing in json::object(...), but got {}({}) type",
                        json_type_to_str(it.m_type), static_cast<int>(it.m_type)));
            }
        }
    }

    return std::move(obj);
}

manapi::json manapi::json::array(manapi::json data) {
    if (data.m_type == type_pair)
        return json::array({std::move(data.first()), std::move(data.second())});
    return json::array({std::move(data)});
}

manapi::status_or<manapi::json> manapi::json::parse(STRING_VIEW data, uint32_t flags) {
    manapi::json_mask m(nullptr);
    manapi::json_builder builder (m);
    builder.flags(flags);

    auto res = builder.parse(data);
    if (!res.ok())
        return manapi::status{std::move(res)};
    auto rhs = builder.get();
    if (!rhs.ok())
        return manapi::status{rhs.err()};
    return rhs.unwrap();
}

manapi::status_or<manapi::json> manapi::json::parse(const manapi::slice_view &data, uint32_t flags) {
    manapi::json_mask m(nullptr);
    manapi::json_builder builder (m);
    builder.flags(flags);
    for (const auto b : data) {
        auto res = builder.parse(b);
        if (!res.ok())
            return manapi::status{std::move(res)};
    }

    auto rhs = builder.get();
    if (!rhs.ok())
        return manapi::status{rhs.err()};

    return rhs.unwrap();
}

std::string manapi::json::stringify(const json &n, uint32_t spaces) {
    return std::move(n.dump(spaces));
}

manapi::json manapi::json::array(const std::initializer_list<json> &data) {
    auto arr = json::array();
    for (const auto & it : data) {
        if (it.m_type == type_pair) {
            json element (json::array());
            try {
                element.push_back(std::move(static_cast <PAIR *> (it.m_data.src)->first));
                element.push_back(std::move(static_cast <PAIR *> (it.m_data.src)->second));

                arr.push_back(std::move(element));
            }
            catch (json_parse_exception &e) {
                throw json_parse_exception(std::move(e));
            }
        }
        else {
            try {
                arr.push_back(it);
            }
            catch (json_parse_exception &e) {
                throw json_parse_exception(std::move(e));
            }
        }
    }
    return std::move(arr);
}

// std::map<std::string, manapi::json>::iterator manapi::json::find(const STRING &key) {
//     return this->as_object().find(key);
// }

manapi::json::OBJECT::iterator manapi::json::find(STRING_VIEW key) {
    return this->as_object().find(key);
}

void manapi::json::source(manapi::json_source *source) {
    ::json_set_source_(source, this->m_type, this->m_data);
}

manapi::json::OBJECT::const_iterator manapi::json::find(STRING_VIEW key) const {
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
    return ::json_as_pair_(this->m_type, this->m_data).first;
}

manapi::json & manapi::json::second() {
    return ::json_as_pair_(this->m_type, this->m_data).second;
}

const manapi::json & manapi::json::first() const {
    return ::json_as_pair_(this->m_type, this->m_data).first;
}

const manapi::json & manapi::json::second() const {
    return ::json_as_pair_(this->m_type, this->m_data).second;
}

bool manapi::json::contains(const std::string &key) const {
    return this->as_object().contains(key);
}

manapi::json::ARRAY::iterator manapi::json::erase(ARRAY::iterator it) {
    return this->as_array().erase(it);
}

manapi::json::OBJECT::iterator manapi::json::erase(OBJECT::iterator it) {
    return this->as_object().erase(it);
}

manapi::json::ARRAY::const_iterator manapi::json::erase(ARRAY::const_iterator it) {
    return this->as_array().erase(it);
}

manapi::json::OBJECT::const_iterator manapi::json::erase(OBJECT::const_iterator it) {
    return this->as_object().erase(it);
}

void manapi::json::erase(const std::string &key) {
    this->as_object().erase(key);
}

bool manapi::json::is_object() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_object;
}

bool manapi::json::is_array() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_array;
}

bool manapi::json::is_string() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_string;
}

bool manapi::json::is_slice() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_slice;
}

bool manapi::json::is_integer() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_integer;
}

bool manapi::json::is_null() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_null;
}

bool manapi::json::is_decimal() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_decimal;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
bool manapi::json::is_bigint() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_bigint;
}
#endif

bool manapi::json::is_bool() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_boolean;
}

bool manapi::json::is_pair() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_pair;
}

bool manapi::json::is_source() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == type_source;
}

manapi::json_source * manapi::json::release_source() {
    auto const z = this->m_data.source_src_;
    this->m_type = type_null;
    this->m_data.src = nullptr;
    return z;
}

const manapi::json_source & manapi::json::as_source() const {
    return ::json_as_source_(this->m_type, this->m_data);
}

manapi::json_source & manapi::json::as_source() {
    return ::json_as_source_(this->m_type, this->m_data);
}

const manapi::json::OBJECT & manapi::json::as_object() const {
    return ::json_as_object_(this->m_type, this->m_data);
}

manapi::json::OBJECT & manapi::json::as_object() {
    return ::json_as_object_(this->m_type, this->m_data);
}


const manapi::json::ARRAY & manapi::json::as_array() const {
    return ::json_as_array_(this->m_type, this->m_data);
}

manapi::json::ARRAY & manapi::json::as_array() {
    return ::json_as_array_(this->m_type, this->m_data);
}

const manapi::json::STRING & manapi::json::as_string() const {
    return ::json_as_string_(this->m_type, this->m_data);
}

manapi::json::STRING & manapi::json::as_string() {
    return ::json_as_string_(this->m_type, this->m_data);
}

const manapi::json::INTEGER & manapi::json::as_integer() const {
    return ::json_as_integer_(this->m_type, this->m_data);
}

manapi::json::INTEGER & manapi::json::as_integer() {
    return ::json_as_integer_(this->m_type, this->m_data);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
const manapi::json::BIGINT & manapi::json::as_bigint() const {
    return ::json_as_bigint_(this->m_type, this->m_data);
}
#endif

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json::BIGINT & manapi::json::as_bigint() {
    return ::json_as_bigint_(this->m_type, this->m_data);
}
#endif

const manapi::json::BOOLEAN & manapi::json::as_bool() const {
    return ::json_as_bool_(this->m_type, this->m_data);
}

manapi::json::BOOLEAN & manapi::json::as_bool() {
    return ::json_as_bool_(this->m_type, this->m_data);
}

const manapi::slice & manapi::json::as_slice() const {
    return ::json_as_slice_(this->m_type, this->m_data);
}

manapi::slice & manapi::json::as_slice() {
    return ::json_as_slice_(this->m_type, this->m_data);
}

const manapi::json::DECIMAL & manapi::json::as_decimal() const {
    return ::json_as_decimal_(this->m_type, this->m_data);
}

manapi::json::DECIMAL & manapi::json::as_decimal() {
    return ::json_as_decimal_(this->m_type, this->m_data);
}

manapi::json::NULLPTR manapi::json::as_null() const {
    if (this->m_type == type_null) {
        return nullptr;
    }
    THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
}

manapi::json& manapi::json::cast_string() {
    if (this->m_type == type_string) {
    }
    else {
        *this = ::json_as_string_cast(*this);
    }

    return *this;
}

manapi::json& manapi::json::cast_integer() {
    if (this->m_type == type_integer) {
    }
    else {
        *this = ::json_as_integer_cast(*this);
    }
    return *this;
}

manapi::json& manapi::json::cast_null() {
    if (this->m_type == type_null) {

    }
    else {
        *this = ::json_as_null_cast(*this);
    }
    return *this;
}

manapi::json& manapi::json::cast_decimal() {
    if (this->m_type == type_decimal) {

    }
    else {
        *this = ::json_as_decimal_cast(*this);
    }
    return *this;
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
manapi::json& manapi::json::cast_bigint() {
    if (this->m_type == type_bigint) {

    }
    else {
        *this = ::json_as_bigint_cast(*this);
    }
    return *this;
}
#endif

manapi::json & manapi::json::cast_bool() {
    if (this->m_type == type_boolean) {

    }
    else {
        *this = ::json_as_bool_cast(*this);
    }
    return *this;
}

manapi::json & manapi::json::cast_slice() {
    if (this->m_type == type_slice) {
    }
    else {
        *this = ::json_as_slice_cast(*this);
    }

    return *this;
}

void * manapi::json::as_ptr() const {
    return this->m_data.src;
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

manapi::json manapi::json::operator+(INTEGER num) const {
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
    if (n.is_string()) {
        n.as_string() += str;
    }
    else if (n.is_slice()) {
        n.as_slice().push_back(str.data(), str.size());
    }
    else {
        THROW_MANAPIHTTP_JSON_MISSING_FUNCTION;
    }
    return std::move(n);
}

manapi::json manapi::json::operator+(const char *str) const {
    auto n = *this;
    if (n.is_string()) {
        n.as_string() += str;
    }
    else if (n.is_slice()) {
        n.as_slice().push_back(str, std::strlen(str));
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
    if (this->m_type != x.m_type)
        return false;

    switch (this->m_type)
    {
        case type_integer:
            return as_integer() == x.as_integer();
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        case type_bigint:
            return as_bigint() == x.as_bigint();
#endif
        case type_string: {
            if (x.is_slice())
                return x.as_slice().cmp(as_string().data(), as_string().size()) == 0;
            return as_string() == x.as_string();
        }
        case type_boolean:
            return as_bool() == x.as_bool();
        case type_decimal:
            return as_decimal() == x.as_decimal();
        case type_null:
            return true;
        case type_slice: {
            if (x.is_string())
                return as_slice().cmp(x.as_string().data(), x.as_string().size()) == 0;
            return as_slice().cmp(x.as_slice()) == 0;
        }
        case type_object: {
            if (size() != x.size())
                return false;

            auto &e = this->entries();
            auto it1 = e.begin();
            auto it2 = x.entries().begin();

            while (it1 != e.end()) {
                if (it1->second != it2->second)
                    return false;
                ++it1;
                ++it2;
            }

            return true;
        }
        case type_array:
            if (size() != x.size())
                return false;

            for (size_t i = 0; i < size(); i++) {
                if (at(i) != x.at(i))
                    return false;
            }
            return true;
        default:
            return false;
    }

    return false;
}

bool manapi::json::operator==(BOOLEAN n) const {
    return ::json_as_bool_cast(*this) == n;
}

bool manapi::json::operator==(const char *n) const {
    if (this->is_slice())
        return this->as_slice().cmp(n, ::strlen(n));
    return this->as_string() == n;
}

bool manapi::json::operator==(const STRING_VIEW &n) const {
    if (this->is_slice())
        return this->as_slice().cmp(n.data(), n.size());
    return std::string_view(this->as_string()) == n;
}

bool manapi::json::operator==(const STRING &n) const {
    if (this->is_slice())
        return this->as_slice().cmp(n.data(), n.size()) == 0;
    return this->as_string() == n;
}

bool manapi::json::operator==(const manapi::slice_base &n) const {
    if (this->is_slice())
        return this->as_slice().cmp(n) == 0;
    auto &s = this->as_string();
    return this->as_slice().cmp(s.data(), s.size()) == 0;
}

bool manapi::json::operator==(INTEGER n) const {
    return ::json_as_integer_cast(*this) == n;
}

bool manapi::json::operator==(DECIMAL n) const {
    return ::json_as_decimal_cast(*this) == n;
}

bool manapi::json::operator>(INTEGER n) const {
    return ::json_as_integer_cast(*this) > n;
}

bool manapi::json::operator>(DECIMAL n) const {
    return ::json_as_decimal_cast(*this) > n;
}

bool manapi::json::operator>=(INTEGER n) const {
    return ::json_as_integer_cast(*this) >= n;
}

bool manapi::json::operator>=(DECIMAL n) const {
    return ::json_as_decimal_cast(*this) >= n;
}

bool manapi::json::operator<(INTEGER n) const {
    return ::json_as_integer_cast(*this) < n;
}

bool manapi::json::operator<(DECIMAL n) const {
    return ::json_as_decimal_cast(*this) < n;
}

bool manapi::json::operator<=(INTEGER n) const {
    return ::json_as_integer_cast(*this) <= n;
}

bool manapi::json::operator<=(DECIMAL n) const {
    return ::json_as_decimal_cast(*this) <= n;
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

const char *manapi::json_parse_exception::what() const MANAPIHTTP_NOEXCEPT {
    return message.data();
}

#ifdef _MSC_VER
#   pragma warning(pop)
#endif