#ifndef MANAPIHTTP_MANAPIFETCH_H
#define MANAPIHTTP_MANAPIFETCH_H

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "ManapiTask.h"
#include "ManapiJson.h"
#include "ManapiHttpRequest.h"

namespace manapi::net {

    class fetch : public task {
    public:
        explicit fetch(const std::string &_url);
        ~fetch();

        void handle_body(const std::function<size_t(char *, const size_t&)> &handler);
        void handle_headers (const std::function<void(const std::map <std::string, std::string> &)> &handler);

        void set_method (const std::string &method, const bool &linked = false);
        void set_body (const std::string &data, const bool &linked = false);
        void set_headers (const std::map <std::string, std::string> &headers, const bool &linked = false);

        [[nodiscard]] size_t get_status_code () const;

        void doit() override;

        std::string text();
        manapi::utils::json json();

        const std::map <std::string, std::string> &get_headers();
    private:
        size_t status_code;

        std::map <std::string, std::string>
                                    headers_list;
        std::string                 url;
        std::function <size_t(char *, size_t)>
                                    handler_body = nullptr;
        std::function <void(const std::map <std::string, std::string> &)>
                                    handler_headers = nullptr;


        const std::map <std::string, std::string>
                                    *headers = nullptr;
        const std::string           *body = nullptr;
        const std::string           *method = nullptr;

        bool headers_linked         = false;
        bool body_linked            = false;
        bool method_linked          = false;
    };
}

#endif //MANAPIHTTP_MANAPIFETCH_H
