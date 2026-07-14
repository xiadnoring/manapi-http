#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>
#include <stack>
#include <deque>
#include <set>

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiBigint.hpp"
#include "../ManapiInt.hpp"

namespace manapi {
    class slice;

    class slice_view;

    class slice_base;

    class json_source;

    /**
     * provides json error nums
     */
    enum json_err_num {
        ERR_JSON_INVALID_CHAR = 0,
        ERR_JSON_INVALID_STRING,
        ERR_JSON_NO_SUCH_KEY,
        ERR_JSON_OUT_OF_RANGE,
        ERR_JSON_DUPLICATE_KEY,
        ERR_JSON_UNSUPPORTED_TYPE,
        ERR_JSON_BUG,
        ERR_JSON_UNEXPECTED_END,
        ERR_JSON_MASK_VERIFY_FAILED,
        ERR_JSON_BAD_ESCAPED_CHAR
    };

    class json {
    public:
        typedef std::map <std::string, manapi::json, std::less<>> OBJECT;

        typedef std::vector <manapi::json> ARRAY;

        typedef double long DECIMAL;

        typedef int64_t INTEGER;

        typedef std::nullptr_t NULLPTR;

        typedef std::string STRING;

        typedef std::string_view STRING_VIEW;

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        typedef bigint BIGINT;
#endif
        typedef bool BOOLEAN;

        typedef std::pair <json, json> PAIR;

        union data_t {
            void    *src;
            BOOLEAN *bool_src_;
            ARRAY   *array_src_;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            BIGINT  *bigint_src_;
#endif
            OBJECT  *object_src_;
            STRING  *string_src_;
            INTEGER *integer_src_;
            DECIMAL *decimal_src_;
            PAIR    *pair_src_;
            manapi::slice *slice_src_;
            manapi::json_source *source_src_;
        };

        enum types {
            /* null type */
            type_null = 0,
            /* integer, decimal, bigint types */
            type_number = 1,
            /* string type */
            type_string = 2,
            /* decimal type */
            type_decimal = 3,
            /* boolean type */
            type_boolean = 4,
            /* std::map type */
            type_object = 5,
            /* std::vector type */
            type_array = 6,
            /* ssize_t type */
            type_integer = 7,
            /* bigint type */
            type_bigint = 8,
            /* pair<string, string> type */
            type_pair = 9,
            /* external source */
            type_source = 10,
            /* slice */
            type_slice = 11,
            /* max type code reserved by manapihttp */
            type_max = type_source
        };

        /**
         * Generate a JSON object
         * @return created JSON object
         */
        static json object ();

        /**
         * Generate a JSON array
         * @return created JSON array
         */
        static json array ();

        /**
         * Generate a JSON array from the provided initializer list
         * @param data the provided initializer list
         * @return create JSON array
         */
        static json array (const std::initializer_list<json> &data);

        /**
         * Generate a JSON object from the provided initializer list
         * @param data the provided initializer list
         * @return created JSON object
         */
        static json object (const std::initializer_list<json> &data);

        /**
         * Generate a JSON array using an other JSON
         *
         * If the provided JSON is a pair, it returns JSON array with 2 items from the pair,
         * but otherwise, it returns JSON array with 1 items
         *
         * @param data provided JSON
         * @return created JSON array
         */
        static json array (manapi::json data);

        /**
         * Parse JSON from string
         *
         * @param data source string
         * @return a json object on success, otherwise it returns ParseError, InternalError, ResourceExhausted
         */
        static manapi::status_or<json> parse (STRING_VIEW data, uint32_t flags = 0);

        /**
         * Parse JSON from slice
         *
         * @param data source slice
         * @return a json object on success, otherwise it returns ParseError, InternalError, ResourceExhausted
         */
        static manapi::status_or<json> parse (const manapi::slice_view &data, uint32_t flags = 0);

        /**
         * Stringify JSON as a string
         * @param n source JSON
         * @param spaces count of spaces
         * @return stringified JSON
         */
        static std::string stringify (const json &n, uint32_t spaces = 2);

        /**
         * initialize JSON as a null
         */
        json();

        /**
         * initialize JSON using an other JSON
         * @param other JSON
         */
        json(const json &other);

        /**
         * make it movable
         * @param other JSON
         */
        json(json &&other) MANAPIHTTP_NOEXCEPT;

        /**
         * initialize JSON using an initializer list
         * @param data initializer list
         */
        json(const std::initializer_list<json> &data);

        /**
         * initialize JSON from a source string
         * @param str source string
         */
        json(STRING_VIEW str);

        /**
         * initialize JSON from an integer
         * @param num source integer
         */
        json(INTEGER num);

