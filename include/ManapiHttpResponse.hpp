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
        std::move_only_function <void(void *)> clean;
    };

    struct custom_data_deleter_t {
        void operator()(custom_data_t *n) {
             if (n && n->clean)
                 n->clean(n->src);
        }
    };

    class response {
    public:
        typedef std::move_only_function<ssize_t(char *buffer, ssize_t size, bool&)> resp_callback_sync;
        typedef std::move_only_function<manapi::future<ssize_t>(char *buffer, ssize_t size, bool&)> resp_callback_async;
        typedef std::move_only_function<void(class manapi::net::fetch &)> resp_proxy_setup_cb;

        response (manapi::net::http::request_data_t *request_data, int status, http::config *config);

        ~response ();

        void compress (std::string name);

        void compress_enabled (bool state);

        void text (std::string plain_text);

        void json (manapi::json data, const size_t &spaces = 0);

        void form (formdata_send formdata);

        void status (const size_t &status_code);


        void status_code (const size_t &status_code);

        void replacers (std::map<std::string, std::string> replacers);

        void partial_enabled (bool state);

        void file (std::string path);

#ifdef MANAPIHTTP_FETCH_SUPPORT
        void proxy (std::string url);

        void proxy (std::string url, std::move_only_function<void(class fetch &)> cb);
#endif

        void callback_sync (std::move_only_function<ssize_t(char *, ssize_t, bool&)> cb);

        void callback_async (std::move_only_function<manapi::future<ssize_t>(char *, ssize_t, bool&)> cb);

        [[nodiscard]] int status_code () const;

        std::string_view status_message ();

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

        [[nodiscard]] bool is_async_cb() const;

        [[nodiscard]] bool is_sync_cb() const;

        [[nodiscard]] bool has_ranges () const;

        [[nodiscard]] bool partial_enabled () const;

        [[nodiscard]] int data_type() const;

        std::string file ();

        std::string text ();

        std::string url ();

        std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> ranges ();

        std::unique_ptr<formdata_send> formdata ();
#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<resp_proxy_setup_cb> proxy_setup_cb ();
#endif
        std::string compress ();

        std::unique_ptr<std::map <std::string, std::string>> replacers ();

        void custom_data (custom_data_t data);

        http::config *config ();

        struct custom_data_t *custom_data ();

        std::unique_ptr<resp_callback_async> callback_async();

        std::unique_ptr<resp_callback_sync> callback_sync();

        request_data_t *request_data ();
    private:
        void check_type_ (int type);

        std::string body ();

        // detect the range header
        void detect_ranges ();

        http::config *config_;

        int type_;

        int status_code_;

        int flags;

        std::unique_ptr<std::string> compress_;

        std::map<std::string, std::string> headers_;

        std::unique_ptr<std::vector <std::pair <ssize_t, ssize_t> > > ranges_;

        // custom data for layers
        std::unique_ptr<custom_data_t, custom_data_deleter_t> custom_data_;

        manapi::net::http::request_data_t * request_data_;

        std::unique_ptr<std::map<std::string, std::string>> replacers_;

#ifdef MANAPIHTTP_FETCH_SUPPORT
        std::unique_ptr<std::move_only_function<void(class manapi::net::fetch &)>> proxy_setup;
#endif

        void *data_;
    };
}
