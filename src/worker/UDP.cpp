#include "worker/UDP.hpp"
#include "ManapiParams.hpp"

#include <fcntl.h>

manapi::net::worker::udp::udp(net::site &site) : worker::base(site) {

}

manapi::net::worker::udp::~udp() {
    freeaddrinfo(this->local);
}

void manapi::net::worker::udp::init() {
    this->hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    auto address = this->config->get_address();
    auto port = this->config->get_port();

    if (getaddrinfo(address->data(), port->data(), &this->hints, &this->local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    this->config->set_server_address(*this->local->ai_addr);
    this->config->set_server_len(this->local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP UDP PORT USED: {}. https://{}:{}", *port, *address, *port);

    // for quic
    this->config->set_socket_fd(socket (this->local->ai_family, SOCK_DGRAM, 0));
    auto &fd = this->config->get_socket_fd();

    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }

    setsockopt((fd), SOL_SOCKET, SO_REUSEADDR, &this->socket_param_true, sizeof(this->socket_param_true));

    set_fd_non_blocking(fd);

    if (bind(fd.load(), this->local->ai_addr, this->local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", *port);
    }

    this->fd = fd;
}
