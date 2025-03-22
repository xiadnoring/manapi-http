#pragma once

#include <set>

#include "../ManapiUtils.hpp"
#include "../ManapiHttpTypes.hpp"
#include "../worker/base_worker.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"

#include "./Utils.hpp"
#include "../async/ManapiAsyncFileStream.hpp"
#include "../components/Buffer.hpp"

namespace manapi::net::http {
    class base : public manapi::task {
    public:
        base(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~base() override;
        virtual void prepare ();
        virtual future<void> parse_request (ssize_t j = 0, ssize_t size = 0);
        virtual future<void> send_response (manapi::net::http::response &res);
        virtual future<void> execute_handler ();
        virtual future<void> send_response_file (manapi::net::http::response &res, response_features_t &features);
        virtual future<void> send_response_text (manapi::net::http::response &res, response_features_t &features);
        virtual future<void> send_response_proxy (manapi::net::http::response &res, response_features_t &features);
        virtual future<void> send_response_formdata (manapi::net::http::response &res, response_features_t &features);
        virtual future<ssize_t> mask_response (manapi::net::http::response &resp, bool finish);
        future<void> handle_request (const http_handler_page *data, http::request_data_t &request_data, const size_t &status = 200);
        future<void> send_error_response (const size_t &status, http::request_data_t &request_data, const http_handler_page *error);
        future<void> send_file(manapi::net::http::response &res, filesystem::async::fstream &f, ssize_t size, std::vector<replace_founded_item> &replacers) const;
        future<void> send_file(manapi::net::http::response &res, filesystem::async::fstream &f, ssize_t size) const;
        future<void> send_text(std::string_view text, ssize_t size) const;
        future<void> expect_header ();
        future<std::string> compress_file(const std::string &file, const std::string &folder, const std::string &compress, const std::function<future<bool>(const std::string &src, const std::string &dest)> &compressor) const;
        virtual future<ssize_t> read (void *buf, ssize_t size);
        manapi::net::site &get_site ();

        std::shared_ptr<worker::connection> connection;
        static std::set<std::string> methods;
        http::request_data_t request_data;

        object_item_pool<bytebuffer, std::size_t> buffer{};
    protected:
        std::shared_ptr<manapi::net::http::config> config{nullptr};
        std::shared_ptr<manapi::net::worker::base> worker{nullptr};
        manapi::net::site &site;

        std::function<void(char&)> current{nullptr}, next{nullptr};
    };
}
