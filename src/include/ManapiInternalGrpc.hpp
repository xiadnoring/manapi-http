#pragma once


#include "ManapiUtils.hpp"
#include "utils/ManapiConfig.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/version_info.h>

#include "std/ManapiAsyncContext.hpp"

#define MANAPIHTTP_GRPC_SINCE_AT(major, minor, patch) MANAPIHTTP_SINCE_AT_CUSTOM(GRPC_CPP_VERSION_MAJOR, GRPC_CPP_VERSION_MINOR, GRPC_CPP_VERSION_PATCH, major, minor, patch)

namespace manapi::net::wgrpc {
    class config : public internal::config_interface {
    public:
        config (const manapi::json &n);

        int tcp_backlog;
        std::size_t max_buffered_size;
        std::size_t buffer_size;
        manapi::json ssl;
    };

    class net_listener final : public grpc_event_engine::experimental::EventEngine::Listener {
    public:

        net_listener (absl::AnyInvocable<void(absl::Status)> on_shutdown);

        ~net_listener() override;

        absl::StatusOr<int> Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) override;

        absl::Status Start() override;

        void shutdown (bool notify = true) noexcept;

        manapi::error::status set (ev::shared_tcp connection);

        const ev::shared_tcp &conn () const;
    private:
        manapi::ev::shared_tcp connection;
        absl::AnyInvocable<void(absl::Status)> on_shutdown;
        async::shared_eventloop ev;
    };

    class dns_resolved final : public  grpc_event_engine::experimental::EventEngine::DNSResolver {
        event_engine_wrapper *engine;
    public:
        dns_resolved (event_engine_wrapper *engine);

        void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) override;

        void LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) override;

        void LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) override;
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        void *QueryExtension(absl::string_view key) override;
#endif
    };

    class net_endpoint : public grpc_event_engine::experimental::EventEngine::Endpoint {
        int flags;

        std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr;
        std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr;

        manapi::ev::shared_tcp conn;
        async::shared_eventloop ev;
        grpc_event_engine::experimental::MemoryAllocator memory_allocator;

        absl::AnyInvocable<void(absl::Status)> on_read;
        grpc_event_engine::experimental::SliceBuffer *on_read_buffer;
        std::size_t on_read_hints_bytes;

        grpc_event_engine::experimental::SliceBuffer buffer;
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        std::shared_ptr<TelemetryInfo> metric;
#endif

    public:

        net_endpoint (manapi::ev::shared_tcp conn,
            std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr,
            std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr,
            grpc_event_engine::experimental::MemoryAllocator memory_allocator);

        ~net_endpoint() override;
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        bool Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, ReadArgs args) override;

        bool Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data, WriteArgs args) override;
#else
        bool Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) override;

        bool Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) override;
#endif
        MANAPIHTTP_NODISCARD const grpc_event_engine::experimental::EventEngine::ResolvedAddress &GetLocalAddress() const override;

        MANAPIHTTP_NODISCARD const grpc_event_engine::experimental::EventEngine::ResolvedAddress &GetPeerAddress() const override;

#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override;
#endif
    private:
        void init_ ();
    };

    class event_engine_wrapper final : public grpc_event_engine::experimental::EventEngine {
        async::shared_eventloop ev;
    public:
        event_engine_wrapper ();

        ~event_engine_wrapper() override;

        bool Cancel(TaskHandle handle) override;

        ConnectionHandle Connect(OnConnectCallback on_connect,
            const ResolvedAddress &addr,
            const grpc_event_engine::experimental::EndpointConfig &args,
            grpc_event_engine::experimental::MemoryAllocator memory_allocator,
            Duration timeout) override;

        void Run(absl::AnyInvocable<void()> closure) override;

        void Run(Closure *closure) override;

        bool CancelConnect(ConnectionHandle handle) override;

        absl::StatusOr<std::unique_ptr<Listener>> CreateListener(Listener::AcceptCallback on_accept,
            absl::AnyInvocable<void(absl::Status)> on_shutdown,
            const grpc_event_engine::experimental::EndpointConfig &config,
            std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) override;

        TaskHandle RunAfter(Duration when, Closure *closure) override;

        TaskHandle RunAfter(Duration when, absl::AnyInvocable<void()> closure) override;

        bool IsWorkerThread() override;

        absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(const DNSResolver::ResolverOptions &options) override;

        const async::shared_eventloop& shared_eventloop () const;
    };
}

#endif