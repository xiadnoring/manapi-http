#pragma once


#include "ManapiUtils.hpp"
#include "utils/ManapiConfig.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpc/event_engine/event_engine.h>
#include <grpcpp/grpcpp.h>
// #include <grpcpp/version_info.h>

#include "std/ManapiContext.hpp"

#define MANAPIHTTP_GRPC_SINCE_AT(major, minor, patch) MANAPIHTTP_SINCE_AT_CUSTOM(GRPC_CPP_VERSION_MAJOR, GRPC_CPP_VERSION_MINOR, GRPC_CPP_VERSION_PATCH, major, minor, patch)

namespace manapi::net::wgrpc {
#if MANAPIHTTP_GRPC_SINCE_AT(1, 49, 0)
    template<typename ...Args>
    using resolve_callback_t = absl::AnyInvocable<void(Args...)>;
#else
    template<typename ...Args>
    using resolve_callback_t = std::function<void(Args...)>;
#endif
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

        net_listener (resolve_callback_t<absl::Status> on_shutdown);

        ~net_listener() override;

        absl::StatusOr<int> Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) override;

        absl::Status Start() override;

        static void shutdown (net_listener *id, manapi::ev::shared_tcp conn, resolve_callback_t<absl::Status> on_shutdown_cb, async::shared_cthread ev) MANAPIHTTP_NOEXCEPT;

        manapi::status set (ev::shared_tcp connection);

        const ev::shared_tcp &conn () const;
    private:
        manapi::ev::shared_tcp connection;
        resolve_callback_t<absl::Status> on_shutdown;
        async::shared_cthread ev;
    };

    class dns_resolved final : public  grpc_event_engine::experimental::EventEngine::DNSResolver {
        event_engine_wrapper *engine;
    public:
        dns_resolved (event_engine_wrapper *engine);
#if MANAPIHTTP_GRPC_SINCE_AT(1,57,0)
        void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) override;

        void LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) override;

        void LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) override;
#else
        LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port, grpc_event_engine::experimental::EventEngine::Duration timeout) override;

        LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve, absl::string_view name, grpc_event_engine::experimental::EventEngine::Duration timeout) override;

        LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve, absl::string_view name, grpc_event_engine::experimental::EventEngine::Duration timeout) override;

        bool CancelLookup(LookupTaskHandle handle) override;
#endif

#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        void *QueryExtension(absl::string_view key) override;
#endif
    };

    class net_endpoint : public grpc_event_engine::experimental::EventEngine::Endpoint {
        int flags;

        std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr;
        std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr;

        manapi::ev::shared_tcp conn;
        async::shared_cthread ev;
        grpc_event_engine::experimental::MemoryAllocator memory_allocator;

        resolve_callback_t<absl::Status> on_read;
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
#if MANAPIHTTP_GRPC_SINCE_AT(1,54,0)
#   if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        bool Read(resolve_callback_t<absl::Status> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, ReadArgs args) override;

        bool Write(resolve_callback_t<absl::Status> on_writable, grpc_event_engine::experimental::SliceBuffer *data, WriteArgs args) override;
#   else
        bool Read(resolve_callback_t<absl::Status> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) override;

        bool Write(resolve_callback_t<absl::Status> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) override;
#   endif
#else
        bool Read2(resolve_callback_t<absl::Status> &on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args);

        bool Write2(resolve_callback_t<absl::Status> &on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args);

        void Read(resolve_callback_t<absl::Status> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) override;

        void Write(resolve_callback_t<absl::Status> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) override;
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
        std::string_view magic_;
        async::shared_cthread primary;
        int flags;

        friend dns_resolved;
    public:
        event_engine_wrapper ();

        ~event_engine_wrapper() override;

        static std::shared_ptr<event_engine_wrapper> &get_instance ();

        static void init_instance ();

        void magic (std::string_view m) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::string_view magic () const MANAPIHTTP_NOEXCEPT;

        bool Cancel(TaskHandle handle) override;

        void enable_threadpool (bool status) MANAPIHTTP_NOEXCEPT;

        ConnectionHandle Connect(OnConnectCallback on_connect,
            const ResolvedAddress &addr,
            const grpc_event_engine::experimental::EndpointConfig &args,
            grpc_event_engine::experimental::MemoryAllocator memory_allocator,
            Duration timeout) override;

        void Run(resolve_callback_t<> closure) override;

        void Run(Closure *closure) override;

        bool CancelConnect(ConnectionHandle handle) override;

        absl::StatusOr<std::unique_ptr<Listener>> CreateListener(Listener::AcceptCallback on_accept,
            resolve_callback_t<absl::Status> on_shutdown,
            const grpc_event_engine::experimental::EndpointConfig &config,
            std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) override;

        TaskHandle RunAfter(Duration when, Closure *closure) override;

        TaskHandle RunAfter(Duration when, resolve_callback_t<> closure) override;

        bool IsWorkerThread() override;
#if MANAPIHTTP_GRPC_SINCE_AT(1,57,0)
        absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(const DNSResolver::ResolverOptions &options) override;
#else
        std::unique_ptr<DNSResolver> GetDNSResolver(const DNSResolver::ResolverOptions &options) override;
#endif
    };
}

#endif