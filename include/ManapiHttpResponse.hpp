#pragma once

#include <string>
#include <map>

#include "ManapiUtils.hpp"
#include "ManapiAsync.hpp"
#include "ManapiJson.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpConfig.hpp"
#include "components/ManapiFormData.hpp"
#include "services/ManapiFetch.hpp"
#include "http/ManapiHttpUtils.hpp"

namespace manapi::net::http {
    namespace internal {
        struct handle_data_t;
    }

    struct custom_data_t {
        void *src = nullptr;
        std::move_only_function <void(void *)> clean;
    };

    struct custom_data_deleter_t {
        void operator()(custom_data_t *n);
    };

    class response {
    public:
        using resp_callback_sync = std::move_only_function<ssize_t(char *buffer, ssize_t size, bool&)>;
        using resp_callback_async = std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool&)>;
        using resp_stream_cb = std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool)>;
        using resp_stream = std::move_only_function<manapi::future<>(resp_stream_cb cb)>;
        using resp_proxy_setup_cb = std::move_only_function<void(class manapi::net::fetch &)>;

        response (internal::handle_data_t* cdata, int status, http::config *config, std::unique_ptr<http::request> req);

        ~response ();

        manapi::error::status compress (std::string name) MANAPIHTTP_NOEXCEPT;

        void compress_enabled (bool state) MANAPIHTTP_NOEXCEPT;

        manapi::error::status text (std::string plain_text) MANAPIHTTP_NOEXCEPT;

        manapi::error::status json (manapi::json data, size_t spaces = 0) MANAPIHTTP_NOEXCEPT;

        manapi::error::status form (formdata_send formdata) MANAPIHTTP_NOEXCEPT;

        void status (size_t status_code) MANAPIHTTP_NOEXCEPT;

        void status_code (size_t status_code) MANAPIHTTP_NOEXCEPT;

        manapi::error::status replacers (std::vector<std::pair<std::string, std::string>> replacers) MANAPIHTTP_NOEXCEPT;

        void partial_enabled (bool state) MANAPIHTTP_NOEXCEPT;

        manapi::error::status file (std::string path) MANAPIHTTP_NOEXCEPT;

#ifdef MANAPIHTTP_FETCH_SUPPORT
        manapi::error::status proxy (std::string url) MANAPIHTTP_NOEXCEPT;

        manapi::error::status proxy (std::string url, resp_proxy_setup_cb cb) MANAPIHTTP_NOEXCEPT;
#endif

        manapi::error::status callback_sync (resp_callback_sync cb) MANAPIHTTP_NOEXCEPT;

        manapi::error::status callback_async (resp_callback_async cb) MANAPIHTTP_NOEXCEPT;

        manapi::error::status callback_stream (resp_stream cb) MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] int status_code () const MANAPIHTTP_NOEXCEPT;

        std::string_view status_message () MANAPIHTTP_NOEXCEPT;

        std::map<std::string, std::string, std::less<>> &headers () MANAPIHTTP_NOEXCEPT;

        manapi::error::status header (const std::string &key, std::string value) MANAPIHTTP_NOEXCEPT;

        void remove_header (std::string_view key) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool contains_header (std::string_view key) const MANAPIHTTP_NOEXCEPT;

        error::status_or<std::string_view> header (std::string_view key) MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_file () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_text () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_proxy () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_no_data () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_formdata () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_async_cb() const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_sync_cb() const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool has_ranges () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool partial_enabled () const MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] int data_type() const MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<std::string *> file () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<std::string *> text () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<std::string *> url () MANAPIHTTP_NOEXCEPT;

        std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> ranges () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<formdata_send *> formdata () MANAPIHTTP_NOEXCEPT;
#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<resp_proxy_setup_cb> &proxy_setup_cb () MANAPIHTTP_NOEXCEPT;
#endif
        std::string compress () MANAPIHTTP_NOEXCEPT;

        std::unique_ptr<std::vector <std::pair<std::string, std::string>>> &replacers () MANAPIHTTP_NOEXCEPT;

        manapi::error::status custom_data (custom_data_t data) MANAPIHTTP_NOEXCEPT;

        http::config *config () MANAPIHTTP_NOEXCEPT;

        struct custom_data_t *custom_data () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<resp_callback_async*> callback_async() MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<resp_callback_sync*> callback_sync() MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<resp_stream *> callback_stream() MANAPIHTTP_NOEXCEPT;

        request_data_t *request_data () MANAPIHTTP_NOEXCEPT;

        http::request *req () MANAPIHTTP_NOEXCEPT;

        internal::handle_data_t *connection_data () MANAPIHTTP_NOEXCEPT;

        internal::handle_data_t *connection_data_release () MANAPIHTTP_NOEXCEPT;

        manapi::error::status finish (std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb) MANAPIHTTP_NOEXCEPT;

        void finish () MANAPIHTTP_NOEXCEPT;
    private:
        manapi::error::status check_type_ (int type) MANAPIHTTP_NOEXCEPT;

        std::string &body () MANAPIHTTP_NOEXCEPT;

        // detect the range header
        void detect_ranges () MANAPIHTTP_NOEXCEPT;

        http::config *config_;

        uint8_t type_;

        int status_code_;

        uint8_t flags;

        std::unique_ptr<std::string> compress_;

        std::map<std::string, std::string, std::less<>> headers_;

        std::unique_ptr<std::vector <std::pair <ssize_t, ssize_t> > > ranges_;

        // custom data for layers
        std::unique_ptr<custom_data_t, custom_data_deleter_t> custom_data_;

        std::unique_ptr<std::vector<std::pair<std::string, std::string>>> replacers_;

        std::unique_ptr<http::request> req_;

        internal::handle_data_t *cdata_;

        std::unique_ptr<std::move_only_function<void(std::exception_ptr err)>> finish_cb;

#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<std::move_only_function<void(class manapi::net::fetch &)>> proxy_setup;
#endif

        void *data_;
    };
}
