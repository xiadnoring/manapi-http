#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>

#include "ManapiUtils.hpp"
#include "ManapiBigint.hpp"
#include "ManapiInt.hpp"

namespace manapi {
    enum json_err_num {
        ERR_JSON_INVALID_CHAR = 0,
        ERR_JSON_INVALID_STRING = 1,
        ERR_JSON_NO_SUCH_KEY = 2,
        ERR_JSON_OUT_OF_RANGE = 3,
        ERR_JSON_DUPLICATE_KEY = 4,
        ERR_JSON_UNSUPPORTED_TYPE = 5,
        ERR_JSON_BUG = 6,
        ERR_JSON_UNEXPECTED_END = 7,
        ERR_JSON_MASK_VERIFY_FAILED = 8,
        ERR_JSON_BAD_ESCAPED_CHAR = 9
    };

    class json_parse_exception : public std::exception {
    public:
        explicit json_parse_exception(const json_err_num &errnum, const std::string &msg);
        // json_parse_exception (json_parse_exception &&n) noexcept ;
        // json_parse_exception &operator=(json_parse_exception &&n) noexcept;
        [[nodiscard]] const char *what () const noexcept override;
        [[nodiscard]] const json_err_num &err_num () const;
    private:
        std::string message;
        json_err_num errnum;
    };

    class json {
    public:
        typedef std::map <std::string, manapi::json> OBJECT;
        typedef std::vector <manapi::json> ARRAY;
        typedef double long DECIMAL;
        typedef ssize_t INTEGER;
        typedef nullptr_t NULLPTR;
        typedef std::string STRING;
        typedef std::string_view STRING_VIEW;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        typedef bigint BIGINT;
#endif
        typedef bool BOOLEAN;
        typedef std::pair <json, json> PAIR;

        typedef std::u32string UNICODE_STRING;

        enum types {
            type_null = 0,
            type_number = 1,
            type_string = 2,
            type_decimal = 3,
            type_boolean = 4,
            type_object = 5,
            type_array = 6,
            type_integer = 7,
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            type_bigint = 8,
#endif
            type_pair = 9
        };

        static json object ();
        static json array ();

        static json array (const std::initializer_list<json> &data);
        static json object (const std::initializer_list<json> &data);

        static json parse (const std::string &data);
        static std::string stringify (const json &n, const int &spaces = 2);

        json();
        json(const json &other);
        json(json &&other) noexcept;
        json(const std::initializer_list<json> &data);

        // Do not use explicit

        json(STRING_VIEW str, const bool &parse = false);
        json(const UNICODE_STRING &str, const bool &parse = false);
        json(const INTEGER &num);
        json(const char *plain_text, const bool &parse = false);
        json(STRING str);
        json(const DECIMAL &num);
        json(const NULLPTR &n);
        json(const BOOLEAN &value);
        json(OBJECT obj);
        json(ARRAY arr);

