#pragma once

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"
#include "./ManapiJson.hpp"

namespace manapi {
    enum json_mask_object_flags {
        JSON_MASK_OBJECT_FLAG_NULL = 1<<0
    };

    enum json_mask_type_flags {
        JSON_MASK_TYPE_FLAG_S=1<<0,
        JSON_MASK_TYPE_FLAG_E=1<<1
    };

    class json_mask_object_t;

    class json_mask_type_t {
    public:
        int type;
        int flags;
        std::unique_ptr<json_mask_object_t> zdefault;
        manapi::json mean;
        manapi::json max_mean;
        manapi::json min_mean;
        const json_mask_object_t *parent;

        json_mask_type_t ();

        json_mask_type_t (const json_mask_type_t &n);

        json_mask_type_t (json_mask_type_t &&n) MANAPIHTTP_NOEXCEPT;

        json_mask_type_t &operator=(const json_mask_type_t &n);

        json_mask_type_t &operator=(json_mask_type_t &&n) MANAPIHTTP_NOEXCEPT;
    };

    struct json_dump_path_t {
        const manapi::json *p;
        manapi::json::OBJECT::const_iterator it;
        std::size_t i;
    };

    struct json_dump_data_t {
        uint32_t spaces;
        uint32_t shift;
        std::size_t sz;
        std::vector<json_dump_path_t> paths;
        int flags;
    };

    enum json_dump_data_flags {
        JSON_DUMP_FLAG_INIT = 1<<0,
        JSON_DUMP_FLAG_PUSHED = 1<<1,
        JSON_DUMP_FLAG_SHIFT = 1<<2,
        JSON_DUMP_FLAG_GAPS = 1<<3
    };

    class json_source {
    public:
        virtual ~json_source() = default;

        virtual json_source *copy () const = 0;

        virtual std::size_t dump_size (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) const;

        virtual void dump (manapi::json_dump_buffer *p, manapi::json_dump_data_t *data) const;
    };

    class json_dump_buffer {
    public:
        virtual ~json_dump_buffer() = default;

        virtual void push_back (const char *buffer, std::size_t sz) MANAPIHTTP_NOEXCEPT = 0;

        virtual void push_back (char c) MANAPIHTTP_NOEXCEPT = 0;
    };

    struct json_mask_path_t {
        manapi::json *p;
        manapi::json::OBJECT::iterator it;
        std::size_t indx;
        uint32_t type;
        uint8_t flags;
    };

    class json_mask_type_t;

    class json_mask_object_t : public manapi::json_source {
    public:
        uint32_t flags;
        std::vector<std::unique_ptr<json_mask_type_t>> types;
        const json_mask_type_t *parent;

        json_mask_object_t ();

        ~json_mask_object_t() override;

        json_mask_object_t (const json_mask_object_t &n);

        json_mask_object_t (json_mask_object_t &&n) MANAPIHTTP_NOEXCEPT;

        json_mask_object_t &operator=(const json_mask_object_t &n);

        json_mask_object_t &operator=(json_mask_object_t &&n) MANAPIHTTP_NOEXCEPT;

        json_source *copy() const override;
    };
}
