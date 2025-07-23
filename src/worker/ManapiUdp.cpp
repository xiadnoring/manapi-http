#include "worker/ManapiUdp.hpp"
#include "ManapiParams.hpp"

#include <fcntl.h>
#include <memory>
#include <memory.h>
#include "../include/ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"

manapi::net::worker::udp::udp(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : worker::interface_worker(std::move(site), std::move(wdata), config) {

}

manapi::net::worker::udp::~udp() {
    freeaddrinfo(this->local);
}

void manapi::net::worker::udp::init(std::size_t deep) {
    addrinfo hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    auto &address = this->config_->address;
    auto &port = this->config_->port;

    if (getaddrinfo(address.data(), port.data(), &hints, &this->local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "failed to resolve host");
    }

    this->config_->server_len=(this->local->ai_addrlen);
    memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

    manapi_log_trace("UDP PORT USED: %.*s. %.*s:%.*s",
        port.size(), port.data(), address.size(), address.data(), port.size(), port.data());

    this->udp_accept_ = manapi::async::current()->eventloop()->create_watcher_udp([this] (const std::shared_ptr<ev::udp> &w, ssize_t nread, const ev::buff_t *buf, const sockaddr *addr, unsigned flags)
        -> void {
        assert (nread >= 0);

        if (addr) {
            this->onrecv(w, buf->base, static_cast<ssize_t>(nread), addr, flags);
        }

        this->recv_buffer_dealloc_(buf);
    },
    [this](const std::shared_ptr<ev::udp> &w, ssize_t nread, ev::buff_t *buff)
        -> void {
        this->recv_buffer_alloc_(nread, buff);
    });

    memset(&this->sockaddrin, '\0', sizeof (sockaddr));

    if (this->local->ai_family == ev::IPv4) {
        if (auto rhs = this->udp_accept_->ip4_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in *>(&this->sockaddrin))) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv4 addr due to result - {}", rhs);
            goto err;
        }
    }
    else if (this->local->ai_family == ev::IPv6) {
        if (auto rhs = this->udp_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv6 addr due to result - {}", rhs);
            goto err;
        }
    }

    {
        int bind_flags = ev::UDP_REUSEADDR;
#if defined(__unix__) && !defined(__APPLE__)
        bind_flags |= ev::UDP_REUSEPORT;
#endif
        if (auto rhs = this->udp_accept_->s_bind(reinterpret_cast<sockaddr *>(&this->sockaddrin), bind_flags)) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't bind socket due to result - {}", rhs);
            goto err;
        }
    }


    return;
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "couldn't initialize udp connection");
}

void manapi::net::worker::udp::stop(std::function<void()> cb) {
    this->udp_accept_->recv_stop();
    manapi::async::current()->eventloop()->stop_callback(this->udp_accept_,
        [cb = std::move(cb)] (const ev::shared_udp &w) -> void {
        cb ();
    });
    manapi::async::current()->eventloop()
        ->stop_watcher(std::move(this->udp_accept_));
}

void manapi::net::worker::udp::recv_buffer_dealloc_(const ev::buff_t *buf) {
    /* free */
    this->bufferpool().free(buf->base, buf->len);

}

void manapi::net::worker::udp::recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) {
    auto buffer = this->bufferpool().buffer(1, nread);
    buff->len = buffer.realsize();
    buff->base = static_cast<char *>(buffer.release());
}