        template<typename T>
        requires(std::is_integral_v<T>)
        json(const T &n) {
            this->_parse (static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json (const T &n) {
            this->_parse (static_cast<DECIMAL>(n));
        }

        template<typename V>
        json(std::map<std::string, V> object) {
            this->_set_object();
            for (auto &v: object) { this->operator[](v.first) = std::move(v.second); }
        }

        template<typename V>
        json(std::unordered_map<std::string, V> object) {
            this->_set_object();
            for (auto &v: object) { this->operator[](v.first) = std::move(v.second); }
        }

        template<typename V>
        json(std::vector<V> array) {
            this->_set_array();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::deque<V> array) {
            this->_set_array();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::stack<V> array) {
            this->_set_array();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::set<V> array) {
            this->_set_array();
            while (!array.empty()) { this->push_back(std::move(array.extract(array.begin()).value())); }
        }

        template<typename V, std::size_t N>
        json(std::array<V, N> array) {
            this->_set_array();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        ~json();

        [[nodiscard]] bool contains (const std::string &key) const;

        const json &operator[] (const STRING &key) const;
        const json &operator[] (const UNICODE_STRING &key) const;
        const json &operator[] (const size_t &index) const;

        json &operator[] (const STRING &key);
        json &operator[] (const UNICODE_STRING &key);
        json &operator[] (const size_t &index);

        [[nodiscard]] const json &at (const STRING &key) const;
        [[nodiscard]] const json &at (const UNICODE_STRING &key) const;
        [[nodiscard]] const json &at (const size_t &index) const;

        json &at (const std::string &key);
        json &at (const UNICODE_STRING &key);
        json &at (const size_t &index);

        // TRASH (no with const json &obj)
        json &operator= (const UNICODE_STRING &str);
        json &operator= (STRING str);
        json &operator= (const char *str);
        json &operator= (const BOOLEAN &b);
        json &operator= (const INTEGER &num);
        json &operator= (const DECIMAL &num);
        json &operator= (const NULLPTR &n);
        json &operator= (const json &obj);
        json &operator= (json &&obj) noexcept ;
        json &operator= (const std::initializer_list <json> &data);

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

        json operator* (const INTEGER &num) const;
        json operator* (const DECIMAL &num) const;

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

        json &operator*= (const INTEGER &num);
        json &operator*= (const DECIMAL &num);

        template<typename T>
        requires(std::is_integral_v<T>)
        json operator*= (const T &n) {
            return this->operator*(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator*= (const T &n) {
            return this->operator* (static_cast<DECIMAL>(n));
        }

        json operator- (const INTEGER &num) const;
        json operator- (const DECIMAL &num) const;

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

        json operator+ (const INTEGER &num) const;
        json operator+ (const DECIMAL &num) const;
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

        json & operator-= (const INTEGER &num);
        json & operator-= (const int &num);
        json & operator-= (const DECIMAL &num);

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
        json &operator+= (const INTEGER &num);
        json &operator+= (const DECIMAL &num);

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
        bool operator== (const BOOLEAN &n) const;
        bool operator== (const char *n) const;
        bool operator== (const STRING_VIEW &n) const;
        bool operator== (const STRING &n) const;
        bool operator== (const INTEGER &n) const;
        bool operator== (const DECIMAL &n) const;
        bool operator== (const NULLPTR &n) const;

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

        bool operator!=(const BOOLEAN &n) const {
            return !this->operator==(n);
        }

        bool operator!=(const STRING_VIEW &n) const {
            return !this->operator==(n);
        }

        void insert (const STRING &key, json obj);
        void insert (const UNICODE_STRING &key, json obj);

        void erase (const STRING &key);
        void erase (const UNICODE_STRING &key);
        ARRAY::const_iterator erase (ARRAY::const_iterator it);
        OBJECT::const_iterator erase (OBJECT::const_iterator it);

        void push_back (json obj);
        void push_back (ARRAY::const_iterator begin, ARRAY::const_iterator end);
        void pop_back ();


        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        constexpr auto begin () const
        { return this->as_object().begin(); }

        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        constexpr auto end () const
        { return this->as_object().end(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        constexpr auto begin () const
        { return this->as_array().begin(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        constexpr auto end () const
        { return this->as_array().end(); }


        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        constexpr auto begin ()
        { return this->as_object().begin(); }

        template<class T>
        requires(std::is_same_v<T, OBJECT>)
        constexpr auto end ()
        { return this->as_object().end(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        constexpr auto begin ()
        { return this->as_array().begin(); }

        template<class T>
        requires(std::is_same_v<T, ARRAY>)
        constexpr auto end ()
        { return this->as_array().end(); }

        [[nodiscard]] const ARRAY &each() const;
        [[nodiscard]] const OBJECT &entries() const;
        [[nodiscard]] ARRAY &each();
        [[nodiscard]] OBJECT &entries();

        [[nodiscard]] bool contains       (const UNICODE_STRING &key) const;

        [[nodiscard]] bool is_object      () const;
        [[nodiscard]] bool is_array       () const;
        [[nodiscard]] bool is_string      () const;
        [[nodiscard]] bool is_integer     () const;
        [[nodiscard]] bool is_null        () const;
        [[nodiscard]] bool is_decimal     () const;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        [[nodiscard]] bool is_bigint      () const;
#endif
        [[nodiscard]] bool is_bool        () const;

        /**
         * strict object retrieval
         * @return
         */
        [[nodiscard]] const OBJECT &as_object () const;
        [[nodiscard]] OBJECT &as_object ();
        /**
         * strict array retrieval
         * @return
         */
        [[nodiscard]] const ARRAY &as_array () const;
        [[nodiscard]] ARRAY &as_array ();
        /**
         * strict string retrieval
         * @return
         */
        [[nodiscard]] const STRING &as_string () const;
        [[nodiscard]] STRING &as_string ();
        /**
         * strict integer retrieval
         * @return
         */
        [[nodiscard]] const INTEGER &as_integer () const;
        [[nodiscard]] INTEGER &as_integer ();
        /**
         * strict null retrieval
         * @return
         */
        [[nodiscard]] NULLPTR as_null () const;
        /**
         * strict decimal retrieval
         * @return
         */
        [[nodiscard]] const DECIMAL &as_decimal () const;
        [[nodiscard]] DECIMAL &as_decimal ();
        /**
         * strict boolean retrieval
         * @return
         */
        [[nodiscard]] const BOOLEAN &as_bool () const;
        [[nodiscard]] BOOLEAN &as_bool ();

        /**
         * non-strict object retrieval
         *
         * @note object - object
         * @return
         */
        [[nodiscard]] OBJECT as_object_cast () const;
        /**
         * non-strict array retrieval
         *
         * @note array - array
         * @return
         */
        [[nodiscard]] ARRAY as_array_cast () const;
        /**
         * non-strict string retrieval
         *
         * string - string
         * integer - string
         * bigint - string
         * decimal - string
         * @return
         */
        [[nodiscard]] STRING as_string_cast () const;
        /**
         * non-strict integer retrieval
         *
         * @note integer - integer
         * @note decimal - integer
         * @note bigint - integer
         * @return
         */
        [[nodiscard]] INTEGER as_integer_cast () const;
        /**
         * non-strict null retrieval
         *
         * @note null - null
         * @return
         */
        [[nodiscard]] NULLPTR as_null_cast () const;
        /**
         * non-strict deciaml retrieval
         *
         * @note decimal - decimal
         * @note bigint - decimal
         * @note integer - decimal
         * @return
         */
        [[nodiscard]] DECIMAL as_decimal_cast () const;
        /**
         * non-strict boolean retrieval
         *
         * @note boolean - boolean
         * @return
         */
        [[nodiscard]] BOOLEAN as_bool_cast () const;

        [[nodiscard]] std::string dump (const int &spaces = 0, const int &first_spaces = 0) const;

        [[nodiscard]] size_t size () const;
        [[nodiscard]] bool empty () const;

        static void error_invalid_char (const UNICODE_STRING &plain_text, const size_t &i);
        static void error_invalid_char (const STRING_VIEW &plain_text, const size_t &i);
        static void error_unexpected_end (const size_t &i);

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        /**
         * non-strict bigint retrieval
         *
         * @note bigint - bigint
         * @note integer - bigint
         * @note decimal - bigint
         * @note string - bigint
         * @return
         */
        [[nodiscard]] BIGINT as_bigint_cast () const;
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
        [[nodiscard]] const BIGINT &as_bigint () const;
        [[nodiscard]] BIGINT &as_bigint ();
#endif
    protected:
        bool root = true;
    private:
        [[nodiscard]] OBJECT &_as_object () const;
        [[nodiscard]] ARRAY &_as_array () const;
        [[nodiscard]] STRING &_as_string () const;
        [[nodiscard]] INTEGER &_as_integer () const;
        [[nodiscard]] DECIMAL &_as_decimal () const;
        [[nodiscard]] BOOLEAN &_as_bool () const;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        [[nodiscard]] BIGINT &_as_bigint () const;
#endif
        [[nodiscard]] PAIR &_as_pair () const;

        // string
        void _parse (const UNICODE_STRING &plain_text);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void _parse (const STRING_VIEW &plain_text, bool bigint = false, size_t bigint_precision = 128);
#else
        void _parse (const STRING_VIEW &plain_text);
#endif
        // integers
        void _parse (const size_t &num);
        void _parse (const INTEGER &num);
        void _parse (const int &num);
        void _parse (const double &num);
        void _parse (const DECIMAL &num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void _parse (BIGINT num);
#endif
        void _parse (OBJECT obj);
        void _parse (ARRAY arr);
        void _parse (const BOOLEAN &val);
        // other
        void _parse (const nullptr_t &n);

        static void delete_value_static (const short &type, void *src);

        void delete_value ();
        void _set_object ();
        void _set_bool ();
        void _set_array ();
        void _set_string ();
        void _set_integer ();
        void _set_decimal ();
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void _set_bigint ();
#endif
        void _set_nullptr ();
        void _set_pair ();
        void _set_object (OBJECT val);
        void _set_bool (const BOOLEAN &val);
        void _set_array (ARRAY val);
        void _set_string (STRING val);
        void _set_string (STRING_VIEW val);
        void _set_integer (const INTEGER &val);
        void _set_decimal (const DECIMAL &val);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void _set_bigint (BIGINT val);
#endif
        void _set_pair (json first, json second);
#if MANAPIHTTP_JSON_DEBUG
        void _debug_symb_reinit () {
            _debug_bool_src = nullptr;
            _debug_array_src = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            _debug_bigint_src = nullptr;
#endif
            _debug_object_src = nullptr;
            _debug_string_src = nullptr;
            _debug_integer_src = nullptr;
            _debug_decimal_src = nullptr;
            _debug_pair_src = nullptr;

            switch (type)
            {
                case type_array:
                    _debug_array_src = &this->as_array();
                break;
                case type_object:
                    _debug_object_src = &this->as_object();
                break;
                case type_integer:
                    _debug_integer_src = &this->as_integer();
                break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                case type_bigint:
                    _debug_bigint_src = &this->as_bigint();
                break;
#endif
                case type_boolean:
                    _debug_bool_src = &this->as_bool();
                break;
                case type_decimal:
                    _debug_decimal_src = &this->as_decimal();
                break;
                case type_pair:
                    _debug_pair_src = &this->_as_pair();
                break;
                case type_string:
                    _debug_string_src = this->as_string().data();
                break;
                default:
                    break;
            }
        }
#else
        inline void _debug_symb_reinit () {};
#endif

        void    *src = nullptr;
        types   type = type_null;

#if MANAPIHTTP_JSON_DEBUG
        const BOOLEAN *_debug_bool_src    = nullptr;
        const ARRAY   *_debug_array_src   = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        const BIGINT  *_debug_bigint_src  = nullptr;
#endif
        const OBJECT  *_debug_object_src  = nullptr;
        const char  *_debug_string_src  = nullptr;
        const INTEGER *_debug_integer_src  = nullptr;
        const DECIMAL *_debug_decimal_src = nullptr;
        const PAIR    *_debug_pair_src = nullptr;
#endif
    };
}