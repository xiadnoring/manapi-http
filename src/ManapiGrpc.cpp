#include <memory>

#include "ManapiGrpc.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiTimerObject.hpp"
#include "ManapiEventLoop.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiDns.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "./include/ManapiInternalGrpc.hpp"
#include "./include/ManapiUtils.hpp"
#include "std/ManapiRef.hpp"


#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/grpcpp.h>
#include <grpc/event_engine/event_engine.h>

enum manapi_grpc_endpoint_flags {
    MANAPI_GRPC_ENDPOINT_WANT_READ = 1,
    MANAPI_GRPC_ENDPOINT_FINISHED = 2
};

struct wgrpc_connection_data_t {
    std::shared_ptr<manapi::ev::connect> connect;
    std::unique_ptr<manapi::timer> timer;
};

struct wgrpc_thread_local_storage_t {
    std::map<std::size_t, std::uintptr_t> wgrpc_tasks_exists;
    std::map<std::size_t, std::uintptr_t> wgrpc_connect_exists;
    std::set<manapi::net::wgrpc::net_listener *> wgrpc_tcp_listeners;
    uint32_t current_connect_index = 0;
    uint32_t current_task_index = 0;
    std::shared_ptr<manapi::async::cthread> ctx;
};

struct manapi::net::wgrpc::server_ctx::data_t {
    multithread_storage ms;
    //async::shared_cthread ctx;
};

struct manapi::net::wgrpc::server::data_t {
    server_ctx ctx;
    std::shared_ptr<multithread_storage::worker_t> worker;
    manapi::json data;
    std::unique_ptr<grpc::ServerBuilder> builder;
    std::unique_ptr<grpc::Server> server;
    std::shared_ptr<wgrpc::config> config;
    std::size_t finishid;
};

enum manapi_engine_flags {
    MANAPI_ENGINE_FLAG_ENABLE_THREADPOOL = 1
};

thread_local wgrpc_thread_local_storage_t wgrpc_storage;

struct wgrpc_current_ctx_deleter_t {
    wgrpc_current_ctx_deleter_t (std::shared_ptr<manapi::async::cthread> ctx) {
        assert(!wgrpc_storage.ctx);
        wgrpc_storage.ctx = std::move(ctx);
    }

    ~wgrpc_current_ctx_deleter_t () {
        wgrpc_storage.ctx = nullptr;
    }
};

std::map<std::size_t, std::uintptr_t>::iterator wgrpc_tasks_find (std::intptr_t keys[2]) {
    return wgrpc_storage.wgrpc_tasks_exists.find(keys[1]);
}

std::map<std::size_t, std::uintptr_t>::iterator wgrpc_connect_find (std::intptr_t keys[2]) {
    return wgrpc_storage.wgrpc_connect_exists.find(keys[1]);
}

void unbind_net_listener (manapi::ev::shared_tcp conn, manapi::async::shared_cthread ev, absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    try {
        auto &s = manapi::async::internal::current_();

        if (!conn)
            return;

        if (s == ev) {
            std::unique_ptr<decltype(on_shutdown)> cb{nullptr};
            MANAPIHTTP_MUST_ALLOC_START
            cb = std::make_unique<decltype(on_shutdown)>(nullptr);
            MANAPIHTTP_MUST_ALLOC_END

            *cb = std::move(on_shutdown);

            MANAPIHTTP_MUST_ALLOC_START
            ev->eventloop()->stop_callback (conn, [cb = std::move(cb)] (const manapi::ev::shared_tcp &w) mutable
                -> void {
                if (cb && *cb) {
                    cb->operator()(absl::OkStatus());
                }
            });
            MANAPIHTTP_MUST_ALLOC_END
            ev->eventloop()->stop_watcher(std::move(conn));

            return;
        }

        struct data_unbind_net_mx_t {
            const manapi::async::shared_cthread &ctx;
            std::mutex mx;
            manapi::ev::shared_tcp *conn_;
            manapi::async::shared_cthread *ev_;
            decltype(on_shutdown) *on_shutdown_;
            std::exception_ptr err;
        }
        data{s};

        data.conn_ = &conn;
        data.ev_ = &ev;
        data.on_shutdown_ = &on_shutdown;

        {
            std::lock_guard<std::mutex> lk (data.mx);

            ev->eventloop()->custom_callback([&data] (manapi::event_loop *ev)
                -> void {
                try {
                    unbind_net_listener(*data.conn_, *data.ev_, std::move(*data.on_shutdown_));
                }
                catch (...) {
                    data.err = std::current_exception();
                }
                manapi::event_loop::unlock(data.ctx, data.mx);
            }).unwrap();

            manapi::event_loop::lock(data.ctx, data.mx);
        }

        if (data.err)
            std::rethrow_exception(std::move(data.err));
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc:Close tcp listener failed due to %s", e.what());
    }
}

bool task_handle_cancel (grpc_event_engine::experimental::EventEngine::TaskHandle &handle) {
    auto it = wgrpc_tasks_find(handle.keys);
    if (it == wgrpc_storage.wgrpc_tasks_exists.end())
        return false;

    /* timer */
    std::unique_ptr<manapi::timer> timer (reinterpret_cast<manapi::timer *> (it->second));
    wgrpc_storage.wgrpc_tasks_exists.erase(it);


    if (!timer)
        return false;

    timer->stop();

    return true;
}

bool task_handle_cancel (manapi::async::cthread *t, std::size_t idx) {
    grpc_event_engine::experimental::EventEngine::TaskHandle handle{};
    handle.keys[0] = reinterpret_cast<std::intptr_t>(t);
    handle.keys[1] = static_cast<std::intptr_t>(idx);
    return task_handle_cancel(handle);
}

bool task_connect_cancel (grpc_event_engine::experimental::EventEngine::ConnectionHandle &keys) {
    auto it = wgrpc_connect_find(keys.keys);
    if (it == wgrpc_storage.wgrpc_connect_exists.end())
        return false;

    std::unique_ptr<wgrpc_connection_data_t> p (reinterpret_cast<wgrpc_connection_data_t *> (it->second));
    wgrpc_storage.wgrpc_connect_exists.erase(it);

    if (!p)
        return true;

    if (p->connect && p->connect->is_active()) {
        p->connect->unbind();
    }

    if (p->timer) {
        p->timer->stop();
    }

    return true;
}

bool task_connect_cancel (manapi::async::cthread *t, std::size_t idx) {
    grpc_event_engine::experimental::EventEngine::ConnectionHandle handle{};
    handle.keys[0] = reinterpret_cast<std::intptr_t>(t);
    handle.keys[1] = static_cast<std::intptr_t>(idx);
    return task_connect_cancel(handle);
}


bool manapi::net::wgrpc::event_engine_wrapper::Cancel(TaskHandle handle) {
    auto &ctx = manapi::async::internal::current_();

    if (reinterpret_cast <std::intptr_t>(ctx.get()) == handle.keys[0]) {
        return task_handle_cancel(handle);
    }

    struct data_cancel_t {
        const async::shared_cthread &ctx;
        event_engine_wrapper *engine;
        std::mutex mx;
        std::exception_ptr err;
        TaskHandle *handle_;
        bool res;
    }
    data{ctx};

    data.engine = this;
    data.handle_ = &handle;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        (reinterpret_cast<async::cthread *>(handle.keys[0]))->eventloop()->custom_callback([&data] (event_loop *ev)
            -> void {
            try { data.res = data.engine->Cancel(*data.handle_); }
            catch (...) { data.err = std::current_exception(); }
            manapi::event_loop::unlock(data.ctx, data.mx);
        }).unwrap();

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return data.res;
}

