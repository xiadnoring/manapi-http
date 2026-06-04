#pragma once

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiEventStructures.hpp"
#include "./ManapiJson.hpp"

namespace manapi {
    class json_mask_object_t;

    struct json_mask_path_t;

    namespace json_error {
        class status : public manapi::status {
        public:
            status ();

            ~status () override;

            status (const status &err);

            status (err_num code, std::string_view msg, std::size_t pos, std::string path);

            status (err_num code, std::string_view msg, std::string data, std::size_t pos, std::string path);

            status (err_num code, std::string msg, std::string data, std::size_t pos, std::string path);

            status (err_num code, const char * msg, std::string data, std::size_t pos, std::string path);

            status (json_error::status &&n) MANAPIHTTP_NOEXCEPT;

            status &operator=(json_error::status &&n) MANAPIHTTP_NOEXCEPT;

            status (manapi::status &&n) MANAPIHTTP_NOEXCEPT;

            status &operator=(manapi::status &&n) MANAPIHTTP_NOEXCEPT;

            std::string fullmsg() const override;

            std::string path ();

            std::string additional_data ();

            MANAPIHTTP_NODISCARD std::size_t pos () const;
        private:
            std::string m_data;
            std::string m_path;
            std::size_t m_pos;
        };

        template<typename T, typename E = manapi::json_error::status>
        class status_or : public manapi::status_or<T, E> {
        public:
            status_or (json_error::status n) : manapi::status_or<T, E>(std::move(n)) {}

            status_or (T &&n) : manapi::status_or<T, E>(std::forward<decltype(n)>(n)) {}

            status_or(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

            status_or&operator=(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

            std::string path () {
                return this->err_.path();
            }

            std::string additional_data () {
                return this->err_.additional_data();
            }

            MANAPIHTTP_NODISCARD std::size_t pos () const {
                return this->err_.pos();
            }
        };

        json_error::status status_invalid_argument (std::string_view msg, std::size_t pos, std::string path);

        json_error::status status_invalid_argument (std::string_view msg, std::string data, std::size_t pos, std::string path);

        json_error::status status_ok ();
    }

    class json_mask {
    public:
        json_mask(const std::initializer_list<json> &data);

        json_mask(json data);

        json_mask(const std::nullptr_t &n = nullptr);

        json_mask(const json_mask &n);

        json_mask(json_mask &&n) MANAPIHTTP_NOEXCEPT;

        ~json_mask();

        MANAPIHTTP_NODISCARD bool enabled () const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const json &obj) const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const json &obj, manapi::json_mask_path_t *orig, uint32_t orig_len, const json_mask_object_t **out) const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const std::map <std::string, std::string> &obj) const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const std::map <std::string, std::string, std::less<>> &obj) const;

        MANAPIHTTP_NODISCARD const json_mask_object_t* api_tree () const;

        void api_tree (const json_mask_object_t* tree, bool own);

        static json Or (json data, bool none = false);

        static json Array (json data, ssize_t min, ssize_t max, bool none = false);

        static json Array (json data, bool none = false);

        void complete_status (bool state);
    private:
        int m_flags;

        const json_mask_object_t *m_data;
    };
}
