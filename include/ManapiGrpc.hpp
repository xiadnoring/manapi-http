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

    manapi::future<manapi::status_or<std::shared_ptr<grpc::ChannelCredentials>>> secure_channel_credentials (std::string certfile);

    class server_ctx : public std::enable_shared_from_this<server_ctx> {
        struct data_t;

        server_ctx ();

    public:
        struct worker_data_t {
            std::atomic<ssize_t> cnt;
        };

        static manapi::status_or<std::shared_ptr<server_ctx>> create () MANAPIHTTP_NOEXCEPT;

        multithread_storage &storage ();

        manapi::status enable_threadpool (bool status) MANAPIHTTP_NOEXCEPT;

        static void clean () MANAPIHTTP_NOEXCEPT;
    private:
        std::unique_ptr<data_t> m_data;
    };

    class server : public std::enable_shared_from_this<server> {
    public:
        struct data_t;

        server (std::shared_ptr<wgrpc::server_ctx> ctx);

        static manapi::status_or<std::shared_ptr<server>> create (std::shared_ptr<wgrpc::server_ctx> ctx) MANAPIHTTP_NOEXCEPT;

        ~server();

        manapi::future<manapi::status> config (std::string path);

        manapi::future<manapi::status> config_object (manapi::json config);

        manapi::future<manapi::status> start (std::move_only_function<manapi::status(::grpc::ServerBuilder &b)> cb);

        manapi::status stop ();
    private:

        std::unique_ptr <data_t> m_data;
    };


}

#endif