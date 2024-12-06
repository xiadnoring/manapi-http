#ifndef MANAPIHTTP_MANAPIHTTPREQUEST_H
#define MANAPIHTTP_MANAPIHTTPREQUEST_H

#include <netinet/in.h>
#include <map>
#include <string>
#include <functional>
#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"
#include "components/FormData.hpp"

namespace manapi::net::http {
    class base;
}

namespace manapi::net {

    class http_request {
    public:
        http_request(const manapi::net::utils::manapi_socket_information &ip_data, manapi::net::request_data_t &request_data, class manapi::net::http::base *http_task, std::shared_ptr<http::config>, const void *handler);
        ~http_request();

        [[nodiscard]] const utils::manapi_socket_information &get_ip_data () const;
        [[nodiscard]] const std::string &get_method () const;
        [[nodiscard]] const std::string &get_http_version() const;
        [[nodiscard]] const utils::MAP_STR_STR &get_headers () const;
        [[nodiscard]] const std::string &get_param (const std::string &param) const;
        [[nodiscard]] std::string dump() const;
        std::string text ();
        manapi::json json ();
        formdata_recv form ();
        const size_t &get_body_size ();
        void set_max_plain_body_size (const size_t &size);

        bool contains_header (const std::string &name);
        const std::string& get_header (const std::string &name);
        bool has_header (const std::string &name);

        const std::string& get_query_param (const std::string &name);

        [[nodiscard]] const std::unique_ptr<const manapi::json_mask> &get_post_mask () const;
        [[nodiscard]] const std::unique_ptr<const manapi::json_mask> &get_get_mask () const;

        void stop_propagation (const bool &stop_propagation = true);
        [[nodiscard]] const bool& get_propagation ();
    private:
        void _read_body (const std::function<void(const char *, const size_t &)> &handler);
        void parse_map_url_param ();
        // peer ip
        const utils::manapi_socket_information *ip_data;

        // body, headers, url and etc
        request_data_t *request_data;

        // parent
        http::base *http_task;

        // handler
        const void *page_handler;

        // server
        std::shared_ptr<http::config> config;

        // if peer sent larger by size then max_plain_body_size -> error
        size_t max_plain_body_size = 1000000;

        // url get params ?param1=xxx&param2=xxx
        std::unique_ptr<std::map <std::string, std::string>> map_url_params;


        bool is_propagation = true;
    };
}

#endif //MANAPIHTTP_MANAPIHTTPREQUEST_H