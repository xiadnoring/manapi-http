#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>
#include "ManapiBigint.hpp"
#include "ManapiParams.hpp"

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
        json_parse_exception (json_parse_exception &&n) noexcept ;
        [[nodiscard]] const char *what () const noexcept override;
        [[nodiscard]] const json_err_num &get_err_num () const;
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
        typedef bigint BIGINT;
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
            type_bigint = 8,
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

        json(const STRING_VIEW &str, const bool &parse = false);
        json(const UNICODE_STRING &str, const bool &parse = false);
        json(const INTEGER &num);
        json(const size_t &num);
        json(const char *plain_text, const bool &parse = false);
        json(const int &num);
        json(const double &num);
        json(const STRING &str);
        json(const DECIMAL &num);
        json(const BIGINT &num);
        json(const NULLPTR &n);
        json(const BOOLEAN &value);
        json(const OBJECT &obj);
        json(const ARRAY &arr);

        ~json();

        [[nodiscard]] bool contains (const std::string &key) const;


        const json &operator[] (const STRING &key) const;
        const json &operator[] (const UNICODE_STRING &key) const;
        const json &operator[] (const size_t &index) const;
        const json &operator[] (const int &index) const;

        json &operator[] (const STRING &key);
        json &operator[] (const UNICODE_STRING &key);
        json &operator[] (const size_t &index);
        json &operator[] (const int &index);

        [[nodiscard]] const json &at (const STRING &key) const;
        [[nodiscard]] const json &at (const UNICODE_STRING &key) const;
        [[nodiscard]] const json &at (const size_t &index) const;
        [[nodiscard]] const json &at (const int &index) const;

        json &at (const std::string &key);
        json &at (const UNICODE_STRING &key);
        json &at (const size_t &index);
        json &at (const int &index);


        // TRASH (no with const json &obj)
        json &operator=     (const UNICODE_STRING   &str);
        json &operator=     (const STRING           &str);
        json &operator=     (const char             *str);
        json &operator=     (const BOOLEAN          &b);
        json &operator=     (const INTEGER           &num);
        json &operator=     (const int              &num);
        json &operator=     (const double           &num);
        json &operator=     (const DECIMAL          &num);
        json &operator=     (const long long        &num);
        json &operator=     (const NULLPTR          &n);
        json &operator=     (const BIGINT           &num);
        json &operator=     (const json             &obj);
        json &operator=     (json                   &&obj);
        json &operator=     (const std::initializer_list <json> &data);
        json operator-      (const INTEGER           &num);
        json operator-      (const int              &num);
        json operator-      (const DECIMAL          &num);
        json operator-      (const double           &num);
        json operator-      (const BIGINT           &num);
        json operator+      (const INTEGER           &num);
        json operator+      (const int              &num);
        json operator+      (const DECIMAL          &num);
        json operator+      (const double           &num);
        json operator+      (const BIGINT           &num);
        json operator+      (const STRING           &str);
        void operator+=     (const STRING           &str);
        void operator-=     (const INTEGER           &num);
        void operator-=     (const int              &num);
        void operator-=     (const DECIMAL          &num);
        void operator-=     (const double           &num);
        void operator-=     (const BIGINT           &num);
        void operator+=     (const INTEGER           &num);
        void operator+=     (const int              &num);
        void operator+=     (const DECIMAL          &num);
        void operator+=     (const double           &num);
        void operator+=     (const BIGINT           &num);

        bool operator==     (const json &n) const;
        bool operator==     (const bool &n) const;
        bool operator==     (const char *n) const;
        bool operator==     (const std::string_view &n) const;
        bool operator==     (const std::string &n) const;
        bool operator==     (const ssize_t &n) const;
        bool operator==     (const int &n) const;
        bool operator==     (const nullptr_t &n) const;


        void insert (const STRING &key, const json &obj);
        void insert (const UNICODE_STRING &key, const json &obj);
        void erase (const STRING &key);
        void erase (const UNICODE_STRING &key);

        void push_back (json obj);
        void push_back (ARRAY::const_iterator begin, ARRAY::const_iterator end);
        void pop_back ();

        template<class T> constexpr auto begin () const
        { return get_ptr<T>()->begin(); }

        template<class T> constexpr auto end () const
        { return get_ptr<T>()->end(); }

        template<class T> auto begin ()
        { return get_ptr<T>()->begin(); }

        template<class T> auto end ()
        { return get_ptr<T>()->end(); }

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
        [[nodiscard]] bool is_bigint      () const;
        [[nodiscard]] bool is_bool        () const;

        /**
         * strict object retrieval
         * @return
         */
        [[nodiscard]] const OBJECT &as_object () const;
        /**
         * strict array retrieval
         * @return
         */
        [[nodiscard]] const ARRAY &as_array () const;
        /**
         * strict string retrieval
         * @return
         */
        [[nodiscard]] const STRING &as_string () const;
        /**
         * strict integer retrieval
         * @return
         */
        [[nodiscard]] const INTEGER &as_integer () const;
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
        /**
         * strict bigint retrieval
         * @return
         */
        [[nodiscard]] const BIGINT &as_bigint () const;
        /**
         * strict boolean retrieval
         * @return
         */
        [[nodiscard]] const BOOLEAN &as_bool () const;

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
         * non-strict bigint retrieval
         *
         * @note bigint - bigint
         * @note integer - bigint
         * @note decimal - bigint
         * @note string - bigint
         * @return
         */
        [[nodiscard]] BIGINT as_bigint_cast () const;
        /**
         * non-strict boolean retrieval
         *
         * @note boolean - boolean
         * @return
         */
        [[nodiscard]] BOOLEAN as_bool_cast () const;

        template <typename T>
        [[nodiscard]] T &get () { return *static_cast <T *> (src); }

        template <typename T>
        T* get_ptr () { return static_cast <T *> (src); }

        template <typename T>
        [[nodiscard]] const T &get () const { return *static_cast <T *> (src); }

        template <typename T>
        const T* get_ptr () const { return static_cast <T *> (src); }


        [[nodiscard]] std::string dump (const int &spaces = 0, const int &first_spaces = 0) const;

        [[nodiscard]] size_t size () const;
        [[nodiscard]] bool empty () const;

        static void error_invalid_char (const UNICODE_STRING &plain_text, const size_t &i);
        static void error_invalid_char (const STRING_VIEW &plain_text, const size_t &i);
        static void error_unexpected_end (const size_t &i);
    protected:
        bool root = true;
    private:

        // string
        void _parse (const UNICODE_STRING &plain_text);
        void _parse (const STRING_VIEW &plain_text, const bool &bigint = false, const size_t &bigint_precision = 128);

        // integers
        void _parse (const size_t &num);
        void _parse (const INTEGER &num);
        void _parse (const int &num);
        void _parse (const double &num);
        void _parse (const DECIMAL &num);
        void _parse (const BIGINT &num);
        void _parse (const OBJECT &obj);
        void _parse (const ARRAY &arr);
        void _parse (const BOOLEAN &val);
        // other
        void _parse (const nullptr_t &n);
        
        static void delete_value_static (const short &type, void *src);
        [[nodiscard]] manapi::json_parse_exception throw_could_not_use_func (const std::string &func) const;

        void delete_value ();
        void _set_object ();
        void _set_bool ();
        void _set_array ();
        void _set_string ();
        void _set_integer ();
        void _set_decimal ();
        void _set_bigint ();
        void _set_nullptr ();
        void _set_pair ();
        void _set_object (const OBJECT &val);
        void _set_bool (const BOOLEAN &val);
        void _set_array (const ARRAY &val);
        void _set_string (const STRING_VIEW &val);
        void _set_integer (const INTEGER &val);
        void _set_decimal (const DECIMAL &val);
        void _set_bigint (const BIGINT &val);
        void _set_pair (json first, json second);
