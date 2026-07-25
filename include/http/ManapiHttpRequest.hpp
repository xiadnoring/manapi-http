#pragma once

#include <map>
#include <string>
#include <functional>

#include "./ManapiFormData.hpp"
#include "./ManapiHttpUtils.hpp"
#include "./ManapiHttpConfig.hpp"
#include "../worker/ManapiSite.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiUtils.hpp"
#include "../json/ManapiJson.hpp"
#include "../json/ManapiJsonMask.hpp"
#include "../worker/ManapiBaseWorker.hpp"

namespace manapi::net::http {
    class request {
    public:
        using onrecv_sync_cb = std::move_only_function<ssize_t(const char *buffer, std::size_t size, bool fin)>;
        using onrecv_async_cb = std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)>;

        request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::shared_conn *conn, worker::shared_worker worker);

        ~request();

        manapi::ctoken cancellation ();

        MANAPIHTTP_NODISCARD const http::manapi_socket_information &ip_data () const;

        MANAPIHTTP_NODISCARD const std::string &method () const;

        MANAPIHTTP_NODISCARD int http() const;

        MANAPIHTTP_NODISCARD const std::map<std::string, std::string, std::less<>> &ref_headers () const;

        MANAPIHTTP_NODISCARD std::map<std::string, std::string, std::less<>> headers () const;

        MANAPIHTTP_NODISCARD manapi::status_or<std::string_view> param (std::string_view param) const MANAPIHTTP_NOEXCEPT;

        manapi::status_or<std::pair<std::string, std::string>> param_extract (std::string_view param) MANAPIHTTP_NOEXCEPT;

        future<manapi::status_or<std::string>> text ();

        future<manapi::status_or<manapi::slice>> slice ();

        future<manapi::json_error::status_or<manapi::json>> json (uint32_t flags, manapi::json_mask *mask = nullptr);

        future<manapi::json_error::status_or<manapi::json>> json (manapi::json_mask *mask = nullptr);

        future<manapi::status> form (formdata_recv::onparam_cb_t cb);

        future<manapi::status> callback_sync (onrecv_sync_cb callback);

        future<manapi::status> callback_async (onrecv_async_cb callback);

        future<manapi::status> file (std::string filepath, int flags = 0644);

        int64_t left ();

        manapi::json_error::status verify_get (const manapi::json_mask *mask) MANAPIHTTP_NOEXCEPT;

        manapi::json_error::status_or<std::string_view> get (std::string_view key);

        manapi::json_error::status_or<std::pair<std::string, std::string>> get_extract (std::string_view key);

        manapi::json_error::status contains_get_param (std::string_view key);

        manapi::status available_body_size (int64_t size);

        bool contains_header (std::string_view name);

        status_or<std::string_view> header (std::string_view name);

        status_or<std::pair<std::string, std::string>> header_extract (std::string_view name);

        void stop_propagation ();

        void propagation (bool state);

        manapi::future<manapi::status_or<std::map<std::string, std::string, std::less<>>>> trailers ();

        MANAPIHTTP_NODISCARD bool propagation () const;

        MANAPIHTTP_NODISCARD std::string_view url () const;

        MANAPIHTTP_NODISCARD const std::vector<std::string> &path () const;

        MANAPIHTTP_NODISCARD bool has_body () const;
    private:
        std::unique_ptr<std::map<std::string, std::string, std::less<>>> m_get_params;

        // peer ip
        std::unique_ptr<http::manapi_socket_information> m_ip_data;

        // body, headers, url and etc
        http::request_data_t *m_request_data;

        manapi::net::worker::shared_conn * m_conn;

        // server
        worker::shared_worker m_worker;

        // m_flags
        int m_flags;
    };
}
