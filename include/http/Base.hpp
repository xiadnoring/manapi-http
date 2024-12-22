#ifndef MANAPIHTTP_HTTP_BASE_HPP
#define MANAPIHTTP_HTTP_BASE_HPP

#include <set>

#include "../worker/Base.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"

namespace manapi::net::http {
    struct response_features_t {
        const std::string &compress;
        manapi::net::utils::compress::TEMPLATE_INTERFACE compressor = nullptr;
        const std::map <std::string, std::string> *replacers = nullptr;
    };

    class base : public manapi::net::task {
    public:
        base(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~base() override;
        virtual void prepare ();
        virtual future<void> parse_request (ssize_t j = 0, ssize_t size = 0);
        virtual future<void> send_response (manapi::net::http_response &res);
        virtual future<void> execute_handler ();
        virtual future<void> send_response_file (manapi::net::http_response &res, response_features_t &features);
        virtual future<void> send_response_text (manapi::net::http_response &res, response_features_t &features);
        virtual future<void> send_response_proxy (manapi::net::http_response &res, response_features_t &features);
        virtual future<ssize_t> mask_response (manapi::net::http_response &resp, bool finish);
        future<void> handle_request (const http_handler_page *data, request_data_t &request_data, const size_t &status = 200, const std::string &message = HTTP_STATUS.OK_200);
        future<void> send_error_response (const size_t &status, request_data_t &request_data, const std::string &message, const http_handler_page *error);
        future<void> send_file(manapi::net::http_response &res, std::ifstream &f, ssize_t size, std::vector<utils::replace_founded_item> &replacers) const;
        future<void> send_file(manapi::net::http_response &res, std::ifstream &f, ssize_t size) const;
        future<void> send_text(const std::string &text, const size_t &size) const;
        future<void> expect_header ();
        future<std::string> compress_file(const std::string &file, const std::string &folder, const std::string &compress, manapi::net::utils::compress::TEMPLATE_INTERFACE compressor) const;
        virtual future<ssize_t> read (void *buf, size_t size);

        std::shared_ptr<worker::connection> connection;
        static std::set<std::string> methods;
        request_data_t request_data;
        std::string buffer;
    protected:
        std::shared_ptr<manapi::net::http::config> config;
        std::shared_ptr<manapi::net::worker::base> worker;
        manapi::net::site &site;

        std::function<void(char&)> current, next;
    };
}

#endif //MANAPIHTTP_HTTP_BASE_HPP
