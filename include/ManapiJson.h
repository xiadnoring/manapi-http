#ifndef MANAPIHTTP_MANAPIJSON_H
#define MANAPIHTTP_MANAPIJSON_H

#include <string>
#include <map>
#include <vector>
#include "ManapiBigint.h"

#define MANAPI_JSON_NULL        0
#define MANAPI_JSON_NUMERIC     1
#define MANAPI_JSON_STRING      2
#define MANAPI_JSON_DECIMAL     3
#define MANAPI_JSON_BOOLEAN     4
#define MANAPI_JSON_MAP         5
#define MANAPI_JSON_ARRAY       6
#define MANAPI_JSON_NUMBER      7
#define MANAPI_JSON_BIGINT      8
#define MANAPI_JSON_PAIR        9

namespace manapi::utils {
    class json {
    public:
        typedef std::map <std::string, json *>  OBJECT;
        typedef std::vector <json *>            ARRAY;

        static json object ();
        static json array ();

        static json array (const std::initializer_list<json> &data);

        json();
        json(const json &other);
        json(const std::initializer_list<json> &data);

        // Do not use explicit

        json(const std::string &str, bool is_json = false);
        json(const std::u32string &str, bool is_json = false);
        json(const ssize_t &num);
        json(const char *plain_text, bool is_json = false);
        json(const int &num);
        json(const double &num);
        json(const double long &num);
        json(const bigint &num);
        json(const nullptr_t &n);
        json(const bool &value);

        ~json();

        // string
        void parse (const std::string &plain_text);
        void parse (const std::u32string &plain_text, const bool &bigint = false, const size_t &bigint_precision = 128, size_t start = 0);

        // numbers
        void parse (const ssize_t &num);
        void parse (const int &num);
        void parse (const double &num);
        void parse (const double long &num);
        void parse (const bigint &num);

        bool contains (const std::string &key) const;

        // other
        void parse (const nullptr_t &n);

        json &operator[]    (const std::string      &key)   const;
        json &operator[]    (const std::u32string   &key)   const;
        json &operator[]    (const size_t           &index) const;

        json &at            (const std::string      &key)   const;
        json &at            (const std::u32string   &key)   const;
        json &at            (const size_t           &index) const;

        // TRASH (no with const json &obj)
        json &operator=     (const std::u32string   &str);
        json &operator=     (const std::string      &str);
        json &operator=     (const char             *str);
        json &operator=     (const bool             &b);
        json &operator=     (const ssize_t          &num);
        json &operator=     (const int              &num);
        json &operator=     (const double           &num);
        json &operator=     (const double long      &num);
        json &operator=     (const long long        &num);
        json &operator=     (const nullptr_t        &n);
        json &operator=     (const bigint           &num);
        json &operator=     (const json             &obj);
        json &operator=     (const std::initializer_list <json> &data);
        json &operator-     (const ssize_t          &num);
        json &operator-     (const int              &num);
        json &operator+     (const ssize_t          &num);
        json &operator+     (const int              &num);

        void insert         (const std::string &key, const json &obj);
        void insert         (const std::u32string &key, const json &obj);
        void erase          (const std::string &key);
        void erase          (const std::u32string &key);

        void push_back      (const json &obj);
        void pop_back       ();

        // to be cool ;)
        void insert         (const std::u32string &key, const std::u32string &arg);
        void insert         (const std::u32string &key, const ssize_t &arg);

        void push_back      (const std::string &arg);
        void push_back      (const std::u32string &arg);
        void push_back      (const ssize_t &arg);

        template<class T>
        auto begin          () const
        {
            return get_ptr<T>()->begin();
        }

        template<class T>
        auto end            () const
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

        template <typename T>
        T get () const { return *reinterpret_cast <T *> (src); }

        template <typename T>
        T* get_ptr () const { return reinterpret_cast <T *> (src); }

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
        short   type                = MANAPI_JSON_NULL;
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
