#pragma once

#include <string>
#include <map>

#include "./ManapiHttpConfig.hpp"
#include "./ManapiFormData.hpp"
#include "./ManapiHttpUtils.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiFetch.hpp"
#include "../json/ManapiJson.hpp"

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
        friend class uresponse;
    public:
        using resp_callback_sync = std::move_only_function<ssize_t(char *buffer, std::size_t size, bool&)>;
        using resp_callback_async = std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool&)>;
        using resp_stream_cb = std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool)>;
        using resp_stream = std::move_only_function<manapi::future<>(resp_stream_cb cb)>;
#ifdef MANAPIHTTP_FETCH_SUPPORT
        using resp_proxy_setup_cb = std::move_only_function<void(const std::shared_ptr<manapi::net::fetch> &)>;
#endif

        response (internal::handle_data_t* cdata, uint16_t status, http::config *config, std::unique_ptr<http::request> req);

        ~response ();

        manapi::status compress (std::string name) MANAPIHTTP_NOEXCEPT;

        void compress_enabled (bool state) MANAPIHTTP_NOEXCEPT;

        manapi::status text (std::string plain_text) MANAPIHTTP_NOEXCEPT;

        manapi::status json (manapi::json data, size_t spaces = 0) MANAPIHTTP_NOEXCEPT;

        manapi::status form (formdata_send formdata) MANAPIHTTP_NOEXCEPT;

        void status (uint16_t status_code) MANAPIHTTP_NOEXCEPT;

        void status_code (uint16_t status_code) MANAPIHTTP_NOEXCEPT;

        manapi::status replacers (std::vector<std::pair<std::string, std::string>> replacers) MANAPIHTTP_NOEXCEPT;

        void partial_enabled (bool state) MANAPIHTTP_NOEXCEPT;

        manapi::status file (std::string path) MANAPIHTTP_NOEXCEPT;

#ifdef MANAPIHTTP_FETCH_SUPPORT
        manapi::status proxy (std::string url) MANAPIHTTP_NOEXCEPT;

        manapi::status proxy (std::string url, resp_proxy_setup_cb cb) MANAPIHTTP_NOEXCEPT;
#endif

        manapi::status callback_sync (resp_callback_sync cb) MANAPIHTTP_NOEXCEPT;

        manapi::status callback_async (resp_callback_async cb) MANAPIHTTP_NOEXCEPT;

        manapi::status callback_stream (resp_stream cb) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD uint16_t status_code () const MANAPIHTTP_NOEXCEPT;

        std::string_view status_message () MANAPIHTTP_NOEXCEPT;

        std::map<std::string, std::string, std::less<>> &headers () MANAPIHTTP_NOEXCEPT;

        manapi::status header (const std::string &key, std::string value) MANAPIHTTP_NOEXCEPT;

        void remove_header (std::string_view key) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool contains_header (std::string_view key) const MANAPIHTTP_NOEXCEPT;

        status_or<std::string_view> header (std::string_view key) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_file () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_text () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_proxy () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_no_data () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_formdata () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_async_cb() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_sync_cb() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool has_ranges () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool partial_enabled () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD int data_type() const MANAPIHTTP_NOEXCEPT;

        manapi::status_or<std::string *> file () MANAPIHTTP_NOEXCEPT;

        manapi::status_or<std::string *> text () MANAPIHTTP_NOEXCEPT;

        manapi::status_or<std::string *> url () MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool contains_ranges () const MANAPIHTTP_NOEXCEPT;

        std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> ranges () MANAPIHTTP_NOEXCEPT;

        manapi::status_or<formdata_send *> formdata () MANAPIHTTP_NOEXCEPT;
#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<resp_proxy_setup_cb> &proxy_setup_cb () MANAPIHTTP_NOEXCEPT;
#endif
        std::string compress () MANAPIHTTP_NOEXCEPT;

        std::unique_ptr<std::vector <std::pair<std::string, std::string>>> &replacers () MANAPIHTTP_NOEXCEPT;

        manapi::status custom_data (custom_data_t data) MANAPIHTTP_NOEXCEPT;

        http::config *config () MANAPIHTTP_NOEXCEPT;

        struct custom_data_t *custom_data () MANAPIHTTP_NOEXCEPT;

        manapi::status_or<resp_callback_async*> callback_async() MANAPIHTTP_NOEXCEPT;

        manapi::status_or<resp_callback_sync*> callback_sync() MANAPIHTTP_NOEXCEPT;

        manapi::status_or<resp_stream *> callback_stream() MANAPIHTTP_NOEXCEPT;

        request_data_t *request_data () MANAPIHTTP_NOEXCEPT;

        http::request *req () MANAPIHTTP_NOEXCEPT;

        internal::handle_data_t *connection_data () MANAPIHTTP_NOEXCEPT;

        internal::handle_data_t *connection_data_release () MANAPIHTTP_NOEXCEPT;

        template<typename T>
        T *custom_data_as () MANAPIHTTP_NOEXCEPT {
            return static_cast<T*>(this->custom_data()->src);
        }
    private:
        void finish () MANAPIHTTP_NOEXCEPT;

        manapi::status finish (std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb) MANAPIHTTP_NOEXCEPT;

        manapi::status check_type_ (int type) MANAPIHTTP_NOEXCEPT;

        std::string &body () MANAPIHTTP_NOEXCEPT;

        // detect the range header
        void detect_ranges () MANAPIHTTP_NOEXCEPT;

        http::config *m_config;

        uint8_t m_type;

        uint16_t m_status_code;

        uint8_t m_flags;

        std::unique_ptr<std::string> m_compress;

        std::map<std::string, std::string, std::less<>> m_headers;

        std::unique_ptr<std::vector <std::pair <ssize_t, ssize_t> > > m_ranges;

        // custom data for layers
        std::unique_ptr<custom_data_t, custom_data_deleter_t> m_custom_data;

        std::unique_ptr<std::vector<std::pair<std::string, std::string>>> m_replacers;

        std::unique_ptr<http::request> m_req;

        internal::handle_data_t *m_cdata;

        std::unique_ptr<std::move_only_function<void(std::exception_ptr err)>> m_finish_cb;

#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<std::move_only_function<void(const std::shared_ptr<manapi::net::fetch> &)>> m_proxy_setup;
#endif

        void *m_data;
    };

    class uresponse {
    public:
        uresponse (response *resp, std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb);

        ~uresponse();

        uresponse (uresponse &&n) MANAPIHTTP_NOEXCEPT;

        uresponse &operator=(uresponse &&n) MANAPIHTTP_NOEXCEPT;

        response *operator->() MANAPIHTTP_NOEXCEPT;

        const response *operator->() const MANAPIHTTP_NOEXCEPT;

        response &operator*() MANAPIHTTP_NOEXCEPT;

        const response &operator *() const MANAPIHTTP_NOEXCEPT;

        void finish () MANAPIHTTP_NOEXCEPT;
    private:
        response *m_resp;
    };

}
