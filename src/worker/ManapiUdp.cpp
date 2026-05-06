#include <fcntl.h>
#include <memory>
#include <memory.h>

#include "ManapiDns.hpp"
#include "ManapiParams.hpp"
#include "ManapiEventLoop.hpp"
#include "worker/ManapiUdp.hpp"
#include "std/ManapiAsyncSocket.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "../include/ManapiUtils.hpp"

manapi::net::worker::udp::udp(std::shared_ptr<net::worker::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : worker::interface_worker(std::move(site), std::move(wdata), config) {
    this->local = nullptr;
}

manapi::net::worker::udp::~udp() {
    ev::getaddrinfo::free(this->local);
}

manapi::future<manapi::status> manapi::net::worker::udp::init(std::size_t deep) {
    addrinfo hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    auto &address = this->config_->address;
    auto &port = this->config_->port;

    int rhs = co_await dns::getaddrinfo(address.data(), port.data(), &hints, &this->local, ctokens::timeout(5000));
    if (rhs) {
        manapi_log_trace(debug::LOG_TRACE_HIGH, "%s failed due to %s", "dns::getaddrinfo()", ev::strerror(rhs));
        co_return status_internal("failed to resolve host");
    }

    this->config_->server_len=static_cast<decltype(this->config_->server_len)>(this->local->ai_addrlen);
    memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

    manapi_log_trace(debug::LOG_TRACE_HIGH, "UDP PORT USED: %.*s. %.*s:%.*s",
        port.size(), port.data(), address.size(), address.data(), port.size(), port.data());

    auto wres = manapi::async::current()->eventloop()->create_watcher_udp([this] (const std::shared_ptr<ev::udp> &w, ssize_t nread, const ev::buff_t *buf, const sockaddr *addr, unsigned flags)
        -> void {
        if (nread > 0 && addr)
            this->onrecv(w, buf->base, static_cast<ssize_t>(nread), addr, flags);

        this->recv_buffer_dealloc_(buf);
    },
    [this](const std::shared_ptr<ev::udp> &w, std::size_t nread, ev::buff_t *buff)
        -> void {
        this->recv_buffer_alloc_(nread, buff);
    });

    if (!wres)
        co_return wres.err();

    this->udp_accept_ = wres.unwrap();

    memset(&this->sockaddrin, '\0', sizeof (sockaddr));

    if (this->local->ai_family == ev::IPv4) {
        rhs = this->udp_accept_->ip4_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in *>(&this->sockaddrin));
        if (rhs) {
            manapi_log_error("%s failed due to %s(%d)", "udp:ip4_addr failed", ev::strerror(rhs), rhs);
            co_return status_internal("udp:ip4_addr failed");
        }
    }
    else if (this->local->ai_family == ev::IPv6) {
        rhs = this->udp_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin));
        if (rhs) {
            manapi_log_error("%s failed due to %s(%d)", "udp:ip6_addr failed", ev::strerror(rhs), rhs);
            co_return status_internal("udp:ip6_addr failed");
        }
    }

    {
        uint32_t bind_flags = ev::UDP_REUSEADDR;
#if defined(__unix__) && !defined(__APPLE__)
        bind_flags |= ev::UDP_REUSEPORT;
#endif
        rhs = this->udp_accept_->s_bind(reinterpret_cast<sockaddr *>(&this->sockaddrin), bind_flags);
        if (rhs) {
            manapi_log_error("%s failed due to %s(%d)", "udp:couldn't bind socket", ev::strerror(rhs), rhs);
            co_return status_internal("udp:s_bind failed");
        }
    }


    co_return status_ok();
}

void manapi::net::worker::udp::stop(std::function<void()> cb) {
    if (this->udp_accept_) {
        try {
            this->udp_accept_->recv_stop();

            manapi::async::current()->eventloop()->stop_callback(this->udp_accept_,
                [cb = std::move(cb)] (const ev::shared_udp &w) -> void {
                cb ();
            });

            manapi::async::current()->eventloop()
                ->stop_watcher(std::move(this->udp_accept_));
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "udp:Stop", e.what());

            if (cb)
                cb();
        }
    }
    else
        cb();
}

void manapi::net::worker::udp::recv_buffer_dealloc_(const ev::buff_t *buf) {
    /* free */
    this->bufferpool().free(buf->base, buf->len);

}

void manapi::net::worker::udp::recv_buffer_alloc_(std::size_t nread, ev::buff_t *buff) {
    auto bufres = this->bufferpool().buffer(1, nread);
    if (bufres.ok()) {
        auto buffer = bufres.unwrap();
        buff->len = static_cast<decltype(buff->len)>(buffer.realsize());
        buff->base = static_cast<char *>(buffer.release());
    }
}
