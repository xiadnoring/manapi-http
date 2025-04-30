#include "worker/UDP.hpp"
#include "ManapiParams.hpp"

#include <fcntl.h>

#include "async/ManapiAsyncSocket.hpp"

manapi::net::worker::udp::udp(net::site &site) : worker::base(site) {

}

manapi::net::worker::udp::~udp() {
    async::close_descriptor(this->fd);
    freeaddrinfo(this->local);
}

void manapi::net::worker::udp::init() {
    this->hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    auto address = this->config->address();
    auto port = this->config->port();

    if (getaddrinfo(address->data(), port->data(), &this->hints, &this->local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    this->config->server_address(*this->local->ai_addr);
    this->config->server_len(this->local->ai_addrlen);

    MANAPIHTTP_LOG(this->site.async_context(), "HTTP UDP PORT USED: {}. https://{}:{}", *port, *address, *port);

    // for quic
    auto fd = socket (this->local->ai_family, SOCK_DGRAM, 0);

    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }

    this->fd = fd;

    setsockopt((fd), SOL_SOCKET, SO_REUSEADDR, &this->socket_param_true, sizeof(this->socket_param_true));

    manapi::async::set_non_blocking(fd);

    if (bind(fd, this->local->ai_addr, this->local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", *port);
    }

    this->watcher = this->site.async_context()->eventloop()->create_watcher_socket(fd, [this] (std::shared_ptr<ev::io> &w, int status, int revents)
            -> void { this->onrecv(w, status, revents); });
    this->watcher->start(ev::READ);
}
