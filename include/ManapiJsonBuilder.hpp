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
        json_builder &operator<< (char c);
        error::status parse (std::string_view str);
        error::status parse (char c);
        manapi::error::status_or<json> get ();
        [[nodiscard]] bool is_ready () const;
        [[nodiscard]] bool is_empty () const;
        void clear ();
        /**
         *
         * @param plain_text
         * @param i
         * @param left
         * @return true if wchar are contained the following char
         */
        static bool _valid_utf_char (std::string_view plain_text, size_t i, size_t &left);
        static void _valid_utf_string (std::string_view str);
    private:
        static error::status_or<const manapi::json *> next_parent_cb (json_builder *p);
        void _reset ();
        error::status _parse (std::string_view plain_text, size_t &j, bool root = true);
        error::status _check_type (std::string_view plain_text, size_t &j);
        error::status _build_string (std::string_view plain_text, size_t &j);
        error::status _build_numeric (std::string_view plain_text, size_t &j);
        error::status _build_numeric_string (std::string_view plain_text, size_t &j);
        error::status _build_object (std::string_view plain_text, size_t &j);
        error::status _build_array (std::string_view plain_text, size_t &j);
        error::status _check_end (std::string_view plain_text, size_t &j);

        void _reset_type ();
        bool _next_type ();
        bool _next_parent ();
        bool _check_eq_type ();
        manapi::error::status _check_max_mean (bool building = false);
        manapi::error::status _check_min_mean ();
        manapi::error::status _check_type_none_complex_value ();
        manapi::error::status _check_default ();
        manapi::error::status _check_meta_value ();
        manapi::error::status _check_string ();
        manapi::error::status _check_numeric ();
        manapi::error::status _check_object ();
        manapi::error::status _check_array ();

        manapi::error::status _check_part_object ();
        manapi::error::status call_action_(const std::string_view &plain_text, size_t &j);

        [[nodiscard]] const json &get_current_type ();


        json::types type;
        json object;
        //json_mask mask;
        size_t i;
        int action;

        std::unique_ptr<std::vector<std::string_view>> path;
        const json *current_types;
        size_t current_type;

#ifdef MANAPIHTTP_BIGINT_SUPPORT
        // bigint
        size_t bigint_precision = 128;
#endif

        // how many char are contained in the wchar
        size_t wchar_left = 0;
        std::string key{};

        size_t start_cut;
        size_t end_cut;

        std::string buffer;
        const manapi::json *max_mean_size = nullptr;
        const manapi::json *min_mean_size = nullptr;

        int flags;
        int utf_escaped_status = -1;
        unsigned char utf_escaped[4]{};

        size_t element_index = 0;

        std::unique_ptr<json_builder> item = nullptr;
        json_builder *next_parent;
    };
}