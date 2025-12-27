#pragma once

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiEventStructures.hpp"
#include "./ManapiJson.hpp"

namespace manapi {
    namespace json_error {
        class status : public manapi::status {
        public:
            status ();

            ~status () override;

            status (const status &err);

            status (err_num code, std::string_view msg, std::size_t pos, std::string path);

            status (err_num code, std::string_view msg, std::string data, std::size_t pos, std::string path);

            status (json_error::status &&n) MANAPIHTTP_NOEXCEPT;

            status &operator=(json_error::status &&n) MANAPIHTTP_NOEXCEPT;

            status (manapi::status &&n) MANAPIHTTP_NOEXCEPT;

            status &operator=(manapi::status &&n) MANAPIHTTP_NOEXCEPT;

            void log () const override;

            void unwrap() const override;

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

        MANAPIHTTP_NODISCARD bool is_enabled () const;

        void set_enabled (bool status);

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const json &obj) const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const std::map <std::string, std::string> &obj) const;

        MANAPIHTTP_NODISCARD manapi::json_error::status valid (const std::map <std::string, std::string, std::less<>> &obj) const;

        MANAPIHTTP_NODISCARD const json &get_api_tree () const;

        void set_api_tree (json tree);

        static json OR (json data, bool none = false);

        static json ARRAY (json data, ssize_t min, ssize_t max, bool none = false);

        static json ARRAY (json data, bool none = false);

        void set_complete_status (bool complete);
    private:
        bool enabled;

        bool complete = true;

        json information;

        static void set_status_prepared_ (json &data);

        static void insert_meta_row_ (json &information, const std::string &key, const json &value);

        static void initial_resolve_information (json &obj);

        MANAPIHTTP_NODISCARD manapi::json_error::status recursive_valid (const json &obj, const json &information, bool is_complex, std::vector<ev::buff_t> *path) const;
    };
}