#include "http/HTTPv2.hpp"

#include "ManapiUnicode.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiURL.hpp"
#include "components/ManapiURLDecodeStream.hpp"

manapi::net::http::http_v2::http_v2(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,
                                    manapi::net::site &site) : base(std::move(worker), std::move(config), site) {

}

std::shared_ptr<manapi::net::worker::http_v2> manapi::net::http::http_v2::create(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,manapi::net::site &site) {
    std::shared_ptr<manapi::net::worker::http_v2> w = std::make_shared <manapi::net::worker::http_v2> (std::move(worker), std::move(config), site);
    w->new_dependency = [w = std::weak_ptr<manapi::net::worker::http_v2> (w)] () {
        return std::shared_ptr<manapi::net::worker::http_v2> (w);
    };
    return std::move(w);
}

manapi::future<void> manapi::net::http::http_v2::parse_request(ssize_t j, ssize_t size) {
    net::http::url_decode_stream url_decode;

    if (!this->request_data.buffer) {
        this->request_data.buffer = this->site.bufferpool().get();
    }

    this->request_data.buffer->resize(this->config->buffer_size());

    for (char & i : this->request_data.uri) {
        url_decode << i;
    }

    auto result = url_decode.result();
    this->request_data.path = std::move(result.first);
    this->request_data.divided = result.second;

    co_return;
}

manapi::future<void> manapi::net::http::http_v2::execute_handler() {
    const auto handler = this->site.handler(this->request_data);
    co_await handle_request(&handler, this->request_data);
    co_return;
}

