#pragma once

#include "../ManapiUtils.hpp"
#include "../std/ManapiContext.hpp"
#include "../std/ManapiCancelToken.hpp"

namespace manapi::hash {

    class hash_base {
    public:
        virtual ~hash_base() = default;

        virtual void update (const uint8_t *message, std::size_t len) = 0;

        virtual void final(uint8_t *digest) = 0;

        MANAPIHTTP_NODISCARD virtual std::size_t final_size () const = 0;
    };

    future<manapi::status> hash_file (hash::hash_base *inst, manapi::ev::file src, int64_t offset = -1, manapi::ctoken token = nullptr);
}