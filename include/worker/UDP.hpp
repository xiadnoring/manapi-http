#pragma once

#include <netdb.h>

#include "./Base.hpp"

namespace manapi::net::worker {
    class udp : public worker::base {
    public:
        explicit udp(net::site &site);
        ~udp() override;
        void init() override;
    protected:
        int socket_param_true = 1;
        int socket_param_false = 0;
        addrinfo *local;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
        int fd{0};
    private:
    };
}