void manapi::net::wgrpc::event_engine_wrapper::enable_threadpool(bool status) MANAPIHTTP_NOEXCEPT {
    if (status) {
        this->flags |= MANAPI_ENGINE_FLAG_ENABLE_THREADPOOL;
    }
    else if (this->flags & MANAPI_ENGINE_FLAG_ENABLE_THREADPOOL) {
        this->flags ^= MANAPI_ENGINE_FLAG_ENABLE_THREADPOOL;
    }
}


manapi::net::wgrpc::config::config(const manapi::json &n) {
    this->buffer_size = config::get_config_param<std::size_t>(n, "buffer_size", 4096);
    this->max_buffered_size = config::get_config_param<std::size_t>(n, "max_buffered_size", 65536);
    this->ssl = config::get_config_object_param(n, "ssl", json::object());
    this->tcp_backlog = config::get_config_param(n, "tcp_backlog", 10);
}

manapi::net::wgrpc::net_listener::net_listener(absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    this->on_shutdown = std::move(on_shutdown);
    this->ev = manapi::async::current();
}

manapi::net::wgrpc::net_listener::~net_listener() {
    net_listener::shutdown(this, std::move(this->connection), std::move(this->on_shutdown), std::move(this->ev));
}

absl::StatusOr<int> manapi::net::wgrpc::net_listener::Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) {
    if (!this->connection)
        return absl::InternalError("wgrpc:Tcp wasn't configured");

    int bind_flags = 0;
#if defined(__unix__) && !defined(__APPLE__)
    bind_flags |= ev::TCP_REUSEPORT;
#endif
    auto res = this->connection->s_bind(addr.address(), bind_flags);
    if (!res)
        return static_cast<int>(reinterpret_cast <const sockaddr_in *> (addr.address())->sin_port);
    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s due to %s", "wgrpc:Bind failed", ev::strerror(res));
    return absl::InternalError("wgrpc:Bind failed");
}

absl::Status manapi::net::wgrpc::net_listener::Start() {
    if (!this->connection)
        return absl::InternalError("wgrpc:Tcp wasn't configured");

    if (this->connection->listen(10)) {
        return absl::InternalError("wgrpc:Tcp listen failed");
    }

    return absl::OkStatus();
}

void manapi::net::wgrpc::net_listener::shutdown(net_listener *id, manapi::ev::shared_tcp conn, absl::AnyInvocable<void(absl::Status)> on_shutdown_cb, async::shared_cthread ev) MANAPIHTTP_NOEXCEPT  {
    try {
        if (conn) {
            auto &s = async::internal::current_();
            if (s == ev) {
                wgrpc_storage.wgrpc_tcp_listeners.erase(id);
                unbind_net_listener(std::move(conn), ev, std::move(on_shutdown_cb));
            }
            else {
                ev->eventloop()->custom_callback([id, conn = std::move(conn), on_shutdown_cb = std::move(on_shutdown_cb)]
                        (manapi::event_loop *ev) mutable -> void {
                    net_listener::shutdown(id, std::move(conn), std::move(on_shutdown_cb), manapi::async::current());
                });
            }
        }
        else if (on_shutdown_cb) {
            on_shutdown_cb (absl::OkStatus());
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s:%s failed due to %s", "wgrpc", "shutdown", e.what());
    }
}

manapi::status manapi::net::wgrpc::net_listener::set(ev::shared_tcp connection) {
    try {
        auto res = wgrpc_storage.wgrpc_tcp_listeners.insert(this).second;
        assert(res);
    }
    catch (std::exception const &) {
        manapi::async::current()->eventloop()->stop_watcher(std::move(connection));
        return status_resource_exhausted();
    }
    this->connection = std::move(connection);
    return status_ok();

}

const manapi::ev::shared_tcp & manapi::net::wgrpc::net_listener::conn() const {
    return this->connection;
}

manapi::net::wgrpc::dns_resolved::dns_resolved(event_engine_wrapper *engine) {
    this->engine = engine;
}

void manapi::net::wgrpc::dns_resolved::LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) {
#if true || MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
    auto ctx = manapi::async::internal::current_();
    if (!ctx) {
        ctx = wgrpc_storage.ctx;
    }

    std::move_only_function<void()> cb = [on_resolve = std::move(on_resolve), name, default_port, ctx] (/*const ev::shared_work &w*/) mutable  -> void {
        wgrpc_current_ctx_deleter_t deleter (ctx);

        ::addrinfo hints{};
        ::addrinfo *result = nullptr, *resp;
        std::string_view host1;
        std::string_view port1;
        bool has_port;
        try {
            // parse name, splitting it into host and port parts
            http::split_http_port (name, host1, port1, has_port);
            if (host1.empty()) {
                return on_resolve(absl::InvalidArgumentError("getaddrinfo:Unparsable name"));
            }
            if (port1.empty()) {
                if (default_port.empty()) {
                    return on_resolve(absl::InvalidArgumentError("getaddrinfo:No port in name or default_port argument"));
                }
                port1 = default_port;
            }
            // Call getaddrinfo
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;      // ipv4 or ipv6
            hints.ai_socktype = SOCK_STREAM;  // stream socket
            hints.ai_flags = AI_PASSIVE;      // for wildcard IP address

            std::string host {host1};
            std::string port {port1};

            auto res = ::getaddrinfo(host.data(), port.data(), &hints, &result);
            if (res) {
                // Retry if well-known service name is recognized
                const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
                for (auto & i : svc) {
                    if (port == i[0]) {
                        res = ::getaddrinfo(host.data(), i[1], &hints, &result);
                        break;
                    }
                }
            }
            if (res) {
                return on_resolve(absl::UnknownError("getaddrinfo:Address lookup failed"));
            }
            // Success path: fill in addrs
            std::vector<grpc_event_engine::experimental::EventEngine::ResolvedAddress> addresses;
            for (resp = result; resp != nullptr; resp = resp->ai_next) {
                addresses.emplace_back(resp->ai_addr, resp->ai_addrlen);
            }
            ev::getaddrinfo::free(std::exchange(result, nullptr));
            on_resolve(std::move(addresses));
            return;
        }
        catch (std::exception const &e) {
            ev::getaddrinfo::free(std::exchange(result, nullptr));
            manapi_log_error("%s due to %s", "wgrpc:look up failed", e.what());
        }
        on_resolve(absl::UnknownError("wgrpc:look up failed"));
    };

    if (!(this->engine->flags & MANAPI_ENGINE_FLAG_ENABLE_THREADPOOL )
        || (!ctx || ctx->threadpool()->size() == 0)) {
        std::thread (std::move(cb)).detach();
    }
    else {
        ctx->threadpool()->append_task(std::move(cb));
    }

#else
    this->engine->Run([on_resolve = std::move(on_resolve), name, default_port] () mutable -> void {
        manapi::async::run(manapi::async::invoke(
            [] (LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) mutable
            -> manapi::future<> {
                ::addrinfo hints{};
                ::addrinfo *result = nullptr, *resp;
                std::string_view host1;
                std::string_view port1;
                bool has_port;
                try {
                    // parse name, splitting it into host and port parts
                    http::split_http_port (name, host1, port1, has_port);
                    if (host1.empty()) {
                        co_return on_resolve(absl::InvalidArgumentError("getaddrinfo:Unparsable name"));
                    }
                    if (port1.empty()) {
                        if (default_port.empty()) {
                            co_return on_resolve(absl::InvalidArgumentError("getaddrinfo:No port in name or default_port argument"));
                        }
                        port1 = default_port;
                    }
                    // Call getaddrinfo
                    memset(&hints, 0, sizeof(hints));
                    hints.ai_family = AF_UNSPEC;      // ipv4 or ipv6
                    hints.ai_socktype = SOCK_STREAM;  // stream socket
                    hints.ai_flags = AI_PASSIVE;      // for wildcard IP address

                    std::string host {host1};
                    std::string port {port1};

                    auto res = co_await dns::getaddrinfo(host.data(), port.data(), &hints, &result);
                    if (res) {
                        // Retry if well-known service name is recognized
                        const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
                        for (auto & i : svc) {
                            if (port == i[0]) {
                                res = co_await dns::getaddrinfo(host.data(), i[1], &hints, &result);
                                break;
                            }
                        }
                    }
                    if (res) {
                        co_return on_resolve(absl::UnknownError("getaddrinfo:Address lookup failed"));
                    }
                    // Success path: fill in addrs
                    std::vector<grpc_event_engine::experimental::EventEngine::ResolvedAddress> addresses;
                    for (resp = result; resp != nullptr; resp = resp->ai_next) {
                        addresses.emplace_back(resp->ai_addr, resp->ai_addrlen);
                    }
                    ev::getaddrinfo::free(std::exchange(result, nullptr));
                    on_resolve(std::move(addresses));
                    co_return;
                }
                catch (std::exception const &e) {
                    ev::getaddrinfo::free(std::exchange(result, nullptr));
                    manapi_log_error("%s due to %s", "wgrpc:look up failed", e.what());
                }
                on_resolve(absl::UnknownError("wgrpc:look up failed"));
        }, std::move(on_resolve), name, default_port));
    });
#endif
}

