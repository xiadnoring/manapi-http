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
        json(const std::u32string &str, bool is_json = false);
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
        void parse (const std::string &plain_text);
        void parse (const std::u32string &plain_text, const bool &bigint = false, const size_t &bigint_precision = 128, size_t start = 0);

        // numbers
        void parse (const size_t &num);
        void parse (const NUMBER &num);
        void parse (const int &num);
        void parse (const double &num);
        void parse (const DECIMAL &num);
        void parse (const BIGINT &num);
        void parse (const OBJECT &obj);
        void parse (const ARRAY &arr);

        bool contains (const std::string &key) const;

        // other
        void parse (const nullptr_t &n);

        json &operator[]    (const std::string      &key)   const;
        json &operator[]    (const std::u32string   &key)   const;
        json &operator[]    (const size_t           &index) const;

        [[nodiscard]] json &at            (const std::string      &key)   const;
        [[nodiscard]] json &at            (const std::u32string   &key)   const;
        [[nodiscard]] json &at            (const size_t           &index) const;

        // TRASH (no with const json &obj)
        json &operator=     (const std::u32string   &str);
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


        void insert         (const std::string &key, const json &obj);
        void insert         (const std::u32string &key, const json &obj);
        void erase          (const std::string &key);
        void erase          (const std::u32string &key);

        void push_back      (json obj);
        void pop_back       ();

        // to be cool ;)
        void insert         (const std::u32string &key, const std::u32string &arg);
        void insert         (const std::u32string &key, const ssize_t &arg);

        void push_back      (const std::string &arg);
        void push_back      (const std::u32string &arg);
        void push_back      (const ssize_t &arg);

        template<class T>
        constexpr auto begin          () const
        {
            return get_ptr<T>()->begin();
        }

        template<class T>
        constexpr auto end            () const
        {
            return get_ptr<T>()->end();
        }

        [[nodiscard]] bool contains       (const std::u32string &key) const;

        [[nodiscard]] bool is_object      () const;
        [[nodiscard]] bool is_array       () const;
        [[nodiscard]] bool is_string      () const;
        [[nodiscard]] bool is_number      () const;
        [[nodiscard]] bool is_null        () const;
        [[nodiscard]] bool is_decimal     () const;
        [[nodiscard]] bool is_bigint      () const;
        [[nodiscard]] bool is_bool        () const;

        [[nodiscard]] const OBJECT &as_object () const;
        [[nodiscard]] const ARRAY &as_array () const;
        [[nodiscard]] STRING as_string () const;
        [[nodiscard]] NUMBER as_number () const;
        [[nodiscard]] NULLPTR as_null () const;
        [[nodiscard]] DECIMAL as_decimal () const;
        [[nodiscard]] BIGINT as_bigint () const;
        [[nodiscard]] const BOOLEAN &as_bool () const;

        template <typename T>
        T &get () const { return *static_cast <T *> (src); }

        template <typename T>
        T* get_ptr () const { return static_cast <T *> (src); }

        [[nodiscard]] std::string dump    (const size_t &spaces = 0, const size_t &first_spaces = 0) const;

        [[nodiscard]] size_t size () const;
    protected:
        [[nodiscard]] size_t get_start_cut  ()      const;
        [[nodiscard]] size_t get_end_cut    ()      const;

        bool   root                 = true;
    private:
        static void                 error_invalid_char (const std::u32string &plain_text, const size_t &i);
        static void                 error_unexpected_end (const size_t &i);
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
