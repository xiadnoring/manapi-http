#pragma once

#include "./ManapiBaseWorker.hpp"

namespace manapi::net::worker {
    class interface_worker : public worker::base {
    public:
        interface_worker (std::shared_ptr<net::worker::base_http> site, manapi::net::worker::worker_data_t* worker_data, manapi::net::http::config *config);

        ~interface_worker () override;

        void wrk_global (wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT;

        wrk_interface_global_t *wrk_global () MANAPIHTTP_NOEXCEPT;

        const std::shared_ptr<net::worker::base_http> &site () MANAPIHTTP_NOEXCEPT override;

        net::http::config *config () MANAPIHTTP_NOEXCEPT override;

        MANAPIHTTP_NODISCARD int worker_flags () MANAPIHTTP_NOEXCEPT;

        void worker_pool_id (std::size_t worker_pool_id) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t worker_pool_id () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t deep_worker_id () const MANAPIHTTP_NOEXCEPT;

        manapi::net::worker::worker_data_t *worker_data () MANAPIHTTP_NOEXCEPT override;

        manapi::future<status> init(std::size_t deep) override;

        void stop(manapi::stoken token) override;
    protected:
        int flags_;

        manapi::net::worker::worker_data_t *worker_data_;

        wrk_interface_global_t global_;

        std::shared_ptr<net::worker::base_http> site_;

        manapi::net::http::config *config_;

        std::size_t worker_pool_id_;

        std::size_t deep_worker_id_;

        manapi::stoken m_token;
    };
}