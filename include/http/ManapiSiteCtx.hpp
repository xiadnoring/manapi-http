#pragma once

#include <atomic>

#include "../ManapiJson.hpp"
#include "../async/ManapiAsyncThreadsMutex.hpp"
#include "../components/ManapiMultithreadStorage.hpp"

namespace manapi::net::http {
    class server_ctx {
        struct data_t;

    public:
        struct worker_data_t {
            std::atomic<ssize_t> count;
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
