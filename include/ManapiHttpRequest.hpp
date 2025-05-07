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
#include "components/FormData.hpp"
#include "http/Utils.hpp"
#include "ManapiSite.hpp"
#include "worker/base_worker.hpp"

namespace manapi::net::http {
    class request {
    public:
        request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::connection *conn, worker::shared_worker worker, const http_handler_functions *handler);
        ~request();

        [[nodiscard]] const http::manapi_socket_information &ip_data () const;
        [[nodiscard]] const std::string &method () const;
        [[nodiscard]] int http_version() const;
        [[nodiscard]] const std::map<std::string, std::string> &ref_headers () const;
        [[nodiscard]] std::map<std::string, std::string> headers () const;
        [[nodiscard]] const std::string &param (const std::string &param) const;
        [[nodiscard]] std::string dump() const;
        future<std::string> text ();
        future<manapi::json> json ();
        future<formdata_recv> form ();
        future<void> callback_sync (std::move_only_function<ssize_t(const char *buffer, ssize_t size)> callback);
        future<void> callback_async (std::move_only_function<manapi::future<ssize_t>(const char *buffer, ssize_t size)> callback);
        future<void> file (std::string filepath);
        ssize_t body_size ();
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
        future<void> _read_body (std::move_only_function<ssize_t(const char *, ssize_t )> handler);
        future<void> _read_async_body (std::move_only_function<manapi::future<ssize_t>(const char *, ssize_t )> handler);

        std::unique_ptr<std::map<std::string, std::string>> get_params_;

        void prepare_get_params_();

        // peer ip
        std::unique_ptr<http::manapi_socket_information> ip_data_;

        // body, headers, url and etc
        http::request_data_t *request_data;

        manapi::net::worker::connection * conn_;

        // server
        worker::shared_worker worker_;

        const http_handler_functions *handler_;

        // if peer sent larger by size then max_plain_body_size -> error
        int max_plain_body_size_;
        int flags;
    };
}