void manapi::net::wgrpc::dns_resolved::LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) {
    this->engine->Run([on_resolve = std::move(on_resolve)] () mutable
        -> void {
        on_resolve(absl::UnimplementedError("not already"));
    });
}

void manapi::net::wgrpc::dns_resolved::LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) {
    this->engine->Run([on_resolve = std::move(on_resolve)] () mutable
        -> void {
        on_resolve(absl::UnimplementedError("not already"));
    });
}
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
void * manapi::net::wgrpc::dns_resolved::QueryExtension(absl::string_view basic_string_view) {
    return DNSResolver::QueryExtension(basic_string_view);
}
#endif

manapi::net::wgrpc::net_endpoint::net_endpoint(manapi::ev::shared_tcp conn,
                                               std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr,
                                               std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr,
                                               grpc_event_engine::experimental::MemoryAllocator memory_allocator) {

    auto &ctx = manapi::async::current();
    if (ctx) {
        this->flags = 0;
        this->ev = ctx;
        this->local_addr = std::move(local_addr);
        this->conn = std::move(conn);
        this->peer_addr = std::move(peer_addr);
        this->memory_allocator = std::move(memory_allocator);
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        this->metric = nullptr;
#endif
        this->init_();
    }
}

void manapi::net::wgrpc::net_endpoint::init_() {
    auto &ctx = manapi::async::current();

    ctx->eventloop()->read_callback(this->conn,
        [this] (const std::shared_ptr<manapi::ev::tcp> &, ssize_t nread, const manapi::ev::buff_t *buf) -> void {
            bytebuffer buffer;

            if (buf && buf->base) {
                buffer = bytebuffer (buf->base, buf->len, bytebuffer::BYTEBUFFER_FLAG_OBJECT_POOL);
            }

            if (nread < 0 || !buf) {
                /* error */
                this->flags |= MANAPI_GRPC_ENDPOINT_FINISHED;
                if (this->flags & MANAPI_GRPC_ENDPOINT_WANT_READ)
                    this->on_read(absl::AbortedError("grpc:Tcp connection was closed"));

                return;
            }

            if (!nread)
                return;


            std::size_t cursor = 0;
            while (cursor != nread) {
                auto const copy = std::min<std::size_t>(nread - cursor, 4096);
                grpc_event_engine::experimental::Slice slice (this->memory_allocator.MakeSlice(copy));
                memcpy ((char*)slice.data(), buf->base + cursor, copy);
                this->buffer.Append(std::move(slice));
                cursor += copy;
            }

            if (this->flags & MANAPI_GRPC_ENDPOINT_WANT_READ) {
                while (this->buffer.Count())
                    this->on_read_buffer->Append(this->buffer.TakeFirst());

                if (this->on_read_hints_bytes <= nread) {
                    this->on_read_hints_bytes = 0;
                    this->flags ^= MANAPI_GRPC_ENDPOINT_WANT_READ;
                    this->on_read(absl::OkStatus());
                }
                else
                    this->on_read_hints_bytes -= nread;

            }
            else {
                if (this->buffer.Length() >= 65536 && this->conn->is_active())
                    this->conn->read_stop();
            }
        });

    ctx->eventloop()->alloc_callback(this->conn,
        [this] (const std::shared_ptr<manapi::ev::tcp> &, size_t suggested_size, manapi::ev::buff_t *buf) MANAPIHTTP_NOEXCEPT
        -> void {
            auto size = static_cast<ssize_t>(suggested_size);
            if (!(this->flags & MANAPI_GRPC_ENDPOINT_WANT_READ)) {
                size = std::min<ssize_t>(65536L - this->buffer.Length(), 65536);
                if (size <= 0)
                    return;
            }

            auto bufres = manapi::async::current()->memory_fabric().buffer(size);
            if (bufres.ok()) {
                auto buffer = bufres.unwrap();
                buf->len = buffer.realsize();
                buf->base = static_cast<char *>(buffer.release());
            }
    });
}

#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)

std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint::TelemetryInfo> manapi::net::wgrpc::net_endpoint::GetTelemetryInfo() const {
    return this->metric;
}

#endif

void unbind_net_endpoint (manapi::ev::shared_tcp conn, manapi::async::shared_cthread ev) MANAPIHTTP_NOEXCEPT {
    try {
        auto &s = manapi::async::internal::current_();
        if (s == ev) {
            if (conn->is_active())
                conn->read_stop();

            s->eventloop()->stop_watcher(std::move(conn));
            return;
        }

        ev->eventloop()->custom_callback([conn = std::move(conn)] (manapi::event_loop *ev) mutable
            -> void {
            unbind_net_endpoint (std::move(conn), manapi::async::current());
        }).unwrap();
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc: close conn failed due to %s", e.what());
    }
}

