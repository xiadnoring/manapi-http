#pragma once

#include "worker/ManapiBaseWorker.hpp"

namespace manapi::net::worker {
    class interface_worker : public worker::base {
    public:
        interface_worker (net::http::site site, std::shared_ptr<multithread_storage::worker_t> worker_data, manapi::net::http::config *config);

        ~interface_worker () override;

        std::shared_ptr<worker::base> copy ();

        void wrk_global (wrk_interface_global_t *data) MANAPIHTTP_NOEXPECT;

        wrk_interface_global_t *wrk_global () MANAPIHTTP_NOEXPECT;

        net::http::site &site () MANAPIHTTP_NOEXPECT override;

        net::http::config *config () MANAPIHTTP_NOEXPECT override;

        void worker_pool_id (std::size_t worker_pool_id) MANAPIHTTP_NOEXPECT;

        MANAPIHTTP_NODISCARD std::size_t worker_pool_id () const MANAPIHTTP_NOEXPECT;

        MANAPIHTTP_NODISCARD std::size_t deep_worker_id () const MANAPIHTTP_NOEXPECT;

        const std::shared_ptr<multithread_storage::worker_t> &worker_data () MANAPIHTTP_NOEXPECT override;

    protected:
        std::shared_ptr<multithread_storage::worker_t> worker_data_;

        wrk_interface_global_t global_;

        net::http::site site_;

        std::weak_ptr<interface_worker> self_;

        manapi::net::http::config *config_;

        std::size_t worker_pool_id_;
        std::size_t deep_worker_id_;
    };
}