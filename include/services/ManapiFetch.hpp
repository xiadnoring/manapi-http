#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <curl/curl.h>

#include "ManapiTask.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiHttpRequest.hpp"
#include "async/ManapiAsyncParallelRun.hpp"

namespace manapi::net {

    class curlformdata {
    public:
        struct multipart_param_value_file {
            std::function<size_t (void *buff, size_t size)> callback;
            long long filesize;
        };

        enum multipart_param_type {
            PARAM_DEFAULT = 0,
            PARAM_FILE = 1,
            PARAM_CALLBACK = 2
        };

        struct multipart_param_value {
            std::unique_ptr<void, void (*)(void*)> data;
            multipart_param_type type = PARAM_DEFAULT;
        };

        typedef std::map <std::string, multipart_param_value> tdata;

        curlformdata();
        ~curlformdata();

        curlformdata(curlformdata &&fd) noexcept;
        curlformdata &operator=(curlformdata &&fd) noexcept ;

        void setdata (const std::string &name, std::string value);
        void setfile (const std::string &filename, std::string filepath);
        void setcallback (const std::string &name, const long long &size, const std::function<size_t (void *buff, size_t buff_size)> &cb);

        void clear ();

        tdata::iterator begin();
        tdata::iterator end();
    private:
        tdata mdata;
    };

    class fetch : public task {
        struct curl_deleter {
            void operator() (CURL *curl)
                { curl_free(curl); }
        };
        struct curl_slist_deleter {
            void operator() (curl_slist *list)
                { curl_slist_free_all(list); }
        };

        struct curl_mime_deleter {
            void operator() (curl_mime *mime)
                { curl_mime_free(mime); }
        };

        struct shared_data {
            async::mutex async_run;
            ssize_t async_buffer_cursor{0};
            std::string async_buffer{};
            std::function <void(CURL *)> handle_custom_setup{nullptr};
            std::function <ssize_t(char *, ssize_t)> handler_body{nullptr};
            std::function <manapi::future<>(bool finish)> async_handler_body{nullptr};
            std::function <manapi::future<bool>(std::map <std::string, std::string>)> async_handler_headers{nullptr};
            std::function <bool(std::map <std::string, std::string>)> handler_headers{nullptr};
            std::shared_ptr<async::context> ctx;
            std::unique_ptr<CURL, curl_deleter> curl {nullptr};
            std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers {nullptr};
            std::map<std::string, std::string> headers{};
            std::atomic<bool> async_waiting{false};
        };
    public:

        explicit fetch(const std::shared_ptr<async::context> &ctx, const std::string &url);
        fetch(fetch &&n) noexcept;
        ~fetch() override;

        enum body_type {
            BODY_NONE = 0,
            BODY_PLAIN = 1,
            BODY_MULTIPART = 2
        };

        fetch &operator=(fetch &&n) noexcept;
        void handle_body(std::function<ssize_t(char *, ssize_t)> handler);
        void handle_async_body(std::function<manapi::future<ssize_t>(char *, ssize_t )> handler);
        void handle_headers (std::function<bool(std::map <std::string, std::string>)> handler);
        void handle_async_headers (std::function<manapi::future<bool>(std::map<std::string, std::string>)> handler);
        void enable_alpn (bool status);
        void enable_http3 ();
        void enable_http2 ();
        void enable_http1_1 ();
        void set_body (curlformdata params);
        void set_method (std::string method);
        void set_body (std::string data);
        void set_headers (std::map <std::string, std::string> headers);
        void set_custom_setup (std::function<void(CURL *curl)> func);
        void enable_ssl_verify (const bool &status);
        void set_verbose (bool status);

        void break_write_loop ();
        void continue_write_loop ();

        [[nodiscard]] size_t get_status_code () const;

        future<void> async_doit();

        future<std::string> text();
        future<manapi::json> json();

        std::map <std::string, std::string> get_headers();

        void clear ();
    private:
        static size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata);
        static size_t curl_write_handler (char *buffer, size_t size, size_t n_mem_b, void *user_p);

        future<CURLcode> async_curl_perform ();
        size_t status_code = 200;

        int attempts = 20;
        std::chrono::milliseconds attempt_delay {100};

        std::string url;

        std::shared_ptr<shared_data> data{nullptr};

        body_type body = BODY_NONE;

        std::string body_default{};
        std::string method{};
        curlformdata body_formdata;

        std::atomic<ssize_t> total_read{0};
        std::atomic<ssize_t> total_write{0};

    };
}
