#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <curl/curl.h>

#include "ManapiTask.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiHttpRequest.hpp"

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

        tdata::iterator begin();
        tdata::iterator end();
    private:
        tdata mdata;
    };

    class fetch : public task {
    public:
        explicit fetch(const std::string &url);
        fetch(fetch &&n) noexcept;
        ~fetch() override;

        enum body_type {
            BODY_NONE = 0,
            BODY_PLAIN = 1,
            BODY_MULTIPART = 2
        };

        fetch &operator=(fetch &&n) noexcept;
        void handle_body(const std::function<size_t(char *, const size_t&)> &handler);
        void handle_headers (const std::function<void(const std::map <std::string, std::string> &)> &handler);

        void set_body (curlformdata params);
        void set_method (std::string method);
        void set_body (std::string data);
        void set_headers (const std::map <std::string, std::string> &headers);
        void set_custom_setup (const std::function<void(CURL *curl)> &func);
        void enable_ssl_verify (const bool &status);

        [[nodiscard]] size_t get_status_code () const;

        void doit() override;

        std::string text();
        manapi::json json();

        std::map <std::string, std::string> get_headers();
    private:
        size_t status_code = 200;

        std::map <std::string, std::string> headers_list;
        std::string url;

        std::function <void(CURL *)> handle_custom_setup;
        std::function <size_t(char *, size_t)> handler_body;
        std::function <void(const std::map <std::string, std::string> &)> handler_headers;

        body_type body = BODY_NONE;

        std::string body_default{};
        std::string method{};
        curlformdata body_formdata;

        CURL *curl = nullptr;
        struct curl_slist* curl_headers = nullptr;
    };
}