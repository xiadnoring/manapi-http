#ifndef MANAPIJSONBUILDER_HPP
#define MANAPIJSONBUILDER_HPP

#include <functional>
#include <memory>

#include "ManapiJson.hpp"

namespace manapi::net::utils {
    class json_builder {
    public:
        explicit json_builder (const bool &use_bigint = false, const size_t &bigint_precision = 128);
        ~json_builder();
        json_builder &operator<< (const std::string &str);
        json get ();

        /**
         *
         * @param plain_text
         * @param i
         * @param left
         * @return true if wchar are contained the following char
         */
        static bool                        _valid_utf_char (const std::string &plain_text, const size_t &i, size_t &left);
        static void                        _valid_utf_string (const std::string &str);
    private:
        void _reset ();
        void _parse (const std::string &plain_text, size_t &j, bool root = true);
        void _check_type (const std::string &plain_text, size_t &j);
        void _build_string (const std::string &plain_text, size_t &j);
        void _build_numeric (const std::string &plain_text, size_t &j);
        void _build_numeric_string (const std::string &plain_text, size_t &j);
        void _build_object (const std::string &plain_text, size_t &j);
        void _build_array (const std::string &plain_text, size_t &j);
        void _check_end (const std::string &plain_text, size_t &j);


        json::types type = json::type_null;
        json object;
        size_t i = 0;
        std::function<void(const std::string &, size_t &j)> action;

        // bigint
        bool use_bigint = false;
        size_t bigint_precision = 128;

        // build vars
        bool opened_quote = false;
        // how many char are contained in the wchar
        size_t wchar_left = 0;
        // escaped with '/'
        bool escaped = false;
        // is key in _build_object
        bool is_key = true;
        std::string key{};

        bool go_to_delimiter = false;

        size_t start_cut;
        size_t end_cut;

        std::string buffer;

        bool getting = false;
        bool ready = false;

        std::unique_ptr<json_builder> item = nullptr;
    };
}

#endif //MANAPIJSONBUILDER_HPP
