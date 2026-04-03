#pragma once

#include "./ManapiBaseWorker.hpp"

namespace manapi::net::worker {
    class interface_worker : public worker::base {
    public:
        interface_worker (std::shared_ptr<net::http::site> site, std::shared_ptr<multithread_storage::worker_t> worker_data, manapi::net::http::config *config);

        ~interface_worker () override;

        void wrk_global (wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT;

        wrk_interface_global_t *wrk_global () MANAPIHTTP_NOEXCEPT;

        const std::shared_ptr<net::http::site> &site () MANAPIHTTP_NOEXCEPT override;

        net::http::config *config () MANAPIHTTP_NOEXCEPT override;

        MANAPIHTTP_NODISCARD int worker_flags () MANAPIHTTP_NOEXCEPT;

        void worker_pool_id (std::size_t worker_pool_id) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t worker_pool_id () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t deep_worker_id () const MANAPIHTTP_NOEXCEPT;

        const std::shared_ptr<multithread_storage::worker_t> &worker_data () MANAPIHTTP_NOEXCEPT override;

    protected:
        int flags_;

        std::shared_ptr<multithread_storage::worker_t> worker_data_;

        wrk_interface_global_t global_;

        std::shared_ptr<net::http::site> site_;

        manapi::net::http::config *config_;

        std::size_t worker_pool_id_;

        std::size_t deep_worker_id_;
    };
}