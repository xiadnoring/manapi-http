#pragma once

#include <map>
#include <string>
#include <functional>

#include "./ManapiFormData.hpp"
#include "./ManapiHttpUtils.hpp"
#include "./ManapiHttpConfig.hpp"
#include "./ManapiSite.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiUtils.hpp"
#include "../json/ManapiJson.hpp"
#include "../json/ManapiJsonMask.hpp"
#include "../worker/ManapiBaseWorker.hpp"

namespace manapi::net::http {
    class request {
    public:
        using onrecv_sync_cb = std::move_only_function<ssize_t(const char *buffer, ssize_t size, bool fin)>;
        using onrecv_async_cb = std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)>;

        request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::shared_conn *conn, worker::shared_worker worker);

        ~request();

        manapi::async::cancellation_action cancellation ();

        MANAPIHTTP_NODISCARD const http::manapi_socket_information &ip_data () const;

        MANAPIHTTP_NODISCARD const std::string &method () const;

        MANAPIHTTP_NODISCARD int http() const;

        MANAPIHTTP_NODISCARD const std::map<std::string, std::string, std::less<>> &ref_headers () const;

        MANAPIHTTP_NODISCARD std::map<std::string, std::string, std::less<>> headers () const;

        MANAPIHTTP_NODISCARD manapi::error::status_or<std::string_view> param (std::string_view param) const MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<std::pair<std::string, std::string>> param_extract (std::string_view param) MANAPIHTTP_NOEXCEPT;

        future<manapi::error::status_or<std::string>> text ();

        future<manapi::json_error::status_or<manapi::json>> json (const manapi::json_mask *mask = nullptr);

        future<manapi::error::status> form (formdata_recv::onparam_cb_t cb);

        future<manapi::error::status> callback_sync (onrecv_sync_cb callback);

        future<manapi::error::status> callback_async (onrecv_async_cb callback);

        future<manapi::error::status> file (std::string filepath);

        ssize_t left ();

        manapi::json_error::status verify_get (const manapi::json_mask *mask) MANAPIHTTP_NOEXCEPT;

        manapi::json_error::status_or<std::string_view> get (std::string_view key);

        manapi::json_error::status_or<std::pair<std::string, std::string>> get_extract (std::string_view key);

        manapi::json_error::status contains_get_param (std::string_view key);

        void max_plain_body_size (size_t size);

        bool contains_header (std::string_view name);

        error::status_or<std::string_view> header (std::string_view name);

        error::status_or<std::pair<std::string, std::string>> header_extract (std::string_view name);

        void stop_propagation ();

        void propagation (bool state);

        manapi::future<manapi::error::status_or<std::map<std::string, std::string, std::less<>>>> trailers ();

        MANAPIHTTP_NODISCARD bool propagation () const;
    private:
        static future<manapi::error::status> read_body_ (worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_sync_cb handler);

        static future<manapi::error::status> read_async_body_ (worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_async_cb handler);

        std::unique_ptr<std::map<std::string, std::string, std::less<>>> get_params_;

        manapi::json_error::status prepare_get_params_(const manapi::json_mask *mask) MANAPIHTTP_NOEXCEPT;

        // peer ip
        std::unique_ptr<http::manapi_socket_information> ip_data_;

        // body, headers, url and etc
        http::request_data_t *request_data;

        manapi::net::worker::shared_conn * conn_;

        // server
        worker::shared_worker worker_;

        // if peer sent larger by size then max_plain_body_size -> error
        int max_plain_body_size_;
        int flags;
    };
}
