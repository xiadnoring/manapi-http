#pragma once

#include "./ManapiUtils.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "./utils/ManapiConfig.hpp"
#include "./std/ManapiAsyncContext.hpp"
#include "./utils/ManapiMultithreadStorage.hpp"


namespace manapi::net::wgrpc {
    class event_engine_wrapper;

    manapi::future<manapi::error::status_or<std::shared_ptr<grpc::ChannelCredentials>>> secure_channel_credentials (std::string certfile);

    class server_ctx {
        struct data_t;
        server_ctx ();
    public:
        struct worker_data_t {
            std::atomic<ssize_t> cnt;
        };

        static manapi::error::status_or<server_ctx> create () MANAPIHTTP_NOEXCEPT;

        multithread_storage &storage ();

        manapi::error::status enable_threadpool (bool status) MANAPIHTTP_NOEXCEPT;
    private:
        std::shared_ptr<data_t> data_;
    };

    class server {
        struct data_t;

        server (wgrpc::server_ctx ctx);
    public:
        static manapi::error::status_or<server> create (wgrpc::server_ctx ctx) MANAPIHTTP_NOEXCEPT;

        ~server();

        manapi::future<manapi::error::status> config (std::string path);

        manapi::future<manapi::error::status> config_object (manapi::json config);

        manapi::future<manapi::error::status> start (std::move_only_function<manapi::error::status(::grpc::ServerBuilder &b)> cb);

        manapi::error::status stop ();
    private:
        static manapi::future<> stop_ (std::shared_ptr<data_t> data);

        manapi::future<manapi::error::status> subscribe_ ();

        manapi::error::status setup_user_config_ ();

        error::status setup_config_ (manapi::json data, manapi::json &n);

        std::shared_ptr<data_t> data_;
    };


}

#endif