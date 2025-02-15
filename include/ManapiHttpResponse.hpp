#pragma once

#include <string>
#include <map>

#include "ManapiAsync.hpp"
#include "ManapiJson.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpConfig.hpp"
#include "components/FormData.hpp"

#include "http/Utils.hpp"

namespace manapi::net {
    struct custom_data_t {
        void *src = nullptr;
        std::function <void(void *)> clean;
    };

    class http_response {
    public:
        http_response (manapi::net::http::request_data_t &request_data, const size_t &_status, std::string message, http::config &config);
        ~http_response ();

        void set_compress (const  std::string &name);
        void set_compress_enabled (const bool &status);

        void text (std::string plain_text);
        void json (const manapi::json &data, const size_t &spaces = 0);
        void form (formdata_send formdata);
        void set_status (const size_t &_status_code, const std::string &_status_message);
        void set_status_code (const size_t &_status_code);
        void set_status_message (const std::string &_status_message);
        void set_replacers (std::map<std::string, std::string> _replacers);
        void set_partial_status (const bool &auto_partial_status);
        void file (std::string path);
        void proxy (std::string url);
        void proxy (std::string url, std::function<void(class fetch &)> cb);

        [[deprecated]]
        const std::string &get_http_version ();
        [[nodiscard]] const size_t &get_status_code () const;
        const std::string &get_status_message ();
        std::string &get_body ();

        const std::map<std::string, std::string> &ref_headers ();
        std::map<std::string, std::string> get_headers ();

        void set_header (const std::string &key, std::string value);
        void remove_header (const std::string &key);
        bool has_header (const std::string &key);
        const std::string &get_header (const std::string &key);

        [[nodiscard]] bool is_file () const;
        [[nodiscard]] bool is_text () const;
        [[nodiscard]] bool is_proxy () const;
        [[nodiscard]] bool is_no_data () const;

        [[nodiscard]] bool has_ranges () const;

        [[nodiscard]] bool get_partial_enabled () const;
        const std::string &get_file ();
        const std::string &get_data ();
        std::function<void(class manapi::net::fetch &)> get_proxy_setup_cb ();
        const std::string &get_compress ();

        std::vector <std::pair <ssize_t, ssize_t> > ranges;

        std::optional<std::map <std::string, std::string>> get_replacers ();

        void set_custom_data (struct custom_data_t data);
        void clear_custom_data ();
        struct custom_data_t &get_custom_data ();

        enum response_type {
            RESPONSE_PROXY,
            RESPONSE_TEXT,
            RESPONSE_NO_DATA,
            RESPONSE_FILE,
            RESPONSE_FORMDATA
        };
    private:
        // custom data for layers
        custom_data_t custom_data;

        size_t type;
        // detect the range header
        void detect_ranges    ();
        http::config &config;

        std::string data;

        size_t status_code;
        std::string status_message;

        std::string http_version;

        std::string compress;

        bool compress_enabled = false;
        bool partial_enabled = false;

        std::map<std::string, std::string> headers;
        manapi::net::http::request_data_t *request_data;

        std::optional<std::map<std::string, std::string>> replacers;
        std::optional<std::function<void(class manapi::net::fetch &)>> proxy_setup{};
        std::optional<formdata_send> formdata;
    };
}
