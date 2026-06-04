#pragma once

#include <functional>
#include <memory>

#include "../ManapiUtils.hpp"
#include "./ManapiJson.hpp"
#include "./ManapiJsonMask.hpp"
#include "../ManapiEventStructures.hpp"

namespace manapi {
    class json_builder {
    public:
        struct data_t;

        json_builder();

        json_builder(json_mask &mask);

        ~json_builder();

        json_builder &operator<< (std::string_view str);

        json_builder &operator<< (char c);

        json_error::status parse (std::string_view str);

        json_error::status parse (char c);

        manapi::json_error::status_or<json> get ();

        MANAPIHTTP_NODISCARD bool is_ready () const;

        MANAPIHTTP_NODISCARD bool is_empty () const;

        void set (json_mask &mask);

        void clear ();
    private:
        std::unique_ptr<data_t> m_data;
    };
}