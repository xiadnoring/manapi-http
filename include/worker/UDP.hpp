#pragma once

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "../ManapiUtils.hpp"
#include "./base_worker.hpp"

namespace manapi::net::worker {
    class udp : public worker::base {
    public:
        explicit udp(net::site &site);
        ~udp() override;
        void init() override;
    protected:
#ifdef _WIN32
        char socket_param_true = 1;
        char socket_param_false = 0;
#else
        int socket_param_true = 1;
        int socket_param_false = 0;
#endif
        addrinfo *local;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
        socket_t fd{0};
    private:
    };
}