#if MANAPIHTTP_JSON_DEBUG
        void _debug_symb_reinit () {
            _debug_bool_src = nullptr;
            _debug_array_src = nullptr;
            _debug_bigint_src = nullptr;
            _debug_object_src = nullptr;
            _debug_string_src = nullptr;
            _debug_integer_src = nullptr;
            _debug_decimal_src = nullptr;
            _debug_pair_src = nullptr;

            switch (type)
            {
                case type_array:
                    _debug_array_src = &get<ARRAY> ();
                break;
                case type_object:
                    _debug_object_src = &get<OBJECT> ();
                break;
                case type_integer:
                    _debug_integer_src = &get<INTEGER> ();
                break;
                case type_bigint:
                    _debug_bigint_src = &get<BIGINT> ();
                break;
                case type_boolean:
                    _debug_bool_src = &get<BOOLEAN> ();
                break;
                case type_decimal:
                    _debug_decimal_src = &get<DECIMAL> ();
                break;
                case type_pair:
                    _debug_pair_src = &get<PAIR> ();
                break;
                case type_string:
                    _debug_string_src = &get<STRING> ();
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
        const BIGINT  *_debug_bigint_src  = nullptr;
        const OBJECT  *_debug_object_src  = nullptr;
        const STRING  *_debug_string_src  = nullptr;
        const INTEGER *_debug_integer_src  = nullptr;
        const DECIMAL *_debug_decimal_src = nullptr;
        const PAIR    *_debug_pair_src = nullptr;
#endif
    };
}