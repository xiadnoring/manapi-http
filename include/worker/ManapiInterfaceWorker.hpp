#pragma once

#include "worker/ManapiBaseWorker.hpp"

namespace manapi::net::worker {
    class interface_worker : public worker::base {
    public:
        interface_worker (net::http::site site, std::shared_ptr<multithread_storage::worker_t> worker_data, manapi::net::http::config *config);

        ~interface_worker () override;

        std::shared_ptr<worker::base> copy ();

        void wrk_global (wrk_interface_global_t *data);

        wrk_interface_global_t *wrk_global ();

        net::http::site &site () override;

        net::http::config *config () override;

        const std::shared_ptr<multithread_storage::worker_t> &worker_data () override;

    protected:
        std::shared_ptr<multithread_storage::worker_t> worker_data_;

        wrk_interface_global_t global_;

        net::http::site site_;

        std::weak_ptr<interface_worker> self_;

        manapi::net::http::config *config_;
    };
}