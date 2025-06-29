#pragma once

#include <map>
#include <string>
#include <functional>

#include "ManapiUtils.hpp"
#include "ManapiAsync.hpp"
#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"
#include "components/ManapiFormData.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "ManapiSite.hpp"
#include "worker/ManapiBaseWorker.hpp"

namespace manapi::net::http {
    class request {
    public:
        using onrecv_sync_cb = std::move_only_function<ssize_t(const char *buffer, ssize_t size, bool fin)>;
        using onrecv_async_cb = std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)>;

        request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::shared_conn *conn, worker::shared_worker worker, const http_handler_function *handler);

        ~request();

        manapi::async::cancellation_action cancellation ();

        [[nodiscard]] const http::manapi_socket_information &ip_data () const;

        [[nodiscard]] const std::string &method () const;

        [[nodiscard]] int http_version() const;

        [[nodiscard]] const std::map<std::string, std::string> &ref_headers () const;

        [[nodiscard]] std::map<std::string, std::string> headers () const;

        [[nodiscard]] const std::string &param (const std::string &param) const;

        [[nodiscard]] std::string dump() const;

        future<std::string> text ();

        future<manapi::json> json ();

        future<> form (formdata_recv::onparam_cb_t cb);

        future<void> callback_sync (onrecv_sync_cb callback);

        future<void> callback_async (onrecv_async_cb callback);

        future<void> file (std::string filepath);

        ssize_t left ();

        const std::string &get (const std::string &key);

        bool contains_get_param (const std::string &key);

        void max_plain_body_size (const size_t &size);

        bool contains_header (const std::string &name);

        const std::string& header (const std::string &name);

        [[nodiscard]] const std::unique_ptr<const manapi::json_mask> &post_mask () const;

        [[nodiscard]] const std::unique_ptr<const manapi::json_mask> &get_mask () const;

        void stop_propagation ();

        void propagation (bool state);

        [[nodiscard]] bool propagation () const;
    private:
        static future<void> read_body_ (worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_sync_cb handler);
        static future<void> read_async_body_ (worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_async_cb handler);

        std::unique_ptr<std::map<std::string, std::string>> get_params_;

        void prepare_get_params_();

        // peer ip
        std::unique_ptr<http::manapi_socket_information> ip_data_;

        // body, headers, url and etc
        http::request_data_t *request_data;

        manapi::net::worker::shared_conn * conn_;

        // server
        worker::shared_worker worker_;

        const http_handler_function *handler_;

        // if peer sent larger by size then max_plain_body_size -> error
        int max_plain_body_size_;
        int flags;
    };
}
