#ifndef MANAPIHTTP_MANAPIRESPONSE_H
#define MANAPIHTTP_MANAPIRESPONSE_H

#include <string>
#include <map>
#include "ManapiUtils.h"
#include "ManapiHttp.h"

namespace manapi::net {
    class http;

    class http_response {
    public:
        http_response(manapi::net::request_data_t &_request_data, const size_t &_status, std::string _message, http *_http_server);
        ~http_response();

        void set_compress           (const  std::string &name);
        void set_compress_enabled   (bool   status);

        void text                   (const std::string &plain_text);
        void set_status             (const size_t &_status_code, const std::string &_status_message);
        void set_status_code        (const size_t &_status_code);
        void set_status_message     (const std::string &_status_message);
        void file                   (const std::string &path, bool auto_partial_status = false);

        [[deprecated]]
        const std::string           &get_http_version   ();
        [[nodiscard]] const size_t  &get_status_code    () const;
        const std::string           &get_status_message ();
        const std::string           &get_body           ();

        const std::map<std::string, std::string> &get_headers ();

        void set_header         (const std::string &key, const std::string &value);
        std::string *get_header (const std::string &key);

        bool                is_sendfile     ();
        [[nodiscard]] bool  get_auto_partial_enabled () const;
        std::string         &get_sendfile   ();

        const std::string   &get_compress   ();

        std::vector <std::pair <ssize_t, ssize_t> > ranges;
    private:
        // detect the range header
        void                detect_ranges    ();
        http                *http_server;

        std::string         body;

        size_t              status_code;
        std::string         status_message;

        std::string         http_version;

        std::string         sendfile;

        std::string         compress;

        bool                compress_enabled        = true;
        bool                auto_partial_enabled    = true;

        manapi::toolbox::MAP_STR_STR    headers;
        manapi::net::request_data_t     *request_data;
    };
}

#endif //MANAPIHTTP_MANAPIRESPONSE_H
