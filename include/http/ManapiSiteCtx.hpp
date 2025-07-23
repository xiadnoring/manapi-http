#pragma once

#include <atomic>

#include "../ManapiJson.hpp"
#include "../async/ManapiAsyncThreadsMutex.hpp"
#include "../components/ManapiMultithreadStorage.hpp"

namespace manapi::net::http {
    class server_ctx {
        struct data_t;

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

        server_ctx ();

        ~server_ctx ();

        server_ctx (const server_ctx &n);

        server_ctx &operator=(const server_ctx &n);

        multithread_storage &storage ();
    private:
        std::shared_ptr<data_t> data_;
    };
}
