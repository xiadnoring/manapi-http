#pragma once

#include "../ManapiUtils.hpp"
#include "../components/ManapiConfig.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "../async/ManapiAsyncContext.hpp"
#include "../components/ManapiMultithreadStorage.hpp"

namespace manapi::net::wgrpc {
    class event_engine_wrapper;

    class server_ctx {
        struct data_t;
    public:
        struct worker_data_t {
            std::atomic<ssize_t> cnt;
        };

        server_ctx ();

        multithread_storage &storage ();
    private:
        std::shared_ptr<data_t> data_;
    };

    class server {
        struct data_t;
    public:
        server (wgrpc::server_ctx ctx);

        manapi::future<manapi::error::status> config (std::string path);

        manapi::future<manapi::error::status> config_object (manapi::json config);

        manapi::future<manapi::error::status> start (std::move_only_function<manapi::error::status(::grpc::ServerBuilder &b)> cb);
    private:
        manapi::future<manapi::error::status> subscribe_ ();

        void setup_user_config_ ();

        error::status setup_config_ (manapi::json data, manapi::json &n);

        std::shared_ptr<data_t> data_;
    };


}

#endif