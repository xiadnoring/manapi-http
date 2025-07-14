#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>
#include <stack>
#include <deque>
#include <set>

#include "ManapiErrors.hpp"
#include "ManapiUtils.hpp"
#include "ManapiBigint.hpp"
#include "ManapiInt.hpp"

namespace manapi {
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
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            /* bigint type */
            type_bigint = 8,
#endif
            /* pair<string, string> type */
            type_pair = 9
        };

        /**
         * Generate a JSON object
         * @return the created JSON object
         */
        static json object ();

        /**
         * Generate a JSON array
         * @return the created JSON array
         */
        static json array ();

        /**
         * Generate a JSON array from the provided initializer list
         * @param data the provided initializer list
         * @return the create JSON array
         */
        static json array (const std::initializer_list<json> &data);

        /**
         * Generate a JSON object from the provided initializer list
         * @param data the provided initializer list
         * @return the created JSON object
         */
        static json object (const std::initializer_list<json> &data);

        /**
         * Generate a JSON array using the other JSON
         *
         * If the provided JSON is a pair, it returns JSON array with 2 items from the pair,
         * but otherwise, it returns JSON array with 1 items
         *
         * @param data the provided JSON
         * @return the created JSON array
         */
        static json array (manapi::json data);

        /**
         * Parse JSON from the string
         *
         * @param data the source string
         * @return the json object on success, otherwise it returns ParseError, InternalError, ResourceExhausted
         */
        static manapi::error::status_or<json> parse (STRING_VIEW data);

        static std::string stringify (const json &n, int spaces = 2);

        json();

        json(const json &other);

        json(json &&other) noexcept;

        json(const std::initializer_list<json> &data);

        // Do not use explicit

        json(STRING_VIEW str, bool parse = false);

        json(const UNICODE_STRING &str, bool parse = false);

        json(INTEGER num);

        json(const char *plain_text, bool parse = false);

        json(STRING str);

        json(DECIMAL num);

        json(const NULLPTR &n);

        json(BOOLEAN value);

        json(OBJECT obj);

        json(ARRAY arr);

        template<typename T>
        json (std::map<std::string, T> n) {
            while (!n.empty()) {
                auto e = n.extract(n.begin());
                this->insert({std::move(e.key()), std::move(e.mapped())});
            }
        }

        template<typename T>
        json (std::unordered_map<std::string, T> n) {
            while (!n.empty()) {
                auto e = n.extract(n.begin());
                this->insert({std::move(e.key()), std::move(e.mapped())});
            }
        }

        template<typename T>
        requires(std::is_integral_v<T>)
        json(const T &n) {
            this->parse_ (static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json (const T &n) {
            this->parse_ (static_cast<DECIMAL>(n));
        }

        template<typename V>
        json(std::vector<V> array) {
            this->set_array_();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::deque<V> array) {
            this->set_array_();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::stack<V> array) {
            this->set_array_();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        template<typename V>
        json(std::set<V> array) {
            this->set_array_();
            while (!array.empty()) { this->push_back(std::move(array.extract(array.begin()).value())); }
        }

        template<typename V, std::size_t N>
        json(std::array<V, N> array) {
            this->set_array_();
            for (auto &v: array) { this->push_back(std::move(v)); }
        }

        ~json();

        [[nodiscard]] bool contains (const std::string &key) const;

        const json &operator[] (const STRING &key) const;
        const json &operator[] (const UNICODE_STRING &key) const;
        const json &operator[] (size_t index) const;

        json &operator[] (const STRING &key);
        json &operator[] (const UNICODE_STRING &key);
        json &operator[] (size_t index);

        [[nodiscard]] const json &at (const STRING &key) const;
        [[nodiscard]] const json &at (const UNICODE_STRING &key) const;
        [[nodiscard]] const json &at (size_t index) const;

        json &at (const std::string &key);
        json &at (const UNICODE_STRING &key);
        json &at (size_t index);

        // TRASH (no with const json &obj)
        json &operator= (const UNICODE_STRING &str);
        json &operator= (STRING str);
        json &operator= (const char *str);
        json &operator= (BOOLEAN b);
        json &operator= (INTEGER num);
        json &operator= (DECIMAL num);
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
        json operator*= (const T &n) {
            return this->operator*(static_cast<INTEGER>(n));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        json operator*= (const T &n) {
            return this->operator* (static_cast<DECIMAL>(n));
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

        std::pair<OBJECT::iterator, bool> insert (const STRING &key, json obj);

        std::pair<OBJECT::iterator, bool> insert (const UNICODE_STRING &key, json obj);

        std::pair<OBJECT::iterator, bool> insert (const OBJECT::value_type &v);

        std::pair<OBJECT::iterator, bool> insert (OBJECT::value_type &&v);

        void erase (const STRING &key);

        void erase (const UNICODE_STRING &key);

        ARRAY::iterator erase (ARRAY::iterator it);

        OBJECT::iterator erase (OBJECT::iterator it);

        ARRAY::const_iterator erase (ARRAY::const_iterator it);

        OBJECT::const_iterator erase (OBJECT::const_iterator it);

        void push_back (json obj);

        void push_back (ARRAY::const_iterator begin, ARRAY::const_iterator end);

        void pop_back ();

        [[nodiscard]] int data_type () const;

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

        //OBJECT::iterator find (const STRING &key);

        OBJECT::iterator find (STRING_VIEW key);

        OBJECT::const_iterator find (STRING_VIEW key) const;

        [[nodiscard]] const ARRAY &each() const;
        [[nodiscard]] const OBJECT &entries() const;
        [[nodiscard]] ARRAY &each();
        [[nodiscard]] OBJECT &entries();

        [[nodiscard]] bool contains       (const UNICODE_STRING &key) const;

        manapi::json &first ();

        manapi::json &second ();

        [[nodiscard]] const manapi::json &first () const;

        [[nodiscard]] const manapi::json &second () const;

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
        [[nodiscard]] bool is_pair        () const;

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

        [[nodiscard]] std::string dump (int spaces = 0, int first_spaces = 0) const;

        [[nodiscard]] size_t size () const;
        [[nodiscard]] bool empty () const;

        static void error_invalid_char (const UNICODE_STRING &plain_text, size_t i);
        static void error_invalid_char (const STRING_VIEW &plain_text, size_t i);
        static void error_unexpected_end (size_t i);

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
        [[nodiscard]] OBJECT &as_object_ () const;
        [[nodiscard]] ARRAY &as_array_ () const;
        [[nodiscard]] STRING &as_string_ () const;
        [[nodiscard]] INTEGER &as_integer_ () const;
        [[nodiscard]] DECIMAL &as_decimal_ () const;
        [[nodiscard]] BOOLEAN &as_bool_ () const;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        [[nodiscard]] BIGINT &as_bigint_ () const;
#endif
        [[nodiscard]] PAIR &as_pair_ () const;

        // string
        manapi::error::status parse_ (const UNICODE_STRING &plain_text);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        manapi::error::status parse_ (STRING_VIEW plain_text, bool bigint = false, size_t bigint_precision = 128);
#else
        manapi::error::status parse_ (STRING_VIEW plain_text);
#endif
        // integers
        void parse_ (size_t num);
        void parse_ (INTEGER num);
        void parse_ (int num);
        void parse_ (double num);
        void parse_ (DECIMAL num);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void parse_ (BIGINT num);
#endif
        void parse_ (OBJECT obj);
        void parse_ (ARRAY arr);
        void parse_ (BOOLEAN val);
        // other
        void parse_ (const nullptr_t &n);

        static void delete_value_static (short type, void *src);

        void delete_value ();
        void set_object_ ();
        void set_bool_ ();
        void set_array_ ();
        void set_string_ ();
        void set_integer_ ();
        void set_decimal_ ();
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void set_bigint_ ();
#endif
        void set_nullptr_ ();
        void set_pair_ ();
        void set_object_ (OBJECT val);
        void set_bool_ (BOOLEAN val);
        void set_array_ (ARRAY val);
        void set_string_ (STRING val);
        void set_string_ (STRING_VIEW val);
        void set_integer_ (INTEGER val);
        void set_decimal_ (DECIMAL val);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        void set_bigint_ (BIGINT val);
#endif
        void set_pair_ (json first, json second);
#if MANAPIHTTP_JSON_DEBUG
        void debug_symb_reinit_ () {
            this->debug_bool_src_ = nullptr;
            this->debug_array_src_ = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            this->debug_bigint_src_ = nullptr;
#endif
            this->debug_object_src_ = nullptr;
            this->debug_string_src_ = nullptr;
            this->debug_integer_src_ = nullptr;
            this->debug_decimal_src_ = nullptr;
            this->debug_pair_src_ = nullptr;

            switch (this->type)
            {
                case type_array:
                    this->debug_array_src_ = &this->as_array();
                break;
                case type_object:
                    this->debug_object_src_ = &this->as_object();
                break;
                case type_integer:
                    this->debug_integer_src_ = &this->as_integer();
                break;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                case type_bigint:
                    this->debug_bigint_src_ = &this->as_bigint();
                break;
#endif
                case type_boolean:
                    this->debug_bool_src_ = &this->as_bool();
                break;
                case type_decimal:
                    this->debug_decimal_src_ = &this->as_decimal();
                break;
                case type_pair:
                    this->debug_pair_src_ = &this->as_pair_();
                break;
                case type_string:
                    this->debug_string_src_ = this->as_string().data();
                break;
                default:
                    break;
            }
        }
#else
        inline void debug_symb_reinit_ () {};
#endif

        void    *src = nullptr;
        types   type = type_null;

#if MANAPIHTTP_JSON_DEBUG
        const BOOLEAN *debug_bool_src_    = nullptr;
        const ARRAY   *debug_array_src_   = nullptr;
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        const BIGINT  *debug_bigint_src_  = nullptr;
#endif
        const OBJECT  *debug_object_src_  = nullptr;
        const char  *debug_string_src_  = nullptr;
        const INTEGER *debug_integer_src_  = nullptr;
        const DECIMAL *debug_decimal_src_ = nullptr;
        const PAIR    *debug_pair_src_ = nullptr;
#endif
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
        [[nodiscard]] const char *what () const noexcept override;

        /**
         * Get the json error number
         * @return the json error number
         */
        [[nodiscard]] const json_err_num &err_num () const;
    private:
        std::string message;
        json_err_num errnum;
    };
}