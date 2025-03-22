#pragma once

#include <string>
#include <map>

#include "ManapiUtils.hpp"
#include "ManapiAsync.hpp"
#include "ManapiJson.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpConfig.hpp"
#include "components/FormData.hpp"
#include "services/ManapiFetch.hpp"

#include "http/Utils.hpp"

namespace manapi::net::http {
    struct custom_data_t {
        void *src = nullptr;
        std::function <void(void *)> clean;
    };

    class response {
    public:
        enum response_type {
            RESPONSE_PROXY,
            RESPONSE_TEXT,
            RESPONSE_NO_DATA,
            RESPONSE_FILE,
            RESPONSE_FORMDATA
        };

        response (manapi::net::http::request_data_t &request_data, const size_t &_status, http::config &config);
        ~response ();

        void compress (const  std::string &name);
        void compress_enabled (const bool &status);

        void text (std::string plain_text);
        void json (manapi::json data, const size_t &spaces = 0);
        void form (formdata_send formdata);
        void status (const size_t &status_code);
        void status_code (const size_t &status_code);
        void replacers (std::map<std::string, std::string> replacers);
        void partial_status (const bool &auto_partial_status);
        void file (std::string path);
        void proxy (std::string url);
        void proxy (std::string url, std::function<void(class fetch &)> cb);

        [[deprecated]]
        const std::string &http_version ();
        [[nodiscard]] const size_t &status_code () const;
        std::string_view status_message ();
        std::string &body ();

        const std::map<std::string, std::string> &ref_headers ();
        std::map<std::string, std::string> headers ();

        void header (const std::string &key, std::string value);
        void remove_header (const std::string &key);
        bool has_header (const std::string &key);
        const std::string &header (const std::string &key);

        [[nodiscard]] bool is_file () const;
        [[nodiscard]] bool is_text () const;
        [[nodiscard]] bool is_proxy () const;
        [[nodiscard]] bool is_no_data () const;
        [[nodiscard]] bool is_formdata () const;

        [[nodiscard]] bool has_ranges () const;

        [[nodiscard]] bool partial_enabled () const;
        const std::string &file ();
        const std::string &data ();
        formdata_send formdata ();
        std::function<void(class manapi::net::fetch &)> proxy_setup_cb ();
        const std::string &compress ();

        std::vector <std::pair <ssize_t, ssize_t> > ranges_;

        std::optional<std::map <std::string, std::string>> replacers ();

        void custom_data (struct custom_data_t data);
        void clear_custom_data ();
        struct custom_data_t &custom_data ();
    private:
        // custom data for layers
        custom_data_t custom_data_;

        size_t type_;
        // detect the range header
        void detect_ranges ();
        http::config &config_;

        std::string data_;

        size_t status_code_;

        std::string http_version_;

        std::string compress_;

        bool compress_enabled_ = false;
        bool partial_enabled_ = false;

        std::map<std::string, std::string> headers_;
        manapi::net::http::request_data_t *request_data_;

        std::optional<std::map<std::string, std::string>> replacers_;
        std::optional<std::function<void(class manapi::net::fetch &)>> proxy_setup{};
        std::optional<formdata_send> formdata_;
    };
}
