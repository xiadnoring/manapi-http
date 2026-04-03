#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiBaseWorker.hpp"
#include "./ManapiInterfaceWorker.hpp"

namespace manapi::net::worker {
    class udp : public worker::interface_worker {
    public:
        explicit udp(std::shared_ptr<net::http::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);
        ~udp() override;
        manapi::future<status> init(std::size_t deep) override;
        void stop(std::function<void()> cb) override;
        virtual void onrecv (const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) = 0;
    protected:
        virtual void recv_buffer_dealloc_ (const ev::buff_t *buf);
        virtual void recv_buffer_alloc_ (ssize_t nread, ev::buff_t *buff);
        sockaddr_storage sockaddrin{};
        std::shared_ptr<ev::udp> udp_accept_;
        addrinfo *local;
        timeval recv_timeout{}, send_timeout{};
    private:
    };
}