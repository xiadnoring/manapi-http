#ifndef MANAPIHTTP_MANAPIJSON_H
#define MANAPIHTTP_MANAPIJSON_H

#include <string>
#include <map>
#include <vector>
#include "ManapiDecimal.h"

#define MANAPI_JSON_NULL        0
#define MANAPI_JSON_NUMERIC     1
#define MANAPI_JSON_STRING      2
#define MANAPI_JSON_DECIMAL     3
#define MANAPI_JSON_BOOLEAN     4
#define MANAPI_JSON_MAP         5
#define MANAPI_JSON_ARRAY       6
#define MANAPI_JSON_NUMBER      7
#define MANAPI_JSON_BIGINT      8

namespace manapi::toolbox {
    class json {
    public:
        static json object ();
        static json array ();

        json();
        json(const json &other);
        explicit json(const std::string &plain_text);
        explicit json(const ssize_t &num);
        ~json();
        void parse (const std::string &plain_text, const bool &bigint = false, const size_t &bigint_precision = 128, size_t start = 0);
        void parse (const ssize_t &num);

        json &operator[]    (const std::string      &key)   const;
        json &operator[]    (const size_t           &index) const;

        json &at            (const std::string      &key)   const;
        json &at            (const size_t           &index) const;

        json &operator=     (const std::string      &str);
        json &operator=     (const char             *str);
        json &operator=     (const bool             &b);
        json &operator=     (const ssize_t    &num);
        json &operator=     (const int              &num);
        json &operator=     (const double           &num);
        json &operator=     (const nullptr_t        &n);
        json &operator=     (const decimal          &num);
        json &operator=     (const json             &obj);

        void insert         (const std::string &key, const json &obj);
        void erase          (const std::string &key);

        void push_back      (const json &obj);
        void pop_back       ();

        // to be cool ;)
        void insert         (const std::string &key, const std::string &arg);
        void insert         (const std::string &key, const ssize_t &arg);

        void push_back      (const std::string &arg);
        void push_back      (const ssize_t &arg);

        bool contains       (const std::string &key);

        [[nodiscard]] bool is_object      () const;
        [[nodiscard]] bool is_array       () const;
        [[nodiscard]] bool is_string      () const;
        [[nodiscard]] bool is_number      () const;
        [[nodiscard]] bool is_null        () const;
        [[nodiscard]] bool is_decimal     () const;
        [[nodiscard]] bool is_bigint      () const;

        template <typename T>
        T get () const { return *reinterpret_cast <T *> (src); }

        template <typename T>
        T* get_ptr () const { return reinterpret_cast <T *> (src); }

        std::string dump (const size_t &spaces = 0, const size_t &first_spaces = 0);

        size_t size ();
    protected:
        [[nodiscard]] size_t get_start_cut  ()      const;
        [[nodiscard]] size_t get_end_cut    ()      const;

        bool   root                 = true;
    private:
        static void                 error_invalid_char (const std::string &plain_text, const size_t &i);
        static void                 error_unexpected_end (const size_t &i);
        void                        delete_value ();
        void    *src                = nullptr;
        short   type                = MANAPI_JSON_NULL;
        size_t  start_cut           = 0;
        size_t  end_cut             = 0;
        bool    use_bigint          = false;
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
