#ifndef MANAPIHTTP_MANAPIFETCH_H
#define MANAPIHTTP_MANAPIFETCH_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <curl/curl.h>

#include "ManapiTask.hpp"
#include "ManapiJson.hpp"
#include "ManapiHttpRequest.hpp"

namespace manapi::net {

    class fetch : public task {
    public:
        explicit fetch(const std::string &_url);
        ~fetch();

        void handle_body(const std::function<size_t(char *, const size_t&)> &handler);
        void handle_headers (const std::function<void(const std::map <std::string, std::string> &)> &handler);

        void set_method (const std::string &method, const bool &linked = false);
        void set_body (const std::string &data, const bool &linked = false);
        void set_headers (const std::map <std::string, std::string> &headers);
        void set_custom_setup (const std::function<void(CURL *curl)> &func);
        void enable_ssl_verify (const bool &status);

        [[nodiscard]] size_t get_status_code () const;

        void doit() override;

        std::string text();
        manapi::net::utils::json json();

        const std::map <std::string, std::string> &get_headers();
    private:
        size_t status_code = 200;

        std::map <std::string, std::string>
                                    headers_list;
        std::string                 url;

        std::function <void(CURL *)>                                        handle_custom_setup = nullptr;
        std::function <size_t(char *, size_t)>                              handler_body = nullptr;
        std::function <void(const std::map <std::string, std::string> &)>   handler_headers = nullptr;


        const std::string                                                   *body = nullptr;
        const std::string                                                   *method = nullptr;

        bool body_linked            = false;
        bool method_linked          = false;

        CURL *curl                  = nullptr;
        struct curl_slist* curl_headers = nullptr;
    };
}

#endif //MANAPIHTTP_MANAPIFETCH_H