        /**
         * initialize JSON from a source array of chars
         * @param plain_text array of chars
         */
        json(const char *plain_text);

        /**
         * initialize JSON from a source string
         * @param str source string
         */
        json(STRING str);

        /**
         * initialize JSON from a source slice
         * @param sv source slice
         */
        json(manapi::slice&& sv);

        /**
         * initialize JSON from a source slice
         * @param sv source slice
         */
        json(const slice_base &sv);

        /**
         * initialize JSON from a decimal
         * @param num source decimal
         */
        json(DECIMAL num);

        /**
         * initialize JSON as a nullptr
         * @param n nullptr
         */
        json(const NULLPTR &n);

        /**
         * initialize JSON as a boolean
         * @param value source boolean
         */
        json(BOOLEAN value);

        /**
         * initialize JSON as an object
         * @param obj source object
         */
        json(OBJECT obj);

        /**
         * initialize JSON as an array
         * @param arr source array
         */
        json(ARRAY arr);

        /**
         * initialize JSON as an object
         * @tparam T type of value in the mapped object
         * @param n source mapped object
         */
        template<typename T, typename S>
        json (std::map<std::string, T, S> n) : json(json::object()) {
            while (!n.empty()) {
                auto e = n.extract(n.begin());
                this->insert({std::move(e.key()), std::move(e.mapped())});
            }
        }

        /**
         * initialize JSON as an object
         * @tparam T type of value in the unordered mapped object
         * @param n source mapped object
         */
        template<typename T>
        json (std::unordered_map<std::string, T> n) : json(json::object()) {
            while (!n.empty()) {
                auto e = n.extract(n.begin());
                this->insert({std::move(e.key()), std::move(e.mapped())});
            }
        }

        /**
         * initialize JSON as an integer
         * @tparam T type of the integer
         * @param n source integer
         */
        template<typename T>
        requires(std::is_integral_v<T>)
        json(const T &n) : json(static_cast<INTEGER>(n)) {}

        /**
         * initialize JSON as a decimal
         * @tparam T type of the decimal
         * @param n source decimal
         */
        template<typename T>
        requires(std::is_floating_point_v<T>)
        json (const T &n) : json(static_cast<DECIMAL>(n)) {}

