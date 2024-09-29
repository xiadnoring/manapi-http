#ifndef MANAPIHTTP_MANAPIJSON_H
#define MANAPIHTTP_MANAPIJSON_H

#include <string>
#include <map>
#include <vector>
#include "ManapiBigint.hpp"

namespace manapi::utils {
    class json {
    public:
        typedef std::map <std::string, manapi::utils::json> OBJECT;
        typedef std::vector <manapi::utils::json> ARRAY;
        typedef double long DECIMAL;
        typedef ssize_t NUMBER;
        typedef nullptr_t NULLPTR;
        typedef std::string STRING;
        typedef bigint BIGINT;
        typedef bool BOOLEAN;

        typedef std::u32string UNICODE_STRING;

        enum types {
            type_null = 0,
            type_numeric = 1,
            type_string = 2,
            type_decimal = 3,
            type_boolean = 4,
            type_object = 5,
            type_array = 6,
            type_number = 7,
            type_bigint = 8,
            type_pair = 9
        };

        static json object ();
        static json array ();

        static json array (const std::initializer_list<json> &data);

        json();
        json(const json &other);
        json(json &&other) noexcept;
        json(const std::initializer_list<json> &data);

        // Do not use explicit

        json(const STRING &str, bool is_json = false);
        json(const UNICODE_STRING &str, bool is_json = false);
        json(const NUMBER &num);
        json(const size_t &num);
        json(const char *plain_text, bool is_json = false);
        json(const int &num);
        json(const double &num);
        json(const DECIMAL &num);
        json(const BIGINT &num);
        json(const NULLPTR &n);
        json(const BOOLEAN &value);
        json(const OBJECT &obj);
        json(const ARRAY &arr);

        ~json();

        // string
        void parse (const UNICODE_STRING &plain_text);
        void parse (const STRING &plain_text, const bool &bigint = false, const size_t &bigint_precision = 128);

        // numbers
        void parse (const size_t &num);
        void parse (const NUMBER &num);
        void parse (const int &num);
        void parse (const double &num);
        void parse (const DECIMAL &num);
        void parse (const BIGINT &num);
        void parse (const OBJECT &obj);
        void parse (const ARRAY &arr);
        void parse (const BOOLEAN &val);

        bool contains (const std::string &key) const;

        // other
        void parse (const nullptr_t &n);

        const json &operator[] (const STRING &key) const;
        const json &operator[] (const UNICODE_STRING &key) const;
        const json &operator[] (const size_t &index) const;
        const json &operator[] (const int &index) const;

        json &operator[] (const STRING &key);
        json &operator[] (const UNICODE_STRING &key);
        json &operator[] (const size_t &index);
        json &operator[] (const int &index);

        [[nodiscard]] const json &at (const std::string &key) const;
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
        json &operator=     (const NUMBER           &num);
        json &operator=     (const int              &num);
        json &operator=     (const double           &num);
        json &operator=     (const DECIMAL          &num);
        json &operator=     (const long long        &num);
        json &operator=     (const NULLPTR          &n);
        json &operator=     (const BIGINT           &num);
        json &operator=     (const json             &obj);
        json &operator=     (json                   &&obj);
        json &operator=     (const std::initializer_list <json> &data);
        json &operator-     (const NUMBER           &num);
        json &operator-     (const int              &num);
        json &operator-     (const DECIMAL          &num);
        json &operator-     (const double           &num);
        json &operator-     (const BIGINT           &num);
        json &operator+     (const NUMBER           &num);
        json &operator+     (const int              &num);
        json &operator+     (const DECIMAL          &num);
        json &operator+     (const double           &num);
        json &operator+     (const BIGINT           &num);
        json &operator+     (const STRING           &str);
        void operator+=     (const STRING           &str);
        void operator-=     (const NUMBER           &num);
        void operator-=     (const int              &num);
        void operator-=     (const DECIMAL          &num);
        void operator-=     (const double           &num);
        void operator-=     (const BIGINT           &num);
        void operator+=     (const NUMBER           &num);
        void operator+=     (const int              &num);
        void operator+=     (const DECIMAL          &num);
        void operator+=     (const double           &num);
        void operator+=     (const BIGINT           &num);


        void insert         (const STRING &key, const json &obj);
        void insert         (const UNICODE_STRING &key, const json &obj);
        void erase          (const STRING &key);
        void erase          (const UNICODE_STRING &key);

        void push_back      (json obj);
        void pop_back       ();

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
        [[nodiscard]] bool is_number      () const;
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
         * strict number retrieval
         * @return
         */
        [[nodiscard]] const NUMBER &as_number () const;
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
         * number - string
         * bigint - string
         * decimal - string
         * @return
         */
        [[nodiscard]] STRING as_string_cast () const;
        /**
         * non-strict number retrieval
         *
         * @note number - number
         * @note decimal - number
         * @note bigint - number
         * @return
         */
        [[nodiscard]] NUMBER as_number_cast () const;
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
         * @note number - decimal
         * @return
         */
        [[nodiscard]] DECIMAL as_decimal_cast () const;
        /**
         * non-strict bigint retrieval
         *
         * @note bigint - bigint
         * @note number - bigint
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


        [[nodiscard]] std::string dump (const size_t &spaces = 0, const size_t &first_spaces = 0) const;

        [[nodiscard]] size_t size () const;

        static void                 error_invalid_char (const UNICODE_STRING &plain_text, const size_t &i);
        static void                 error_invalid_char (const STRING &plain_text, const size_t &i);
        static void                 error_unexpected_end (const size_t &i);
    protected:
        [[nodiscard]] size_t get_start_cut () const;
        [[nodiscard]] size_t get_end_cut () const;

        bool   root                 = true;
    private:
        static void                 delete_value_static (const short &type, void *src);
        void                        throw_could_not_use_func (const std::string &func) const;

        void                        delete_value ();

        void    *src                = nullptr;
        types   type                = type_null;
        size_t  start_cut           = 0;
        size_t  end_cut             = 0;

    };

    class json_parse_exception : public std::exception {
    public:
        explicit json_parse_exception(const std::string &msg);
        [[nodiscard]] const char *what () const noexcept override;
    private:
        std::string message;
    };
}

#endif //MANAPIHTTP_MANAPIJSON_H
