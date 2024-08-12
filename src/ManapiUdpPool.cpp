#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "ManapiUdpPool.h"
#include "ManapiUtils.h"

manapi::net::udp_pool::udp_pool(const size_t &size, const std::string &address, const std::string &port) {
    constexpr addrinfo hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_DGRAM,
        .ai_protocol    = IPPROTO_UDP
    };

    if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
        THROW_MANAPI_EXCEPTION("{}", "failed to resolve host");
    }

    for (size_t i = 0; i < size; i++)
    {
        auto sock_fd = socket (local->ai_family, SOCK_DGRAM, 0);
        if (sock_fd < 0)
        {
            THROW_MANAPI_EXCEPTION("failed to init udp socket: socket(...) = {}", sock_fd);
        }

        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

        sockets.emplace_back(sock_fd);
    }
}

manapi::net::udp_pool::~udp_pool() {
    for (const auto socket: sockets)
    {
        close (socket);
    }

    freeaddrinfo(local);
}
