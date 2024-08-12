#ifndef MANAPIUDPPOOL_H
#define MANAPIUDPPOOL_H

#include <string>
#include <vector>
#include <netdb.h>

namespace manapi::net {
    class udp_pool {
    public:
        explicit udp_pool(const size_t &size, const std::string &address, const std::string &port);
        ~udp_pool();
    private:
        std::vector <int>   sockets;
        addrinfo            *local{};
        const int           so_reuseaddr_param = 1;
    };
}

#endif //MANAPIUDPPOOL_H