manapi::net::wgrpc::net_endpoint::~net_endpoint() {
    unbind_net_endpoint (this->conn, this->ev);
}

bool manapi::net::wgrpc::net_endpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer,
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
ReadArgs args
#else
const ReadArgs *args
#endif
) {
    auto &ctx = manapi::async::internal::current_();

    if (ctx == this->ev) {
        auto const already = this->buffer.Length();
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
        auto const read_hint_bytes = args.read_hint_bytes();
#else
        auto const read_hint_bytes = args->read_hint_bytes;
#endif

        bool const want_more = already < read_hint_bytes;

        assert(on_read);

        while (this->buffer.Count())
            buffer->Append(this->buffer.TakeFirst());

        if (this->flags & MANAPI_GRPC_ENDPOINT_FINISHED) {
            if (already)
                return true;

            ctx->etaskpool()->append_task([on_read = std::move(on_read)] () mutable -> void {
                on_read (absl::AbortedError("grpc:Tcp connection was closed"));
            });
        }
        else {
            if (!this->conn->is_active())
                this->conn->read_start();

            if (!want_more)
                return true;

            this->on_read_buffer = buffer;
            this->on_read = std::move(on_read);
            this->on_read_hints_bytes = read_hint_bytes - already;
            assert(this->on_read);
            this->flags |= MANAPI_GRPC_ENDPOINT_WANT_READ;
        }


        return false;
    }


    struct data_read_mx_t {
        const async::shared_cthread &ctx;
        net_endpoint *endpoint;
        decltype(on_read) *on_read_;
        decltype(buffer) *buffer_;
        decltype(args) *args_;
        std::mutex mx;
        bool res;
        std::exception_ptr err;
    } data_read{ctx};

    data_read.on_read_ = &on_read;
    data_read.endpoint = this;
    data_read.buffer_ = &buffer;
    data_read.args_ = &args;

    {
        std::lock_guard<std::mutex> lk (data_read.mx);

        this->ev->eventloop()->custom_callback([&data_read] (event_loop *ev) mutable
            -> void {
            try {
                data_read.res = data_read.endpoint->Read(std::move(*data_read.on_read_), std::move(*data_read.buffer_),
                    std::move(*data_read.args_));
            }
            catch (...) {
                data_read.err = std::current_exception();
            }
            manapi::event_loop::unlock(data_read.ctx, data_read.mx);
        }).unwrap();

        manapi::event_loop::lock(data_read.ctx, data_read.mx);
    }

    if (data_read.err)
        std::rethrow_exception(data_read.err);

    return data_read.res;
}
bool manapi::net::wgrpc::net_endpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data,
#if MANAPIHTTP_GRPC_SINCE_AT(1,73,0)
WriteArgs args
#else
const WriteArgs *args
#endif
) {

    auto &ctx = manapi::async::internal::current_();

    if (ctx == this->ev) {
        std::size_t carret = 0;
        ssize_t rhs = 0;

        for (; carret < data->Count(); ++carret) {
            auto &a = data->operator[](carret);
            rhs = this->conn->try_write(a.data(), a.size());
            if (rhs < 0) {
                rhs = 0;
                break;
            }
            if (rhs != a.size()) {
                break;
            }
        }

        if (carret == data->Count()) {
            return true;
        }

        std::unique_ptr<manapi::ev::buff_t, manapi::ev::buffer_deleter> store;
        auto const nbuff = data->Count() - carret;
        store.reset(new manapi::ev::buff_t[nbuff]);

        for (std::size_t i = 0; carret < data->Count(); ++carret, ++i) {
            auto &c = store.get()[i];
            c.base = (char*)((data)->operator[](carret).data()) + rhs;
            c.len = data->operator[](carret).size() - rhs;

            rhs = 0;
        }

        auto const buffs = store.get();
        ctx->eventloop()->create_watcher_write(this->conn.get(),
            [store = std::move(store), on_writable = std::move(on_writable)]
            (const manapi::ev::shared_write &w, int status) mutable
            -> void {
                if (status) {
                    switch (status) {
                        case UV_ECANCELED: on_writable(absl::CancelledError()); break;
                        default: on_writable(absl::UnknownError("bad result")); break;
                    }

                    return;
                }

                on_writable(absl::OkStatus());
            },
            buffs, nbuff
        );

        return false;
    }

    struct data_write_mx_t {
        const async::shared_cthread &ctx;
        net_endpoint *endpoint;
        decltype(on_writable) *on_writable_;
        decltype(data) *data_;
        decltype(args) *args_;
        std::mutex mx;
        bool res;
        std::exception_ptr err;
    } data_write{ctx};

    data_write.on_writable_ = &on_writable;
    data_write.endpoint = this;
    data_write.data_ = &data;
    data_write.args_ = &args;

    {
        std::lock_guard<std::mutex> lk (data_write.mx);

        this->ev->eventloop()->custom_callback([&data_write] (event_loop *ev) mutable
            -> void {
            try {
                data_write.res = data_write.endpoint->Write(std::move(*data_write.on_writable_), std::move(*data_write.data_),
                    std::move(*data_write.args_));
            }
            catch (...) {
                data_write.err = std::current_exception();
            }
            manapi::event_loop::unlock(data_write.ctx, data_write.mx);
        }).unwrap();

        manapi::event_loop::lock(data_write.ctx, data_write.mx);
    }

    if (data_write.err)
        std::rethrow_exception(std::move(data_write.err));

    return data_write.res;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::net::wgrpc::net_endpoint::GetLocalAddress() const {
    return *this->local_addr;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::net::wgrpc::net_endpoint::GetPeerAddress() const {
    return *this->peer_addr;
}


manapi::net::wgrpc::event_engine_wrapper::event_engine_wrapper() : grpc_event_engine::experimental::EventEngine() {
    this->primary = manapi::async::current();
    this->flags = 0;
}

manapi::net::wgrpc::event_engine_wrapper::~event_engine_wrapper() = default;

void manapi::net::wgrpc::event_engine_wrapper::magic(std::string_view m) MANAPIHTTP_NOEXCEPT {
    this->magic_ = m;
}

std::string_view manapi::net::wgrpc::event_engine_wrapper::magic() const MANAPIHTTP_NOEXCEPT {
    return this->magic_;
}

grpc_event_engine::experimental::EventEngine::ConnectionHandle manapi::net::wgrpc::event_engine_wrapper::Connect(
    OnConnectCallback on_connect, const ResolvedAddress &addr,
    const grpc_event_engine::experimental::EndpointConfig &args,
    grpc_event_engine::experimental::MemoryAllocator memory_allocator, Duration timeout) {

    auto &ctx = manapi::async::internal::current_();

    if (ctx) {
        std::unique_ptr<wgrpc_connection_data_t> data (new wgrpc_connection_data_t{});
        std::unique_ptr<manapi::timer> timer (new manapi::timer{});

        struct wgrpc_connect_data_t {
            int refcnt;
            std::size_t index;
            manapi::timer *timer;
            OnConnectCallback on_connect_cb;
            grpc_event_engine::experimental::MemoryAllocator memory_allocator;
            std::shared_ptr<ev::connect> connect;
        };

        manapi::reference ref (new wgrpc_connect_data_t (0, 0, timer.get(), std::move(on_connect), std::move(memory_allocator)));

        auto wres = manapi::async::current()->eventloop()->connect_tcp (addr.address(),
            [ref]
            (const std::shared_ptr<manapi::ev::tcp> &w, int status) mutable
            -> void {
                std::unique_ptr<wgrpc_connection_data_t> conn_data{nullptr};

                auto conn_row = wgrpc_storage.wgrpc_connect_exists.find(ref->index);
                if (conn_row != wgrpc_storage.wgrpc_connect_exists.end()) {
                    conn_data.reset(reinterpret_cast<wgrpc_connection_data_t *>(conn_row->second));
                    wgrpc_storage.wgrpc_connect_exists.erase(conn_row);
                }

                if (!ref->on_connect_cb) {
                    return;
                }

                try {
                    ref->timer->stop();

                    if (status) {
                        /* error */
                        switch (status) {
                            case manapi::ev::ERR_AI_CANCELED: ref->on_connect_cb (absl::CancelledError()); break;
                            default: ref->on_connect_cb(absl::UnknownError("wgrpc:Something gets wrong")); break;
                        }
                        ref->on_connect_cb = nullptr;
                        return;
                    }

                    sockaddr_storage sock_addr{};
                    int sock_len = sizeof (sock_addr);
                    w->getpeername(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

                    auto peer = std::make_unique<grpc_event_engine::experimental::EventEngine::ResolvedAddress>(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

                    sock_len = sizeof (sock_addr);
                    /* think about it */
                    memset(&sock_addr, '\0', sock_len);
                    w->getsockname(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

                    auto local_addr = std::make_shared<grpc_event_engine::experimental::EventEngine::ResolvedAddress>(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

                    auto endpoint = std::make_unique<wgrpc::net_endpoint>(w, std::move(peer), std::move(local_addr), std::move(ref->memory_allocator));

                    ref->on_connect_cb(std::move(endpoint));
                    ref->on_connect_cb = nullptr;
                    return;
                }
                catch (std::exception const &e) {
                    manapi::async::current()->logger()->error(manapi::ERR_INTERNAL, "creating connection failed due to {}",
                        e.what());
                }

                ref->on_connect_cb(absl::InternalError("creating connection failed"));
                ref->on_connect_cb = nullptr;
            },
            nullptr, nullptr);

        if (!wres) {
            ctx->etaskpool()->append_static_task([msg = wres.sysmsg(), ref] ()
                mutable -> void {
                if (ref->on_connect_cb) {
                    ref->on_connect_cb (absl::AbortedError(msg));
                    ref->on_connect_cb = nullptr;
                }
            });
        }

        std::shared_ptr<ev::tcp> conn;
        if (wres.ok()) {
            auto result = wres.unwrap();
            ref->connect = std::move(result.first);
            conn = std::move(result.second);
            auto res = manapi::async::current()->timerpool()->append_timer_sync(
                std::max(1UL, static_cast<std::size_t>(timeout.count() / 1000000)), [ref] (manapi::timer t) mutable
                -> void {
                std::unique_ptr<wgrpc_connection_data_t> conn_data{nullptr};

                auto conn_row = wgrpc_storage.wgrpc_connect_exists.find(ref->index);
                if (conn_row != wgrpc_storage.wgrpc_connect_exists.end()) {
                    conn_data.reset(reinterpret_cast<wgrpc_connection_data_t *>(conn_row->second));
                    wgrpc_storage.wgrpc_connect_exists.erase(conn_row);
                }
                if (ref->connect && ref->connect->is_active()) {
                    manapi::async::current()->eventloop()->stop_watcher(std::move(ref->connect));
                }
                if (ref->on_connect_cb) {
                    ref->on_connect_cb (absl::DeadlineExceededError("Conn:Timeout"));
                    ref->on_connect_cb = nullptr;
                }
            });

            *timer = res.unwrap();
        }

        ConnectionHandle handle{};

        data->connect = ref->connect;
        data->timer = std::move(timer);

        while (true) {
            std::size_t const time = std::chrono::steady_clock::now().time_since_epoch().count();
            std::size_t indx = (static_cast<std::size_t>(wgrpc_storage.current_connect_index++) << 32) | (time & 0x0000FFFF);
            if (wgrpc_storage.current_connect_index == std::numeric_limits<uint32_t>::max()) {
                wgrpc_storage.current_connect_index = 0;
            }

            try {
                auto insert_res = wgrpc_storage.wgrpc_connect_exists.insert(
                    {indx, reinterpret_cast<std::uintptr_t>(data.get())});

                if (!insert_res.second) {
                    continue;
                }

                ref->index = indx;

                data.release();
            }
            catch (std::exception const &) {
                auto &ev = manapi::async::current()->eventloop();
                ev->stop_watcher(ref->connect);
                ev->stop_watcher(std::move(conn));
            }

            handle.keys[0] = reinterpret_cast<std::intptr_t>(ctx.get());
            handle.keys[1] = static_cast<std::intptr_t>(indx);

            return handle;
        }
    }

    auto cur = (wgrpc_storage.ctx);
    if (!cur) {
        cur = this->primary;
    }

    struct data_connect_mx_t {
        const async::shared_cthread &ctx;
        std::mutex mx;
        std::exception_ptr err;
        decltype(on_connect) *on_connect_;
        const ResolvedAddress* addr_;
        const grpc_event_engine::experimental::EndpointConfig *args_;
        decltype(memory_allocator) *memory_allocator_;
        decltype(timeout) *timeout_;
        event_engine_wrapper *engine;
        ConnectionHandle res;
    }
    data{ctx};

    data.engine = this;
    data.addr_ = &addr;
    data.args_ = &args;
    data.on_connect_ = &on_connect;
    data.memory_allocator_ = &memory_allocator;
    data.timeout_ = &timeout;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        cur->eventloop()->custom_callback([&data] (manapi::event_loop *ev) -> void {
            try { data.res = data.engine->Connect(std::move(*data.on_connect_), *data.addr_, *data.args_, std::move(*data.memory_allocator_), *data.timeout_); }
            catch (...) { data.err = std::current_exception(); }
            manapi::event_loop::unlock(data.ctx, data.mx);
        });

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return std::move(data.res);
}

void manapi::net::wgrpc::event_engine_wrapper::Run(absl::AnyInvocable<void()> closure) {
    auto &ctx = manapi::async::internal::current_();

    if (ctx)
        ctx->etaskpool()->append_task(std::move(closure));
    else {
        auto cur = wgrpc_storage.ctx;
        if (!cur) {
            cur = this->primary;
        }

        {
            cur->eventloop()->custom_callback(
                [closure = std::move(closure)] (manapi::event_loop *ev) mutable -> void {
                try {
                    ev->taskpool()->append_task(std::move(closure));
                }
                catch (std::exception const &e) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s" ,
                        "wgrpc", "Run", e.what());
                }
            });
        }
    }
}

void manapi::net::wgrpc::event_engine_wrapper::Run(Closure *closure) {
    auto &ctx = manapi::async::internal::current_();
    //assert(ctx);
    if (ctx)
        ctx->etaskpool()->append_task([closure] ()
            -> void { closure->Run(); });
    else {
        auto cur = wgrpc_storage.ctx;
        if (!cur) {
            cur = this->primary;
        }

        {
            cur->eventloop()->custom_callback(
                [closure] (manapi::event_loop *ev) mutable -> void {
                try {
                    ev->taskpool()->append_static_task([closure] ()
                        -> void { closure->Run(); });
                }
                catch (std::exception const &e) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s",
                        "wgrpc", "Run", e.what());
                }
            });
        }
    }
}

bool manapi::net::wgrpc::event_engine_wrapper::CancelConnect(ConnectionHandle handle) {
    auto &ctx = manapi::async::internal::current_();

    if (reinterpret_cast<std::intptr_t>(ctx.get()) == handle.keys[0]) {
        return task_connect_cancel (handle);
    }

    struct data_cancel_connect_t {
        const async::shared_cthread &ctx;
        std::mutex mx;
        bool res;
        ConnectionHandle *handle;
        event_engine_wrapper *engine;
        std::exception_ptr err;
    } data{ctx};

    data.engine = this;
    data.handle = &handle;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        (reinterpret_cast <async::cthread*>(handle.keys[0]))->eventloop()->custom_callback([&data] (event_loop *ev)
            -> void {
            try {
                data.res = data.engine->CancelConnect(*data.handle);
            }
            catch (...) {
                data.err = std::current_exception();
            }
            manapi::event_loop::unlock(data.ctx, data.mx);
        }).unwrap();

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return data.res;
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>> manapi::
net::wgrpc::event_engine_wrapper::CreateListener(Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown, const grpc_event_engine::experimental::EndpointConfig &config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) {
    auto &ctx = manapi::async::internal::current_();

    if (ctx) {
        auto b = std::make_unique<net_listener>(std::move(on_shutdown));

        auto local_addr = std::make_shared<grpc_event_engine::experimental::EventEngine::ResolvedAddress> ();
        auto wres = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
            [local_addr, on_accept = std::move(on_accept), memory_allocator_factory = std::move(memory_allocator_factory)]
            (const std::shared_ptr<manapi::ev::tcp> & w, int status) mutable
            -> void {
            auto wres = manapi::async::current()->eventloop()->create_watcher_tcp_connection(nullptr, nullptr);

            if (!wres) {
                return;
            }

            auto conn = wres.unwrap();

            if (conn->accept(w.get())) {
                manapi::async::current()->eventloop()->stop_watcher(std::move(conn));
                return;
            }

            try {
                sockaddr_storage sock_addr{};
                int sock_len = sizeof (sock_addr);
                w->getpeername(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

                auto peer = std::make_unique<grpc_event_engine::experimental::EventEngine::ResolvedAddress>(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

                auto endpoint = std::make_unique<net_endpoint>(std::move(conn), std::move(peer), local_addr, memory_allocator_factory->CreateMemoryAllocator("tcp-endpoint"));
                on_accept (std::move(endpoint), memory_allocator_factory->CreateMemoryAllocator("tcp-handler"));
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                manapi::async::current()->eventloop()->stop_watcher(std::move(conn));
            }
        });

        if (!wres) {
            /*error*/
        }

        b->set(wres.unwrap()).unwrap();

        sockaddr_storage sock_addr{};
        int sock_len = sizeof (sock_addr);
        b->conn()->getsockname(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

        *local_addr = grpc_event_engine::experimental::EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

        return std::move(b);
    }
    auto cur = wgrpc_storage.ctx;
    if (!cur) {
        cur = this->primary;
    }

    struct data_create_listener_mx_t {
        absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>> res;
        const async::shared_cthread &ctx;
        std::mutex mx;
        std::exception_ptr err;
        decltype(on_accept) *on_accept_;
        decltype(on_shutdown) *on_shutdown_;
        const grpc_event_engine::experimental::EndpointConfig *config_;
        decltype(memory_allocator_factory) *memory_;
        event_engine_wrapper *engine;
    }
    data{absl::OkStatus(), ctx};

    data.engine = this;
    data.config_ = &config;
    data.on_accept_ = &on_accept;
    data.on_shutdown_ = &on_shutdown;
    data.memory_ = &memory_allocator_factory;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        cur->eventloop()->custom_callback([&data] (manapi::event_loop *ev) -> void {
            try { data.res = data.engine->CreateListener(std::move(*data.on_accept_), std::move(*data.on_shutdown_),
                *data.config_, std::move(*data.memory_)); }
            catch (...) { data.err = std::current_exception(); }

            manapi::event_loop::unlock(data.ctx, data.mx);
        });

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return std::move(data.res);
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, Closure *closure) {
    auto &ctx = manapi::async::internal::current_();

    if (ctx) {
        TaskHandle task{};
        /* oh no way */
        auto timer = std::make_unique<manapi::timer>();

        struct wgrpc_timer_data_t {
            int refcnt;
            Closure *closure;
            std::size_t index;
        };

        reference<wgrpc_timer_data_t> td (new wgrpc_timer_data_t{});
        td->closure = closure;

        auto const ms = std::max(static_cast<std::size_t>(1),
            static_cast<std::size_t>(when.count() / 1000000));

        auto rhs = ctx->timerpool()->append_timer_sync(ms,
            [td] (manapi::timer timer) -> void {
                auto closure = std::move(td->closure);
                task_handle_cancel(manapi::async::current().get(), td->index);
                try { closure->Run(); }
                catch (std::exception const &e) { manapi_log_error("%s:%s failed due to %s",
                    "wgrpc", "gRPC send a error", e.what()); }
            });

        *timer = rhs.unwrap();

        while (true) {
            std::size_t const time = std::chrono::steady_clock::now().time_since_epoch().count();
            td->index = (static_cast<std::size_t>(wgrpc_storage.current_task_index++) << 32) | (time & 0x0000FFFF);

            if (wgrpc_storage.current_task_index == std::numeric_limits<uint32_t>::max()) {
                wgrpc_storage.current_task_index = 0;
            }


            auto res = wgrpc_storage.wgrpc_tasks_exists.insert(
                {td->index, reinterpret_cast<std::uintptr_t>(timer.get())});

            if (!res.second) {
                continue;
            }

            timer.release();

            task.keys[0] = reinterpret_cast<std::intptr_t> (ctx.get());
            task.keys[1] = static_cast <std::intptr_t>(td->index);

            return task;
        }
    }

    auto cur = wgrpc_storage.ctx;
    if (!cur) {
        cur = this->primary;
    }

    struct data_run_after_mx_t {
        const async::shared_cthread &ctx;
        std::mutex mx;
        std::exception_ptr err;
        TaskHandle res;
        Duration *when_;
        Closure *closure_;
        event_engine_wrapper *engine;
    }
    data{ctx};

    data.closure_ = closure;
    data.when_ = &when;
    data.engine = this;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        cur->eventloop()->custom_callback([&data] (manapi::event_loop *ev) -> void {
            try { data.res = data.engine->RunAfter(*data.when_, data.closure_); }
            catch (...) { data.err = std::current_exception(); }

            manapi::event_loop::unlock(data.ctx, data.mx);
        });

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return data.res;
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, absl::AnyInvocable<void()> closure) {
    auto &ctx = manapi::async::internal::current_();

    if (ctx) {
        TaskHandle task{};
        /* oh no way */
        auto timer = std::make_unique<manapi::timer>();

        auto const ms = std::max(static_cast<std::size_t>(1),
            static_cast<std::size_t>(when.count() / 1000000));

        if (wgrpc_storage.current_task_index == std::numeric_limits<uint32_t>::max()) {
            wgrpc_storage.current_task_index = 0;
        }

        struct wgrpc_timer_data_t {
            int refcnt;
            absl::AnyInvocable<void()> closure;
            std::size_t index;
        };

        manapi::reference<wgrpc_timer_data_t> td (new wgrpc_timer_data_t{});
        td->closure = std::move(closure);

        auto rhs = ctx->timerpool()->append_timer_sync(ms,
            [td] (manapi::timer timer) mutable -> void {
                auto closure = std::move(td->closure);
                task_handle_cancel(manapi::async::current().get(), td->index);
                try { closure (); }
                catch (std::exception const &e) { manapi_log_error(
                    "%s:%s failed due to %s", "wgrpc", "gRPC send a error", e.what()); }
            });

        *timer = rhs.unwrap();

        while (true) {
            auto const time = std::chrono::steady_clock::now().time_since_epoch().count();
            td->index = (static_cast<std::size_t>(wgrpc_storage.current_task_index++) << 32) | (time & 0x0000FFFF);

            auto res = wgrpc_storage.wgrpc_tasks_exists.insert({td->index, reinterpret_cast<std::uintptr_t>(timer.get())});

            if (!res.second) {
                continue;
            }

            timer.release();

            task.keys[0] = reinterpret_cast<std::intptr_t> (ctx.get());
            task.keys[1] = static_cast <std::intptr_t>(td->index);

            return task;
        }
    }

    auto cur = (wgrpc_storage.ctx);
    if (!cur) {
        cur = this->primary;
    }

    struct data_run_after2_mx_t {
        const async::shared_cthread &ctx;
        std::mutex mx;
        std::exception_ptr err;
        TaskHandle res;
        Duration *when_;
        decltype(closure) *closure_;
        event_engine_wrapper *engine;
    }
    data{ctx};

    data.closure_ = &closure;
    data.when_ = &when;
    data.engine = this;

    {
        std::lock_guard<std::mutex> lk (data.mx);

        cur->eventloop()->custom_callback([&data] (manapi::event_loop *ev) -> void {
            try { data.res = data.engine->RunAfter(*data.when_, std::move(*data.closure_)); }
            catch (...) { data.err = std::current_exception(); }

            manapi::event_loop::unlock(data.ctx, data.mx);
        });

        manapi::event_loop::lock(data.ctx, data.mx);
    }

    if (data.err)
        std::rethrow_exception(std::move(data.err));

    return data.res;
}

bool manapi::net::wgrpc::event_engine_wrapper::IsWorkerThread() {
    return !manapi::async::internal::current_();
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::DNSResolver>> manapi::net::wgrpc::event_engine_wrapper::GetDNSResolver(const DNSResolver::ResolverOptions &options) {
    try {
        return std::make_unique<dns_resolved>(this);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Failed to create dns resolved", e.what());
    }
    return absl::UnimplementedError("dns");
}

// const manapi::async::shared_eventloop & manapi::net::wgrpc::event_engine_wrapper::shared_eventloop() const {
//     return this->ev;
// }

manapi::future<manapi::status_or<std::shared_ptr<grpc::ChannelCredentials>>> manapi::net::wgrpc::secure_channel_credentials(std::string certfile) {
    try {
        auto res = co_await manapi::fs::async_read(certfile);
        if (!res.ok())
            co_return res.err();
        grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs=res.unwrap();

        auto ssl_creds = grpc::SslCredentials(ssl_opts);
        co_return std::move(ssl_creds);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:SecureChannelCreds something gets wrong", e.what());
    }

    co_return status_internal("wgrpc:SecureChannelCreds something gets wrong");
}

manapi::net::wgrpc::server_ctx::server_ctx() {
    std::function<void(void *ptr)> deleter = [] (void *ptr)
        -> void { delete static_cast<worker_data_t *>(ptr); };
    this->data_ = std::make_shared<data_t>(multithread_storage (new worker_data_t(), std::move(deleter)));
    //this->data_->ctx = manapi::async::current();
    try {
        auto ev = std::make_shared<manapi::net::wgrpc::event_engine_wrapper>();
        ev->magic("MAGIC_MANAPI");
        grpc_event_engine::experimental::SetDefaultEventEngine(ev);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Set default event loop failed", e.what());
    }
}

manapi::status_or<manapi::net::wgrpc::server_ctx> manapi::net::wgrpc::server_ctx::create () MANAPIHTTP_NOEXCEPT {
    try {
        return server_ctx{};
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::multithread_storage & manapi::net::wgrpc::server_ctx::storage() {
    return this->data_->ms;
}

manapi::status manapi::net::wgrpc::server_ctx::enable_threadpool(bool status) MANAPIHTTP_NOEXCEPT {
    if (this->data_) {
        auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
        if (engine) {
            auto manapi_engine = std::dynamic_pointer_cast<event_engine_wrapper>(engine);
            if (manapi_engine->magic() != "MAGIC_MANAPI") {
                return manapi::status_internal("wgrpc:magic incorrect");
            }
            manapi_engine->enable_threadpool(status);
        }
    }
    return manapi::status_ok();
}

void manapi::net::wgrpc::server_ctx::clean() MANAPIHTTP_NOEXCEPT {
    auto &ctx = manapi::async::internal::current_();
    if (ctx) {
        while (!wgrpc_storage.wgrpc_tasks_exists.empty()) {
            auto it = wgrpc_storage.wgrpc_tasks_exists.begin();
            task_handle_cancel(ctx.get(), it->first);
        }

        while (!wgrpc_storage.wgrpc_connect_exists.empty()) {
            auto it = wgrpc_storage.wgrpc_connect_exists.begin();
            task_connect_cancel(ctx.get(), it->first);
        }
    }
    else {

    }
}

// const manapi::async::shared_cthread & manapi::net::wgrpc::server_ctx::ctx() {
//     return this->data_->ctx;
// }

manapi::net::wgrpc::server::server(wgrpc::server_ctx ctx) {
    this->data_ = std::make_shared<data_t>(std::move(ctx), nullptr);
}

manapi::net::wgrpc::server::server() {
    this->data_ = nullptr;
}

manapi::status_or<manapi::net::wgrpc::server> manapi::net::wgrpc::server::create (wgrpc::server_ctx ctx) MANAPIHTTP_NOEXCEPT {
    try {
        return server(std::move(ctx));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::net::wgrpc::server::~server() = default;

manapi::future<manapi::status> manapi::net::wgrpc::server::config(std::string path) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return status_already_exists("grpc:Server can only be run in a signle instance");

    if (this->data_->worker)
        co_return status_already_exists("wgrpc:Config exists");

    auto res = co_await this->subscribe_();
    if (!res.ok())
        goto err;

    co_await this->data_->ctx.storage().edit_async(this->data_->worker, [&] (manapi::json &n) -> manapi::future<bool> {
        if (!n.is_object())
            n = manapi::json::object();

        bool update = false;

        if (!n.contains("grpc")) {
            auto res_exists = co_await manapi::fs::async_exists(path);
            if (!res_exists.ok() || !res_exists.unwrap())
                co_await manapi::fs::async_write(path, "{}", ev::IRUSR|ev::IWUSR|ev::IXUSR|ev::IRGRP|ev::IWGRP);
            auto res_text = co_await manapi::fs::async_read(path);
            if (res_text.ok()) {
                auto text = res_text.unwrap();
                res = this->setup_config_(manapi::json::parse(text).unwrap(), n);
                if (res.ok())
                    n["grpc_path"] = std::move(path);
            }
            else {
                MANAPIHTTP_LOG("wgrpc:Failed to read data from the grpc_path: {} due to {}:{} {}({}, {})",
                    path, res_text.status_msg(), res_text.message(), res_text.syserr(), res_text.sysname(), res_text.sysmsg());
            }

            update = true;
        }

        this->data_->data = n;
        co_return update;
    });


err:
    if (!res.ok())
        co_return std::move(res);

    co_return this->setup_user_config_();
}

manapi::future<manapi::status> manapi::net::wgrpc::server::config_object(manapi::json config) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return status_already_exists("grpc:Server can only be run in a single instance");

    if (this->data_->worker)
        co_return status_already_exists("wgrpc:Config exists");

    auto res = co_await this->subscribe_();
    if (!res.ok())
        goto err;

    co_await this->data_->ctx.storage().edit_async(this->data_->worker, [&] (manapi::json &n) -> manapi::future<bool> {
        if (!n.is_object())
            n = manapi::json::object();

        bool update = false;

        if (!n.contains("grpc")) {
            res = this->setup_config_(std::move(config), n);
            update = true;
        }

        this->data_->data = n;
        co_return update;
    });


err:
    if (!res.ok())
        co_return std::move(res);

    co_return this->setup_user_config_();
}

manapi::future<manapi::status> manapi::net::wgrpc::server::start(std::move_only_function<manapi::status(grpc::ServerBuilder &b)> cb) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return status_already_exists("grpc:Server can only be run in a signle instance");

    try {
        if (this->data_->finishid) {
            co_return status_already_exists("grpc:Server is already running");
        }

        using ci = manapi::internal::config_interface;

        status res;
        if (!this->data_->worker) {
            res = co_await this->subscribe_();
            if (!res.ok())
                co_return std::move(res);

            res = this->setup_user_config_();
            if (!res)
                co_return std::move(res);
        }

        auto grpc_ = &this->data_->data["grpc"];
        auto const ip = ci::get_config_param<std::string>(*grpc_, "address", "localhost");
        auto const port = ci::get_config_param<std::string>(*grpc_, "port", "8080");
        auto const ssl_it = grpc_->find("ssl");

        std::string server_address = absl::StrFormat("%s:%s", ip.data(), port.data());

        this->data_->builder = std::make_unique<grpc::ServerBuilder>();
        //auto cq = builder.AddCompletionQueue();

        std::shared_ptr<grpc::ServerCredentials> creds;
        if (ssl_it == grpc_->end<json::OBJECT>() || !ssl_it->second.is_object())
            creds = grpc::InsecureServerCredentials();
        else {
            auto const cert = ci::get_config_param<std::string>(ssl_it->second, "cert", {});
            auto const key = ci::get_config_param<std::string>(ssl_it->second, "key", {});
            auto const peer_verify = ci::get_config_param<bool>(ssl_it->second, "verify_peer", true);

            try {
                grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
                auto read_res = co_await manapi::fs::async_read(cert);
                if (!read_res.ok())
                    co_return read_res.err();
                pkcp.cert_chain = read_res.unwrap();
                read_res = co_await manapi::fs::async_read(key);
                if (!read_res.ok())
                    co_return read_res.err();
                pkcp.private_key = read_res.unwrap();
                grpc::SslServerCredentialsOptions ssl_opts(peer_verify ? GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
                ssl_opts.pem_root_certs="";
                ssl_opts.pem_key_cert_pairs.push_back(pkcp);
                creds = grpc::SslServerCredentials(ssl_opts);
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "wgrpc:Ssl certs set failed", e.what());
                co_return status_internal("wgrpc:Ssl certs set failed");
            }
        }

        if (cb)
            cb(*this->data_->builder);

        this->data_->builder->AddListeningPort(server_address, std::move(creds));
        this->data_->server = this->data_->builder->BuildAndStart();

        try {
            this->data_->finishid = manapi::async::current()->eventloop()->subscribe_finish(
                    -1,
                [data = this->data_] () -> manapi::future<> {
                co_await data->ctx.storage().unsubscribe(std::move(data->worker));
                co_await server::stop_(data);
                manapi_log_trace("grpc:Shutdown() finished");
                co_return;
            });
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "wgrpc:Failed to subscribe shutdown service", e.what());
        }

        manapi_log_trace("TCP PORT USED: %.*s. %.*s:%.*s (grpc)", port.size(), port.data(),
            ip.size(), ip.data(), port.size(), port.data());

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "grpc:Start failed", e.what());
        co_return status_internal("grpc:Start failed");
    }
}

manapi::status manapi::net::wgrpc::server::stop() {
    try {
        if (!this->data_->finishid)
            return manapi::status_not_found("wgrpc:Server isn't running");

        manapi::async::current()->eventloop()->unsubscribe_finish(std::exchange(this->data_->finishid, 0));
        manapi::async::run(server::stop_(this->data_));

        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc stop:Something get wrong", e.what());
    }
    return status_internal("wgrpc stop:Something get wrong");
}

manapi::future<> manapi::net::wgrpc::server::stop_(std::shared_ptr<data_t> data) {
    manapi::async::tmutex mx;
    mx.try_to_lock();
    manapi::async::current()->eventloop()->append_task([&mx, server = data->server.get()] (const ev::shared_work &w) -> void {
        server->Shutdown();
        mx.unlock();
    }, nullptr);
    co_await mx.lock_guard();
}

manapi::future<manapi::status> manapi::net::wgrpc::server::subscribe_() {
    try {
        this->data_->worker = co_await this->data_->ctx.storage().subscribe(
            [this] (const manapi::json &n) -> void {
                this->data_->data = n;
        });
        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Worker subscribe failed", e.what());
    }

    co_return status_internal("wgrpc:Worker subscribe failed");
}

manapi::status manapi::net::wgrpc::server::setup_user_config_() {
    try {
        this->data_->config = std::make_shared<wgrpc::config>(this->data_->data["grpc"]);
        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "grpc:setup_user_config failed", e.what());
        return status_internal("grpc:setup_user_config failed");
    }
}

manapi::status manapi::net::wgrpc::server::setup_config_(manapi::json data, manapi::json &n) {
    try {
        if (!data.is_object())
            return status_invalid_argument("wgrpc:Grpc config isn't object");

        n["grpc"] = std::move(data);

        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}


#endif
