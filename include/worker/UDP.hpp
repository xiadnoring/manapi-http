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
        void stop() override;
        virtual void onrecv (std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) = 0;
    protected:
        sockaddr_storage sockaddrin{};
        std::shared_ptr<ev::udp> udp_accept_;
        addrinfo *local;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
    private:
    };
}