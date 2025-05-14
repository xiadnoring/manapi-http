#include "worker/UDP.hpp"
#include "ManapiParams.hpp"

#include <fcntl.h>
#include <memory>
#include <memory.h>

#include "async/ManapiAsyncSocket.hpp"

manapi::net::worker::udp::udp(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata) : worker::base(std::move(site), std::move(wdata)) {

}

manapi::net::worker::udp::~udp() {
    freeaddrinfo(this->local);
}

void manapi::net::worker::udp::init() {
    addrinfo hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    auto address = this->config()->address();
    auto port = this->config()->port();

    if (getaddrinfo(address->data(), port->data(), &hints, &this->local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    this->config()->server_address(*this->local->ai_addr);
    this->config()->server_len(this->local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP UDP PORT USED: {}. https://{}:{}", *port, *address, *port);

    this->udp_accept_ = manapi::async::current()->eventloop()->create_watcher_udp([this] (std::shared_ptr<ev::udp> &w, ssize_t nread, const ev::buff_t *buf, const sockaddr *addr, unsigned flags)
        -> void {
        assert (nread >= 0);

        if (addr) {
            this->onrecv(w, buf->base, static_cast<ssize_t>(nread), addr, flags);
        }
        else {
            this->recv_buffer_dealloc_(buf);
        }
    },
    [this](std::shared_ptr<ev::udp> &w, ssize_t nread, ev::buff_t *buff)
        -> void {
        this->recv_buffer_alloc_(nread, buff);
    });

    memset(&this->sockaddrin, '\0', sizeof (sockaddr));

    if (this->local->ai_family == ev::IPv4) {
        if (auto rhs = this->udp_accept_->ip4_addr(this->config()->address()->data(), std::stoi(*this->config()->port()), reinterpret_cast<sockaddr_in *>(&this->sockaddrin))) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv4 addr due to result - {}", rhs);
            goto err;
        }
    }
    else if (this->local->ai_family == ev::IPv6) {
        if (auto rhs = this->udp_accept_->ip6_addr(this->config()->address()->data(), std::stoi(*this->config()->port()), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv6 addr due to result - {}", rhs);
            goto err;
        }
    }

    if (auto rhs = this->udp_accept_->s_bind(reinterpret_cast<sockaddr *>(&this->sockaddrin), ev::UDP_REUSEPORT|ev::UDP_REUSEADDR)) {
        manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't bind socket due to result - {}", rhs);
        goto err;
    }


    return;
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_SOCKET, "couldn't initialize udp connection");
}

void manapi::net::worker::udp::stop() {
    this->udp_accept_->recv_stop();
    manapi::async::current()->eventloop()->stop_watcher(std::move(this->udp_accept_));
}

void manapi::net::worker::udp::recv_buffer_dealloc_(const ev::buff_t *buf) {
    /* free */
    auto object = std::make_unique<bytebuffer>(buf->base, buf->len);
    this->bufferpool()->ret(std::move(object));
}

void manapi::net::worker::udp::recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) {
    auto buffer = this->bufferpool()->get();
    buffer->resize(nread);
    auto object = buffer.release();

    buff->len = object->realsize();
    buff->base = static_cast<char *>(object->release());
}
