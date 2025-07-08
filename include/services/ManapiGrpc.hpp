#pragma once

#include "../ManapiUtils.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "../async/ManapiAsyncContext.hpp"

namespace manapi {
    class manapi_grpc_net_listener final : public grpc_event_engine::experimental::EventEngine::Listener {
    public:
        manapi::ev::shared_tcp connection;

        manapi_grpc_net_listener (absl::AnyInvocable<void(absl::Status)> on_shutdown);

        ~manapi_grpc_net_listener() override;

        absl::StatusOr<int> Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) override;

        absl::Status Start() override;
    private:
        absl::AnyInvocable<void(absl::Status)> on_shutdown;
    };

    class manapi_grpc_dns_resolved final : public  grpc_event_engine::experimental::EventEngine::DNSResolver {
    public:
        manapi_grpc_dns_resolved ();

        void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) override;

        void LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) override;

        void LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) override;
    };

    class manapi_grpc_net_endpoint final : public grpc_event_engine::experimental::EventEngine::Endpoint {
        int flags;

        std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr;
        std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr;

        manapi::ev::shared_tcp conn;
        grpc_event_engine::experimental::MemoryAllocator memory_allocator;

        absl::AnyInvocable<void(absl::Status)> on_read;
        grpc_event_engine::experimental::SliceBuffer *on_read_buffer;
        std::size_t on_read_hints_bytes;

        grpc_event_engine::experimental::SliceBuffer buffer;
    public:

        manapi_grpc_net_endpoint (manapi::ev::shared_tcp conn,
            std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr,
            std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr,
            grpc_event_engine::experimental::MemoryAllocator memory_allocator);

        ~manapi_grpc_net_endpoint();

        bool Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) override;

        bool Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) override;

        [[nodiscard]] const grpc_event_engine::experimental::EventEngine::ResolvedAddress &GetLocalAddress() const override;

        [[nodiscard]] const grpc_event_engine::experimental::EventEngine::ResolvedAddress &GetPeerAddress() const override;
    };

    class manapi_grpc_event_engine_wrapper final : public grpc_event_engine::experimental::EventEngine {
    public:
        manapi_grpc_event_engine_wrapper ();

        ~manapi_grpc_event_engine_wrapper() override;

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
    private:
        async::shared_eventloop ev;
    };
}

#endif