        /**
         * initialize JSON as an array
         * @tparam V type of values in the array
         * @param array source array
         */
        template<typename V>
        json(std::vector<V> array) : json(manapi::json::array()) {
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        /**
         * initialize JSON as an array
         * @tparam V type of values in the array
         * @param array source array
         */
        template<typename V>
        json(std::deque<V> array) : json(manapi::json::array()) {
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        /**
         * initialize JSON as an array
         * @tparam V type of values in the array
         * @param array source array
         */
        template<typename V>
        json(std::stack<V> array) : json(manapi::json::array()) {
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        /**
         * initialize JSON as an array
         * @tparam V type of values in the set
         * @param array source set
         */
        template<typename V>
        json(std::set<V> array) : json(manapi::json::array()) {
            while (!array.empty()) { this->push_back(std::move(array.extract(array.begin()).value())); }
        }

        /**
         * initialize JSON as an array
         * @tparam V type of values in the array
         * @tparam N size of the array
         * @param array source array
         */
        template<typename V, std::size_t N>
        json(std::array<V, N> array) : json(manapi::json::array()) {
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        /* deconstructor */
        ~json();

        /**
         * contains()
         * @param key string
         * @return true if the key exists in the JSON
         */
        MANAPIHTTP_NODISCARD bool contains (const std::string &key) const;

        /**
         * get a value by the key
         * @param key key
         * @return value if it exists, otherwise it throws an exception
         */
        const json &operator[] (const STRING &key) const;

        /**
         * get a value by the index
         * @param index index
         * @return value if it exists, otherwise it throws an exception
         */
        const json &operator[] (size_t index) const;

        /**
         * get a value by the key
         * @param key key
         * @return value if it exists, otherwise it throws an exception
         */
        json &operator[] (const STRING &key);

        /**
         * get a value by the index
         * @param index index
         * @return value if it exists, otherwise it throws an exception
         */
        json &operator[] (size_t index);

        /**
         * get a value by the key
         * @param key key
         * @return value if it exists, otherwise it throws an exception
         */
        MANAPIHTTP_NODISCARD const json &at (const STRING &key) const;

        /**
         * get a value by the index
         * @param index index
         * @return value if it exists, otherwise it throws an exception
         */
        MANAPIHTTP_NODISCARD const json &at (size_t index) const;

        /**
         * get a value by the key
         * @param key key
         * @return value if it exists, otherwise it throws an exception
         */
        json &at (const std::string &key);

        /**
         * get a value by the index
         * @param index index
         * @return value if it exists, otherwise it throws an exception
         */
        json &at (size_t index);

        // TRASH (no with const json &obj)
        json &operator= (STRING str);
        json &operator= (const char *str);
        json &operator= (BOOLEAN b);
        json &operator= (INTEGER num);
        json &operator= (DECIMAL num);
        json &operator= (const NULLPTR &n);
        json &operator= (const json &obj);
        json &operator= (json &&obj) MANAPIHTTP_NOEXCEPT;
        json &operator= (const std::initializer_list <json> &data);
        json &operator= (manapi::slice &&sv);
        json &operator= (const manapi::slice_base &sv);

        template<typename T>
        requires(std::is_integral_v<T>)
        json &operator= (const T &n) {
            this->operator=(static_cast<INTEGER>(n));
            return *this;
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json &operator= (const T &n) {
            this->operator=(static_cast<DECIMAL>(n));
            return *this;
        }

        json operator* (INTEGER num) const;
        json operator* (DECIMAL num) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        json operator* (const T &n) const {
            return this->operator*(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator* (const T &n) const {
            return this->operator* (static_cast<DECIMAL>(n));
        }

        json &operator*= (INTEGER num);
        json &operator*= (DECIMAL num);

        template<typename T>
        requires(std::is_integral_v<T>)
        json &operator*= (const T &n) {
            return this->operator*=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json &operator*= (const T &n) {
            return this->operator*=(static_cast<DECIMAL>(n));
        }

        json operator/ (INTEGER num) const;
        json operator/ (DECIMAL num) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        json operator/ (const T &n) const {
            return this->operator/ (static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator/ (const T &n) const {
            return this->operator/ (static_cast<DECIMAL>(n));
        }

        json &operator/= (INTEGER num);
        json &operator/= (DECIMAL num);

        template<typename T>
        requires(std::is_integral_v<T>)
        json &operator/= (const T &n) {
            return this->operator/=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json &operator/= (const T &n) {
            return this->operator/= (static_cast<DECIMAL>(n));
        }

        json operator- (INTEGER num) const;
        json operator- (DECIMAL num) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        json operator- (const T &n) const {
            return this->operator-(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator- (const T &n) const {
            return this->operator-(static_cast<DECIMAL>(n));
        }

        json operator+ (INTEGER num) const;
        json operator+ (DECIMAL num) const;
        json operator+ (const STRING &str) const;
        json operator+ (const char *str) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        json operator+ (const T &n) const {
            return this->operator+(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator+ (const T &n) const {
            return this->operator+(static_cast<DECIMAL>(n));
        }

        json & operator-= (INTEGER num);
        json & operator-= (int num);
        json & operator-= (DECIMAL num);

        template<typename T>
        requires(std::is_integral_v<T>)
        json &operator-= (const T &n) {
            return this->operator-=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json &operator-= (const T &n) {
            return this->operator-=(static_cast<DECIMAL>(n));
        }

        json &operator+= (const STRING &str);
        json &operator+= (const char *str);
        json &operator+= (INTEGER num);
        json &operator+= (DECIMAL num);

        template<typename T>
        requires(std::is_integral_v<T>)
        json &operator+= (const T &n) {
            return this->operator+=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json &operator+= (const T &n) {
            return this->operator+=(static_cast<DECIMAL>(n));
        }

        bool operator== (const json &n) const;
        bool operator== (BOOLEAN n) const;
        bool operator== (const char *n) const;
        bool operator== (const STRING_VIEW &n) const;
        bool operator== (const STRING &n) const;
        bool operator== (const manapi::slice_base &n) const;
        bool operator== (INTEGER n) const;
        bool operator== (DECIMAL n) const;
        bool operator> (INTEGER n) const;
        bool operator> (DECIMAL n) const;
        bool operator< (INTEGER n) const;
        bool operator< (DECIMAL n) const;
        bool operator>= (INTEGER n) const;
        bool operator>= (DECIMAL n) const;
        bool operator<= (INTEGER n) const;
        bool operator<= (DECIMAL n) const;
        bool operator== (const NULLPTR &n) const;

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        bool operator!= (const BIGINT &n) const;
        bool operator== (const BIGINT &n) const;
        bool operator< (const BIGINT & n) const;
        bool operator> (const BIGINT & n) const;
        bool operator<= (const BIGINT & n) const;
        bool operator>= (const BIGINT & n) const;
#endif

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator==(const T &n) const {
            return this->operator==(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator==(const T &n) const {
            return this->operator==(static_cast<DECIMAL>(n));
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator!=(const T &n) const {
            return !this->operator==(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator!=(const T &n) {
            return !this->operator==(static_cast<DECIMAL>(n));
        }

        bool operator!=(const STRING &str) const {
            return !this->operator==(str);
        }

        bool operator!=(const char * &str) const {
            return !this->operator==(str);
        }

        bool operator!=(const NULLPTR &n) const {
            return !this->operator==(n);
        }

        bool operator!=(BOOLEAN n) const {
            return !this->operator==(n);
        }

        bool operator!=(const STRING_VIEW &n) const {
            return !this->operator==(n);
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator>(const T &n) const {
            return this->operator>(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator>(const T &n) const {
            return this->operator>(static_cast<DECIMAL>(n));
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator<(const T &n) const {
            return this->operator<(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator<(const T &n) const {
            return this->operator<(static_cast<DECIMAL>(n));
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator>=(const T &n) const {
            return this->operator>=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator>=(const T &n) const {
            return this->operator>=(static_cast<DECIMAL>(n));
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        bool operator<=(const T &n) const {
            return this->operator<=(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bool operator<=(const T &n) const {
            return this->operator<=(static_cast<DECIMAL>(n));
        }

        template<typename T>
        bool equals(const T &n) const {
            return this->operator==(n);
        }

        std::pair<OBJECT::iterator, bool> insert (const STRING &key, json &&obj);

        std::pair<OBJECT::iterator, bool> insert (const STRING &key, const json &obj);

        std::pair<OBJECT::iterator, bool> insert (const OBJECT::value_type &v);

        std::pair<OBJECT::iterator, bool> insert (OBJECT::value_type &&v);

        void erase (const STRING &key);

        ARRAY::iterator erase (ARRAY::iterator it);

        OBJECT::iterator erase (OBJECT::iterator it);

        ARRAY::const_iterator erase (ARRAY::const_iterator it);

        OBJECT::const_iterator erase (OBJECT::const_iterator it);

        void push_back (json obj);

        void push_back (ARRAY::const_iterator begin, ARRAY::const_iterator end);

        void pop_back ();

        MANAPIHTTP_NODISCARD int data_type () const MANAPIHTTP_NOEXCEPT;

        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        auto begin () const
        { return this->as_object().begin(); }

        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        auto end () const
        { return this->as_object().end(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        auto begin () const
        { return this->as_array().begin(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        auto end () const
        { return this->as_array().end(); }


        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        auto begin ()
        { return this->as_object().begin(); }

        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        auto end ()
        { return this->as_object().end(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        auto begin ()
        { return this->as_array().begin(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        auto end ()
        { return this->as_array().end(); }

        OBJECT::iterator find (STRING_VIEW key);

        void source (manapi::json_source *source);

        MANAPIHTTP_NODISCARD OBJECT::const_iterator find (STRING_VIEW key) const;

        MANAPIHTTP_NODISCARD const ARRAY &each() const;

        MANAPIHTTP_NODISCARD const OBJECT &entries() const;

        MANAPIHTTP_NODISCARD ARRAY &each();

        MANAPIHTTP_NODISCARD OBJECT &entries();

        manapi::json &first ();

        manapi::json &second ();

        MANAPIHTTP_NODISCARD const manapi::json &first () const;

        MANAPIHTTP_NODISCARD const manapi::json &second () const;

        /**
         * check the JSON type
         * @return true if it's an object
         */
        MANAPIHTTP_NODISCARD bool is_object () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's an array
         */
        MANAPIHTTP_NODISCARD bool is_array () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a string
         */
        MANAPIHTTP_NODISCARD bool is_string () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a slice
         */
        MANAPIHTTP_NODISCARD bool is_slice () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a integer
         */
        MANAPIHTTP_NODISCARD bool is_integer () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a null
         */
        MANAPIHTTP_NODISCARD bool is_null () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a decimal
         */
        MANAPIHTTP_NODISCARD bool is_decimal () const MANAPIHTTP_NOEXCEPT;
#ifdef MANAPIHTTP_BIGINT_SUPPORT

        /**
         * check the JSON type
         * @return true if it's a integer
         */
        MANAPIHTTP_NODISCARD bool is_bigint () const MANAPIHTTP_NOEXCEPT;
#endif

        /**
         * check the JSON type
         * @return true if it's a bool
         */
        MANAPIHTTP_NODISCARD bool is_bool () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a pair
         */
        MANAPIHTTP_NODISCARD bool is_pair () const MANAPIHTTP_NOEXCEPT;

        /**
         * check the JSON type
         * @return true if it's a source
         */
        MANAPIHTTP_NODISCARD bool is_source () const MANAPIHTTP_NOEXCEPT;

        json_source *release_source ();

        /**
         * strict source retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const json_source &as_source () const;

        /**
         * strict source retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD json_source &as_source ();

        /**
         * strict object retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const OBJECT &as_object () const;

        /**
         * strict object retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD OBJECT &as_object ();

        /**
         * strict array retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const ARRAY &as_array () const;

        /**
         * strict array retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD ARRAY &as_array ();

        /**
         * strict string retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const STRING &as_string () const;

        /**
         * strict string retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD STRING &as_string ();

        /**
         * strict integer retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const INTEGER &as_integer () const;

        /**
         * strict integer retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD INTEGER &as_integer ();

        /**
         * strict null retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD NULLPTR as_null () const;

        /**
         * strict decimal retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const DECIMAL &as_decimal () const;

        /**
         * strict decimal retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD DECIMAL &as_decimal ();

        /**
         * strict boolean retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const BOOLEAN &as_bool () const;

        /**
         * strict boolean retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD BOOLEAN &as_bool ();

        /**
         * strict boolean retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const manapi::slice &as_slice () const;

        /**
         * strict boolean retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD manapi::slice &as_slice ();

        /**
         * string retrieval
         *
         * string - string
         * integer - string
         * bigint - string
         * decimal - string
         * slice - string
         * @return
         */
        manapi::json &cast_string ();

        /**
         * integer retrieval
         *
         * @note integer - integer
         * @note decimal - integer
         * @note bigint - integer
         * @return
         */
        manapi::json &cast_integer ();

        /**
         * null retrieval
         *
         * @note null - null
         * @return
         */
        manapi::json &cast_null ();

        /**
         * deciaml retrieval
         *
         * @note decimal - decimal
         * @note bigint - decimal
         * @note integer - decimal
         * @return
         */
        manapi::json &cast_decimal ();

        /**
         * boolean retrieval
         *
         * @note boolean - boolean
         * @return
         */
        manapi::json &cast_bool ();

        /**
         * slice retrieval
         *
         * string - slice
         * integer - slice
         * bigint - slice
         * decimal - slice
         * string - slice
         * @return
         */
        manapi::json &cast_slice ();

        /**
         * get a pointer
         * @return pointer to data
         */
        MANAPIHTTP_NODISCARD void *as_ptr () const;

        /**
         * get a JSON dump string
         * @param spaces additional spaces
         * @param shift left alignment
         * @return JSON string
         */
        MANAPIHTTP_NODISCARD std::string dump (uint32_t spaces = 0, uint32_t shift = 0) const;

        /**
         * get a JSON dump slices
         * @param sv output
         * @param spaces additional spaces
         * @param shift left alignment
         * @return JSON string
         */
        void slice (manapi::slice *sv, uint32_t spaces = 0, uint32_t shift = 0) const;

        MANAPIHTTP_NODISCARD size_t size () const;

        MANAPIHTTP_NODISCARD bool empty () const;

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        /**
         * bigint retrieval
         *
         * @note bigint - bigint
         * @note integer - bigint
         * @note decimal - bigint
         * @note string - bigint
         * @return
         */
        manapi::json &cast_bigint ();

        json &operator+= (const BIGINT &num);

        json(BIGINT num);

        json operator* (const BIGINT &num) const;

        json &operator*= (const BIGINT &num);

        json operator- (const BIGINT &num) const;

        json operator+ (const BIGINT &num) const;

        json &operator= (BIGINT num);

        json & operator-= (const BIGINT &num);

        /**
         * strict bigint retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD const BIGINT &as_bigint () const;

        /**
         * strict bigint retrieval
         * @return
         */
        MANAPIHTTP_NODISCARD BIGINT &as_bigint ();
#endif
    protected:
        int m_type;

        data_t m_data;
    };

    class json_parse_exception : public std::exception {
    public:
        /**
         * Generate Json Parse Exception Instance
         * @param errnum the json error number
         * @param msg the json error message
         */
        explicit json_parse_exception(const json_err_num &errnum, const std::string &msg);

        /**
         * Get the json error message
         * @return the json error message
         */
        MANAPIHTTP_NODISCARD const char *what () const MANAPIHTTP_NOEXCEPT override;

        /**
         * Get the json error number
         * @return the json error number
         */
        MANAPIHTTP_NODISCARD const json_err_num &err_num () const;
    private:
        std::string message;
        json_err_num errnum;
    };
}