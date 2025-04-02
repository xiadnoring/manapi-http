#pragma once

#include <functional>
#include <memory>

#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"

namespace manapi {
    class json_builder {
    public:
        explicit json_builder (const json_mask &mask = nullptr);
        explicit json_builder (const json &mask);
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        explicit json_builder (const json_mask &mask, bool use_bigint, size_t bigint_precision = 128);
        explicit json_builder (const json &mask, bool use_bigint, size_t bigint_precision = 128);
#endif
        ~json_builder();
        json_builder &operator<< (std::string_view str);
        json_builder &operator<< (const char &c);
        json get ();
        [[nodiscard]] const bool &is_ready () const;
        [[nodiscard]] bool is_empty () const;
        void clear ();
        /**
         *
         * @param plain_text
         * @param i
         * @param left
         * @return true if wchar are contained the following char
         */
        static bool _valid_utf_char (std::string_view plain_text, const size_t &i, size_t &left);
        static void _valid_utf_string (std::string_view str);
    private:
        void _reset ();
        void _parse (std::string_view plain_text, size_t &j, bool root = true);
        void _check_type (std::string_view plain_text, size_t &j);
        void _build_string (std::string_view plain_text, size_t &j);
        void _build_numeric (std::string_view plain_text, size_t &j);
        void _build_numeric_string (std::string_view plain_text, size_t &j);
        void _build_object (std::string_view plain_text, size_t &j);
        void _build_array (std::string_view plain_text, size_t &j);
        void _check_end (std::string_view plain_text, size_t &j);

        void _reset_type ();
        void _next_type ();
        bool _next_parent ();
        void _check_eq_type ();
        bool _check_max_mean (const bool &building = false);
        bool _check_min_mean ();
        bool _check_type_none_complex_value ();
        bool _check_default ();
        bool _check_meta_value ();
        void _check_string ();
        void _check_numeric ();
        void _check_object ();
        void _check_array ();

        void _check_part_object ();

        [[nodiscard]] const json &get_current_type ();


        json::types type = json::type_null;
        json object;
        //json_mask mask;
        size_t i = 0;
        std::function<void(std::string_view , size_t &j)> action;

        const json *current_types;
        size_t current_type;

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        // bigint
        bool use_bigint = false;
        size_t bigint_precision = 128;
#endif

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
        const ssize_t *max_mean_size = nullptr;
        const ssize_t *min_mean_size = nullptr;

        bool getting = false;
        bool ready = false;
        bool exp_already = false;
        bool operate_already = false;
        int utf_escaped_status = -1;
        unsigned char utf_escaped[4]{};

        size_t element_index = 0;

        std::unique_ptr<json_builder> item = nullptr;
        std::function <const json &()> next_parent;
    };
}