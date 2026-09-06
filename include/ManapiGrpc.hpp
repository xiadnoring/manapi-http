#pragma once

#include "./ManapiUtils.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "./utils/ManapiConfig.hpp"
#include "./std/ManapiContext.hpp"
#include "./utils/ManapiMultithreadStorage.hpp"
#include "./std/ManapiStopToken.hpp"


namespace manapi::net::wgrpc {
    class event_engine_wrapper;

    manapi::future<manapi::status_or<std::shared_ptr<grpc::ChannelCredentials>>> secure_channel_credentials (std::string certfile);

    class server_ctx : public std::enable_shared_from_this<server_ctx> {

        server_ctx ();

    public:
        struct data_t;

        ~server_ctx();

        static manapi::status_or<std::shared_ptr<server_ctx>> create () MANAPIHTTP_NOEXCEPT;

        server_ctx::data_t &storage ();

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

        manapi::future<manapi::status> start (std::move_only_function<manapi::status(::grpc::ServerBuilder &b, grpc::Service *arg)> cb, grpc::Service * service = nullptr);

        void send_stop (manapi::stoken token);

        manapi::future<manapi::status> stop ();
    private:

        std::unique_ptr <data_t> m_data;
    };


}

#endif