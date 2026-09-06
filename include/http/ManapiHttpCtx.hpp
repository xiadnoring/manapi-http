#pragma once

#include <atomic>

#include "../json/ManapiJson.hpp"
#include "../std/ManapiThreadsMutex.hpp"
#include "../utils/ManapiMultithreadStorage.hpp"
#include "../worker/ManapiHttpBase.hpp"

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
        struct data_t {
            manapi::json data;
            std::mutex mx;
            manapi::async::tmutex wmx;
            net::worker::worker_data_t worker_data;
        };

        server_ctx ();
    public:
        static manapi::status_or<std::shared_ptr<server_ctx>> create () MANAPIHTTP_NOEXCEPT;

        ~server_ctx ();

        data_t &storage ();
    private:
        std::unique_ptr <data_t> m_data;
    };
}
