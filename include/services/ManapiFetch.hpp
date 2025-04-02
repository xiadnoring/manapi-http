#pragma once

#include "../ManapiUtils.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY
#define MANAPIHTTP_FETCH_SUPPORT

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <curl/curl.h>

#include "../ManapiUtils.hpp"
#include "ManapiTask.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiHttpRequest.hpp"
#include "../async/ManapiAsyncParallelRun.hpp"
#include "../components/ManapiFileTransferInfo.hpp"

namespace manapi::net {

    class curlformdata {
    public:
        struct multipart_param_value_file {
            std::move_only_function<size_t (void *buff, size_t size)> callback;
            long long filesize;
        };

        enum multipart_param_type {
            PARAM_DEFAULT = 0,
            PARAM_FILE = 1,
            PARAM_CALLBACK = 2
        };

        struct multipart_param_value {
            std::optional<std::string> strdata{};
            std::optional<multipart_param_value_file> filedata{};
            multipart_param_type type = PARAM_DEFAULT;
        };

        typedef std::map <std::string, multipart_param_value> tdata;

        curlformdata();
        ~curlformdata();

        curlformdata(curlformdata &&fd) noexcept;
        curlformdata &operator=(curlformdata &&fd) noexcept ;

        void setdata (const std::string &name, std::string value);
        void setfile (const std::string &filename, std::string filepath);
        void setcallback (const std::string &name, const long long &size, std::move_only_function<size_t (void *buff, size_t buff_size)> cb);

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
            object_item_pool<manapi::bytebuffer> async_buffer{};
            std::move_only_function <void(CURL *)> handle_custom_setup{nullptr};
            std::move_only_function <ssize_t(char *, ssize_t)> handler_recv_body{nullptr};
            std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)> async_handler_recv_body{nullptr};
            std::move_only_function <manapi::future<bool>(std::shared_ptr<shared_data> data, std::map <std::string, std::string>)> async_handler_headers{nullptr};
            std::move_only_function <bool(std::map <std::string, std::string>)> handler_headers{nullptr};
            std::shared_ptr<async::context> ctx;
            std::unique_ptr<CURL, curl_deleter> curl {nullptr};
            std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers {nullptr};
            std::map<std::string, std::string> headers{};
            std::atomic<bool> async_waiting{false};
            std::optional<std::move_only_function<manapi::future<>(std::shared_ptr<shared_data> data, bool)>> async_user_body_cb{};
            std::optional<std::move_only_function<ssize_t(char *buffer, ssize_t size)>> sync_user_body_cb{};
            std::optional<std::function<void(CURLcode)>> parallel_task{};
            std::move_only_function <ssize_t(char *, ssize_t)> handler_send_body{nullptr};
            std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)> async_handler_send_body{nullptr};
        };
    public:

        explicit fetch(const std::shared_ptr<async::context> &ctx, std::string url);
        fetch(fetch &&n) noexcept;
        ~fetch() override;

        enum body_type {
            BODY_NONE = 0,
            BODY_PLAIN = 1,
            BODY_MULTIPART = 2,
            BODY_CALLBACK = 3
        };

        fetch &operator=(fetch &&n) noexcept;
        void handle_body(std::move_only_function<ssize_t(char *, ssize_t)> handler);
        void handle_async_body(std::move_only_function<manapi::future<ssize_t>(char *, ssize_t )> handler);
        void handle_headers (std::move_only_function<bool(std::map <std::string, std::string>)> handler);
        void handle_async_headers (std::move_only_function<manapi::future<bool>(std::map<std::string, std::string>)> handler);
        void enable_alpn (bool status);
        void enable_http3 ();
        void enable_http2 ();
        void enable_http1_1 ();
        void body (curlformdata params);
        void method (std::string method);
        void body (std::string data);
        manapi::future<> body (file_transfer_info file_info);
        void async_body (std::move_only_function<manapi::future<ssize_t>(char *, ssize_t)> handler);
        void body (std::move_only_function<ssize_t(char *, ssize_t)> handler);
        void headers (std::map <std::string, std::string> headers);
        void json_headers (manapi::json headers);
        void custom_setup (std::move_only_function<void(CURL *curl)> func);
        void enable_ssl_verify (const bool &status);
        void verbose (bool status);
        void timeout (const std::size_t &seconds);

        void break_write_loop ();
        void continue_write_loop ();

        [[nodiscard]] size_t status_code () const;

        future<void> async_doit();

        future<std::string> text();
        future<manapi::json> json();

        std::map <std::string, std::string> headers();

        void clear ();
    private:
        static std::map <std::string, CURLoption> http_method_to_enum;
        static manapi::future<bool> handle_body_verify (std::shared_ptr<shared_data> data);
        static manapi::future<void> handle_sync_body_finish(std::shared_ptr<shared_data> data, bool finish);
        static manapi::future<void> handle_async_body_finish(std::shared_ptr<shared_data> data, bool finish);
        static std::size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata);
        static std::size_t curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p);
        static std::size_t curl_read_handler (char *buffer, std::size_t size, std::size_t nitems, void *user_p);
        static object_pool<bytebuffer, std::true_type> bufferpool;

        void setup_parallel_task ();
        void _default_setup_curl ();

        future<CURLcode> async_curl_perform ();

        size_t status_code_ = 200;
        ssize_t content_length_ = -1;

        std::string url_;

        std::shared_ptr<shared_data> data_{nullptr};

        body_type body_ = BODY_NONE;

        std::string body_default_{};
        std::string method_{};
        std::optional<curlformdata> body_formdata_{};
    };
}

#endif