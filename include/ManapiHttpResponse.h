#ifndef MANAPIHTTP_MANAPIRESPONSE_H
#define MANAPIHTTP_MANAPIRESPONSE_H

#include <string>
#include <map>
#include "ManapiJson.h"
#include "ManapiUtils.h"
#include "ManapiHttp.h"
#include "ManapiApi.h"

namespace manapi::net {
    class http;

    class http_response {
    public:
        http_response               (manapi::net::request_data_t &_request_data, const size_t &_status, std::string _message, http *_http_server);
        ~http_response              ();

        void set_compress           (const  std::string &name);
        void set_compress_enabled   (bool   status);

        void text                   (const std::string &plain_text);
        void json                   (manapi::utils::json &jp, const size_t &spaces = 0);
        void set_status             (const size_t &_status_code, const std::string &_status_message);
        void set_status_code        (const size_t &_status_code);
        void set_status_message     (const std::string &_status_message);
        void set_replacers          (const utils::MAP_STR_STR &_replacers);
        void set_partial_status     (bool auto_partial_status);
        void file                   (const std::string &path);
        void proxy                  (const std::string &url);

        [[deprecated]]
        const std::string               &get_http_version   ();
        [[nodiscard]] const size_t      &get_status_code    () const;
        const std::string               &get_status_message ();
        const std::string               &get_body           ();

        const std::map<std::string, std::string> &get_headers ();

        void                            set_header      (const std::string &key, const std::string &value);
        std::string                     &get_header     (const std::string &key);

        [[nodiscard]] bool              is_file     () const;
        [[nodiscard]] bool              is_text     () const;
        [[nodiscard]] bool              is_proxy    () const;

        [[nodiscard]] bool              get_auto_partial_enabled () const;
        const std::string               &get_file   ();
        const std::string               &get_data   ();

        const std::string               &get_compress   ();

        std::vector <std::pair <ssize_t, ssize_t> > ranges;

        const utils::MAP_STR_STR        *get_replacers ();

        manapi::net::api::pool          *tasks;
    private:
        size_t                          type;
        // detect the range header
        void                            detect_ranges    ();
        http                            *http_server;

        std::string                     data;

        size_t                          status_code;
        std::string                     status_message;

        std::string                     http_version;

        std::string                     compress;

        bool                            compress_enabled        = true;
        bool                            auto_partial_enabled    = true;

        manapi::utils::MAP_STR_STR      headers;
        manapi::net::request_data_t     *request_data;

        manapi::utils::MAP_STR_STR      *replacers = nullptr;
    };
}

#endif //MANAPIHTTP_MANAPIRESPONSE_H
