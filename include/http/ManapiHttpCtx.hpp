#pragma once

#include <atomic>

#include "../json/ManapiJson.hpp"
#include "../std/ManapiAsyncThreadsMutex.hpp"
#include "../utils/ManapiMultithreadStorage.hpp"

namespace manapi::net::worker {
    class base;
    class interface_worker;
    struct wrk_interface_global_t;
}

namespace manapi::net::http {
    class request;
    class response;
    class uresponse;
}

namespace manapi::net::http {
    class server_ctx : public std::enable_shared_from_this<server_ctx> {
        struct data_t;

        server_ctx ();
    public:
        struct pool_worker_t {
            void *data;
            std::size_t ref;
        };

        struct pool_t {
            std::vector<pool_worker_t> data;
            std::unique_ptr<std::mutex> mx;
        };

        struct worker_data_t {
            std::atomic<ssize_t> count;
            std::vector<pool_t> pools;
        };


        static manapi::status_or<std::shared_ptr<server_ctx>> create () MANAPIHTTP_NOEXCEPT;

        ~server_ctx ();

        multithread_storage &storage ();
    private:
        std::unique_ptr <data_t> m_data;
    };
}
