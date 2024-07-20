#ifndef MANAPIHTTP_MANAPIHTTPREQUEST_H
#define MANAPIHTTP_MANAPIHTTPREQUEST_H

#include <netinet/in.h>
#include <map>
#include <string>
#include <functional>
#include "ManapiUtils.h"
#include "ManapiJson.h"

namespace manapi::net {
    class http;

    struct ip_data_t {
        char    *ip;
        int     family;
    };

    struct file_data_t {
        bool        exists      = false;
        std::string file_name;
        std::string mime_type;
        std::string param_name;
    };

    class http_request {
    public:
        http_request(manapi::net::ip_data_t &_ip_data, manapi::net::request_data_t &_request_data, void* http_task, http *http_server);

        ~http_request();
        const ip_data_t                             &get_ip_data ();
        const std::string                           &get_method ();
        const std::string                           &get_http_version();
        const std::map<std::string, std::string>    &get_headers ();
        const std::string                           &get_param (const std::string &param);
        std::string                                 text ();
        manapi::utils::json                         json ();
        manapi::utils::MAP_STR_STR                  form ();
        const size_t                                &get_body_size ();
        void                                        set_max_plain_body_size (const size_t &size);
        const file_data_t                           &inf_file ();
        bool                                        has_file () const;
        void                                        set_file (const std::function<void(const char *, const size_t &)> &handler);
        std::string                                 set_file_to_str ();
        void                                        set_file_to_local (const std::string &filepath);

        bool                                        contains_header (const std::string &name);
        const std::string&                          get_header      (const std::string &name);
    private:
        static void                                 buff_to_extra_buff (const request_data_t *req_data, const size_t &start, const size_t &end, char *dest, size_t &size);
        static void                                 chars2string (std::string &str, const char *ptr, const size_t &size);
        void                                        multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line = nullptr, const std::function<void(const std::string &)> &send_name = nullptr);

        ip_data_t       *ip_data;
        request_data_t  *request_data;

        void            *http_task;
        http            *http_server;

        std::string     body_boundary;

        size_t          max_plain_body_size = 300;

        // the data of the next file
        file_data_t     file_data;

        bool            cut_header = true;
        bool            first_line = true;
    };
}

#endif //MANAPIHTTP_MANAPIHTTPREQUEST_H