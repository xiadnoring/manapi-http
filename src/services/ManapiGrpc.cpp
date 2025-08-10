#include "services/ManapiGrpc.hpp"

#include <memory>
#include <memory>

#include "ManapiFilesystem.hpp"
#include "services/ManapiDns.hpp"
#include "../include/ManapiInternalGrpc.hpp"
#include "../include/ManapiUtils.hpp"
#include "http/ManapiHttpUtils.hpp"


#if MANAPIHTTP_GRPC_DEPENDENCY

#include <grpcpp/grpcpp.h>

enum manapi_grpc_endpoint_flags {
    MANAPI_GRPC_ENDPOINT_WANT_READ = 1,
    MANAPI_GRPC_ENDPOINT_FINISHED = 2
};

struct wgrpc_connection_data_t {
    std::shared_ptr<manapi::ev::connect> connect;
    std::unique_ptr<manapi::timer> timer;
};

struct wgrpc_thread_local_storage_t {
    std::set<std::pair<uintptr_t, ssize_t>> wgrpc_tasks_exists;
    std::set<std::pair<uintptr_t, ssize_t>> wgrpc_connect_exists;
    std::set<manapi::net::wgrpc::net_listener *> wgrpc_tcp_listeners;
};

struct manapi::net::wgrpc::server_ctx::data_t {
    multithread_storage ms;
    async::shared_cthread ctx;
};

struct manapi::net::wgrpc::server::data_t {
    server_ctx ctx;
    std::shared_ptr<multithread_storage::worker_t> worker;
    manapi::json data;
    std::unique_ptr<grpc::Server> server;
    std::shared_ptr<wgrpc::config> config;
    std::size_t finishid;
};

thread_local wgrpc_thread_local_storage_t wgrpc_storage;

std::set<std::pair<uintptr_t, ssize_t>>::iterator wgrpc_tasks_find (std::intptr_t keys[2]) {
    return wgrpc_storage.wgrpc_tasks_exists.find({static_cast<std::uintptr_t>(keys[1]), static_cast<ssize_t>(keys[0])});
}

std::set<std::pair<uintptr_t, ssize_t>>::iterator wgrpc_connect_find (std::intptr_t keys[2]) {
    return wgrpc_storage.wgrpc_connect_exists.find({static_cast<std::uintptr_t>(keys[1]), static_cast<ssize_t>(keys[0])});
}

void unbind_net_listener (manapi::ev::shared_tcp conn, manapi::async::shared_eventloop ev, absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    try {
        auto &s = manapi::async::internal::current_();

        if (s) {
            if (conn) {
                MANAPIHTTP_MUST_ALLOC_START
                ev->stop_callback (conn, [cb = std::move(on_shutdown)] (const manapi::ev::shared_tcp &w) mutable
                    -> void {
                    if (cb) {
                        cb (absl::OkStatus());
                    }
                });
                MANAPIHTTP_MUST_ALLOC_END
                ev->stop_watcher(std::move(conn));
            }
            return;
        }
        std::move_only_function<void(manapi::event_loop *ev)> cb;
        MANAPIHTTP_MUST_ALLOC_START
        cb = [conn = std::move(conn), ev, cb = std::move(on_shutdown)] (manapi::event_loop *ev_) mutable -> void {
            unbind_net_listener(std::move(conn), std::move(ev), std::move(cb));
        };
        MANAPIHTTP_MUST_ALLOC_END
        MANAPIHTTP_MUST_ALLOC_START
        auto res = ev->custom_callback(&cb);
        if (!res) {
            res.log();
            throw std::bad_alloc{};
        }
        MANAPIHTTP_MUST_ALLOC_END
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc:Close tcp listener failed due to %s", e.what());
    }
}
bool task_handle_cancel (grpc_event_engine::experimental::EventEngine::TaskHandle handle) {
    auto it = wgrpc_tasks_find(handle.keys);
    if (it == wgrpc_storage.wgrpc_tasks_exists.end())
        return false;

    wgrpc_storage.wgrpc_tasks_exists.erase(it);

    /* timer */
    std::unique_ptr<manapi::timer> timer (reinterpret_cast<manapi::timer *> (
        std::exchange(handle.keys[1], 0)));

    if (!timer)
        return false;

    timer->stop();

    return true;
}

bool task_handle_cancel (manapi::timer *t, long long time) {
    grpc_event_engine::experimental::EventEngine::TaskHandle handle;
    handle.keys[0] = time;
    handle.keys[1] = reinterpret_cast<std::intptr_t>(t);
    return task_handle_cancel(handle);
}


bool manapi::net::wgrpc::event_engine_wrapper::Cancel(TaskHandle handle) {
    auto &ctx = manapi::async::internal::current_();
    if (ctx) {
        return task_handle_cancel(handle);
    }

    this->ev->custom_callback([this, handle] (event_loop *ev)
        -> void { this->Cancel(handle); }).unwrap();

    return true;
}

void wgrpc_shutdown () {
    while (!wgrpc_storage.wgrpc_tcp_listeners.empty()) {
        auto it = wgrpc_storage.wgrpc_tcp_listeners.begin();
        (*it)->shutdown(false);
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
    this->ev = manapi::async::current()->eventloop();
}

manapi::net::wgrpc::net_listener::~net_listener() {
    this->shutdown();
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
    return absl::InternalError("manapi:Bind failed");
}

absl::Status manapi::net::wgrpc::net_listener::Start() {
    if (!this->connection)
        return absl::InternalError("wgrpc:Tcp wasn't configured");

    if (this->connection->listen(10)) {
        return absl::InternalError("wgrpc:Tcp listen failed");
    }

    return absl::OkStatus();
}

void manapi::net::wgrpc::net_listener::shutdown(bool notify) noexcept {
    if (this->connection) {
        auto &s = async::internal::current_();
        if (s) {
            wgrpc_storage.wgrpc_tcp_listeners.erase(this);
            unbind_net_listener(std::move(this->connection), this->ev, notify ? std::move(this->on_shutdown) : nullptr);
        }
        else {
            std::move_only_function<void(event_loop *ev)> cb;
            MANAPIHTTP_MUST_ALLOC_START
            cb =[this, ev = this->ev, notify, conn = std::move(this->connection), cb = std::move(on_shutdown)] (event_loop *ev_) mutable
                -> void {
                wgrpc_storage.wgrpc_tcp_listeners.erase(this);
                unbind_net_listener(conn, ev, notify ? std::move(cb) : nullptr);
            };
            MANAPIHTTP_MUST_ALLOC_END
            MANAPIHTTP_MUST_ALLOC_START
            auto res = this->ev->custom_callback(&cb);
            if (!res) {
                res.log();
                throw std::bad_alloc{};
            }
            MANAPIHTTP_MUST_ALLOC_END
        }
    }
}

manapi::error::status manapi::net::wgrpc::net_listener::set(ev::shared_tcp connection) {
    try {
        assert(wgrpc_storage.wgrpc_tcp_listeners.insert(this).second);
    }
    catch (std::exception const &) {
        manapi::async::current()->eventloop()->stop_watcher(std::move(connection));
        return error::status_resource_exhausted();
    }
    this->connection = std::move(connection);
    return error::status_ok();

}

const manapi::ev::shared_tcp & manapi::net::wgrpc::net_listener::conn() const {
    return this->connection;
}

manapi::net::wgrpc::dns_resolved::dns_resolved() {
}

void manapi::net::wgrpc::dns_resolved::LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc loopups hostname only in the event loop");
    manapi::async::current()->etaskpool()->append_task([on_resolve = std::move(on_resolve), name, default_port] () mutable -> void {
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
}

void manapi::net::wgrpc::dns_resolved::LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc loopups srv only in the event loop");
    manapi::async::current()->etaskpool()->append_task([on_resolve = std::move(on_resolve)] () mutable
        -> void {
        on_resolve(absl::UnimplementedError("not already"));
    });
}

void manapi::net::wgrpc::dns_resolved::LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc loopups txt only in the event loop");
    manapi::async::current()->etaskpool()->append_task([on_resolve = std::move(on_resolve)] () mutable
        -> void {
        on_resolve(absl::UnimplementedError("not already"));
    });
}

manapi::net::wgrpc::net_endpoint::net_endpoint(manapi::ev::shared_tcp conn,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> peer_addr,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine::ResolvedAddress> local_addr,
    grpc_event_engine::experimental::MemoryAllocator memory_allocator) {

    this->flags = 0;
    this->ev = manapi::async::current()->eventloop();
    this->local_addr = std::move(local_addr);
    this->conn = std::move(conn);
    this->peer_addr = std::move(peer_addr);
    this->memory_allocator = std::move(memory_allocator);
#if MANAPIHTTP_GRPC_TELEMETRY_INFO
    this->metric = nullptr;
#endif

    manapi::async::current()->eventloop()->read_callback(this->conn,
        [this] (const std::shared_ptr<manapi::ev::tcp> &, ssize_t nread, const manapi::ev::buff_t *buf) -> void {
            bytebuffer buffer;

            if (buf && buf->base)
                buffer = bytebuffer (buf->base, buf->len);

            if (nread < 0) {
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

    manapi::async::current()->eventloop()->alloc_callback(this->conn,
        [this] (const std::shared_ptr<manapi::ev::tcp> &, size_t suggested_size, manapi::ev::buff_t *buf) MANAPIHTTP_NOEXCEPT
        -> void {
            ssize_t size = suggested_size;
            if (!(this->flags & MANAPI_GRPC_ENDPOINT_WANT_READ)) {
                size = std::min<ssize_t>(65536 - this->buffer.Length(), 65536);
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

#if MANAPIHTTP_GRPC_TELEMETRY_INFO

std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint::TelemetryInfo> manapi::net::wgrpc::net_endpoint::GetTelemetryInfo() const {
    return this->metric;
}
#endif

void unbind_net_endpoint (manapi::ev::shared_tcp conn, manapi::async::shared_eventloop ev) noexcept {
    try {
        auto &s = manapi::async::internal::current_();
        if (s) {
            if (conn->is_active())
                conn->read_stop();

            s->eventloop()->stop_watcher(std::move(conn));
            return;
        }

        MANAPIHTTP_MUST_ALLOC_START
        std::move_only_function<void(manapi::event_loop *ev)> cb = [conn, ev] (manapi::event_loop *ev_) mutable -> void {
            unbind_net_endpoint(std::move(conn), std::move(ev));
        };

        auto res = ev->custom_callback(&cb);

        if (!res)
            throw std::bad_alloc{};
        MANAPIHTTP_MUST_ALLOC_END
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc: close conn failed due to %s", e.what());
    }
}

manapi::net::wgrpc::net_endpoint::~net_endpoint() {
    unbind_net_endpoint (this->conn, this->ev);
}

bool manapi::net::wgrpc::net_endpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer,
#if MANAPIHTTP_GRPC_ARGS_MOVEABLE
const ReadArgs args
#else
const ReadArgs *args
#endif
) {
    auto const already = this->buffer.Length();
#if MANAPIHTTP_GRPC_ARGS_MOVEABLE
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

        manapi::async::current()->etaskpool()->append_task([on_read = std::move(on_read)] () mutable -> void {
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
bool manapi::net::wgrpc::net_endpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data,
#if MANAPIHTTP_GRPC_ARGS_MOVEABLE
const WriteArgs args
#else
const WriteArgs *args
#endif
) {
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
    manapi::async::current()->eventloop()->create_watcher_write(this->conn.get(),
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

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::net::wgrpc::net_endpoint::GetLocalAddress() const {
    return *this->local_addr;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::net::wgrpc::net_endpoint::GetPeerAddress() const {
    return *this->peer_addr;
}

manapi::net::wgrpc::event_engine_wrapper::event_engine_wrapper() : grpc_event_engine::experimental::EventEngine() {
    this->ev = manapi::async::current()->eventloop();
}

manapi::net::wgrpc::event_engine_wrapper::~event_engine_wrapper() {

}

grpc_event_engine::experimental::EventEngine::ConnectionHandle manapi::net::wgrpc::event_engine_wrapper::Connect(
    OnConnectCallback on_connect, const ResolvedAddress &addr,
    const grpc_event_engine::experimental::EndpointConfig &args,
    grpc_event_engine::experimental::MemoryAllocator memory_allocator, Duration timeout) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc creates new connection only in the event loop");

    std::unique_ptr<wgrpc_connection_data_t> data (new (std::nothrow) wgrpc_connection_data_t{});
    std::unique_ptr<manapi::timer> timer (new (std::nothrow) manapi::timer{});

    assert(data && timer);

    auto wres = manapi::async::current()->eventloop()->connect_tcp (addr.address(),
        [timer = timer.get(), on_connect = std::move(on_connect), memory_allocator = std::move(memory_allocator)]
        (const std::shared_ptr<manapi::ev::tcp> &w, int status) mutable
        -> void {
            try {
                timer->stop();

                if (status) {
                    /* error */
                    switch (status) {
                        case manapi::ev::ERR_AI_CANCELED: on_connect (absl::CancelledError()); break;
                        default: on_connect(absl::UnknownError("wgrpc:Something gets wrong")); break;
                    }
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

                auto endpoint = std::make_unique<wgrpc::net_endpoint>(w, std::move(peer), std::move(local_addr), std::move(memory_allocator));

                on_connect(std::move(endpoint));

                return;
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_INTERNAL, "creating connection failed due to {}",
                    e.what());
            }

            on_connect(absl::InternalError("creating connection failed"));
        },
        nullptr, nullptr);

    auto [connect, conn] = wres.unwrap();

    auto res = manapi::async::current()->timerpool()->append_timer_sync(
        std::max(1UL, static_cast<std::size_t>(timeout.count() / 1000000)), [connect] (manapi::timer t)
        -> void {
        if (connect->is_active())
            connect->unbind();
    });

    assert(res.ok());

    *timer = res.unwrap();

    ConnectionHandle handle{};

    data->connect = std::move(connect);
    data->timer = std::move(timer);

    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();

    try {
        assert(wgrpc_storage.wgrpc_connect_exists.insert({reinterpret_cast<std::uintptr_t>(data.get()), time}).second);
    }
    catch (std::exception const &) {
        auto &ev = manapi::async::current()->eventloop();
        ev->stop_watcher(std::move(connect));
        ev->stop_watcher(std::move(conn));
    }

    handle.keys[0] = time;
    handle.keys[1] = reinterpret_cast<std::intptr_t>(data.release());

    return handle;
}

void manapi::net::wgrpc::event_engine_wrapper::Run(absl::AnyInvocable<void()> closure) {
    auto &ctx = manapi::async::internal::current_();
    if (ctx)
        ctx->etaskpool()->append_task(std::move(closure));
    else {
        this->ev->custom_callback(
            [closure = std::move(closure)] (manapi::event_loop *ev) mutable -> void {
            ev->taskpool()->append_task(std::move(closure));
        });
    }
}

void manapi::net::wgrpc::event_engine_wrapper::Run(Closure *closure) {
    //auto &ctx = manapi::async::current();
    this->Run([closure] ()
        -> void { closure->Run(); });
}

bool manapi::net::wgrpc::event_engine_wrapper::CancelConnect(ConnectionHandle handle) {
    auto &ctx = manapi::async::internal::current_();
    //assert(ctx && "bug: I think grpc closes the connection only in the event loop");
    if (ctx) {
        auto it = wgrpc_connect_find(handle.keys);
        if (it == wgrpc_storage.wgrpc_connect_exists.end())
            return false;

        wgrpc_storage.wgrpc_connect_exists.erase(it);

        std::unique_ptr<wgrpc_connection_data_t> p (reinterpret_cast<wgrpc_connection_data_t *> (std::exchange(handle.keys[1], 0)));
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

    this->ev->custom_callback([this, handle] (event_loop *ev)
        -> void { this->CancelConnect(handle); });

    return true;
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>> manapi::
net::wgrpc::event_engine_wrapper::CreateListener(Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown, const grpc_event_engine::experimental::EndpointConfig &config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc creates new listener only in the event loop");
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

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, Closure *closure) {
    auto &ctx = manapi::async::current();
    assert(ctx && "bug: I think grpc creates timer only in the event loop");
    TaskHandle task{};
    /* oh no way */
    auto timer = std::make_unique<manapi::timer>();

    auto const ms = std::max(static_cast<std::size_t>(1),
        static_cast<std::size_t>(when.count() / 1000000));
    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();
    auto rhs = ctx->timerpool()->append_timer_sync(ms,
        [closure, ptr = timer.get(), time] (manapi::timer timer) -> void {
            try { closure->Run(); }
            catch (std::exception const &e) { MANAPIHTTP_LOG("gRPC send a error: {}", e.what()); }
            task_handle_cancel(ptr, time);
        });

    assert(rhs.ok());

    *timer = rhs.unwrap();

    assert(wgrpc_storage.wgrpc_tasks_exists.insert({reinterpret_cast<std::uintptr_t>(timer.get()), time}).second);

    task.keys[0] = time;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, absl::AnyInvocable<void()> closure) {
    auto &ctx = manapi::async::internal::current_();
    assert(ctx && "bug: I think grpc creates timer only in the event loop");
    TaskHandle task{};
    /* oh no way */
    auto timer = std::make_unique<manapi::timer>();

    auto const ms = std::max(static_cast<std::size_t>(1),
        static_cast<std::size_t>(when.count() / 1000000));
    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();
    auto rhs = ctx->timerpool()->append_timer_sync(ms,
        [closure = std::move(closure), ptr = timer.get(), time] (manapi::timer timer) mutable -> void {
            try { closure (); }
            catch (std::exception const &e) { MANAPIHTTP_LOG("gRPC send a error: {}", e.what()); }
            task_handle_cancel(ptr, time);
        });

    assert(rhs.ok());

    *timer = rhs.unwrap();

    assert(wgrpc_storage.wgrpc_tasks_exists.insert({reinterpret_cast<std::uintptr_t>(timer.get()), time}).second);

    task.keys[0] = time;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

bool manapi::net::wgrpc::event_engine_wrapper::IsWorkerThread() {
    return !manapi::async::internal::current_();
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::DNSResolver>> manapi::net::wgrpc::event_engine_wrapper::GetDNSResolver(const DNSResolver::ResolverOptions &options) {
    try {
        return std::make_unique<dns_resolved>();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Failed to create dns resolved", e.what());
    }
    return absl::UnimplementedError("dns");
}

manapi::future<manapi::error::status_or<std::shared_ptr<grpc::ChannelCredentials>>> manapi::net::wgrpc::secure_channel_credentials(std::string certfile) {
    try {
        auto res = co_await manapi::filesystem::async_read(certfile);
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

    co_return error::status_internal("wgrpc:SecureChannelCreds something gets wrong");
}

manapi::net::wgrpc::server_ctx::server_ctx() {
    std::function<void(void *ptr)> deleter = [] (void *ptr)
        -> void { delete static_cast<worker_data_t *>(ptr); };
    this->data_ = std::make_shared<data_t>(multithread_storage (new worker_data_t(), std::move(deleter)));
    this->data_->ctx = manapi::async::current();
    try {
        auto ev = std::make_shared<manapi::net::wgrpc::event_engine_wrapper>();
        grpc_event_engine::experimental::SetDefaultEventEngine(ev);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Set default event loop failed", e.what());
    }
}

static manapi::error::status_or<manapi::net::wgrpc::server_ctx> manapi::net::wgrpc::server_ctx::create () MANAPIHTTP_NOEXCEPT {
    try {
        return server_ctx{};
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return error::status_resource_exhausted();
    }
}

manapi::multithread_storage & manapi::net::wgrpc::server_ctx::storage() {
    return this->data_->ms;
}

const manapi::async::shared_cthread & manapi::net::wgrpc::server_ctx::ctx() {
    return this->data_->ctx;
}

manapi::net::wgrpc::server::server(wgrpc::server_ctx ctx) {
    this->data_ = std::make_shared<data_t>(std::move(ctx), nullptr);
}

static manapi::error::status_or<manapi::net::wgrpc::server> manapi::net::wgrpc::server::create (wgrpc::server_ctx ctx) MANAPIHTTP_NOEXCEPT {
    try {
        return server(std::move(ctx));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return error::status_resource_exhausted();
    }
}

manapi::net::wgrpc::server::~server() = default;

manapi::future<manapi::error::status> manapi::net::wgrpc::server::config(std::string path) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return error::status_already_exists("grpc:Server can only be run in a signle instance");

    if (this->data_->worker)
        co_return error::status_already_exists("wgrpc:Config exists");

    auto res = co_await this->subscribe_();
    if (!res.ok())
        goto err;

    co_await this->data_->ctx.storage().edit_async(this->data_->worker, [&] (manapi::json &n) -> manapi::future<bool> {
        if (!n.is_object())
            n = manapi::json::object();

        bool update = false;

        if (!n.contains("grpc")) {
            auto res_exists = co_await manapi::filesystem::async_exists(path);
            if (!res_exists.ok() || !res_exists.unwrap())
                co_await manapi::filesystem::async_write(path, "{}", ev::IRUSR|ev::IWUSR|ev::IXUSR|ev::IRGRP|ev::IWGRP);
            auto res_text = co_await manapi::filesystem::async_read(path);
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

manapi::future<manapi::error::status> manapi::net::wgrpc::server::config_object(manapi::json config) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return error::status_already_exists("grpc:Server can only be run in a single instance");

    if (this->data_->worker)
        co_return error::status_already_exists("wgrpc:Config exists");

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

manapi::future<manapi::error::status> manapi::net::wgrpc::server::start(std::move_only_function<manapi::error::status(grpc::ServerBuilder &b)> cb) {
    // if (this->data_->ctx.ctx() != manapi::async::current())
    //     co_return error::status_already_exists("grpc:Server can only be run in a signle instance");
    try {
        if (this->data_->finishid) {
            co_return error::status_already_exists("grpc:Server is already running");
        }

        using ci = manapi::internal::config_interface;

        error::status res;
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

        grpc::ServerBuilder builder;
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
                auto read_res = co_await manapi::filesystem::async_read(cert);
                if (!read_res.ok())
                    co_return read_res.err();
                pkcp.cert_chain = read_res.unwrap();
                read_res = co_await manapi::filesystem::async_read(key);
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
                co_return error::status_internal("wgrpc:Ssl certs set failed");
            }
        }

        if (cb)
            cb(builder);

        builder.AddListeningPort(server_address, std::move(creds));
        this->data_->server = builder.BuildAndStart();

        try {
            this->data_->finishid = manapi::async::current()->eventloop()->subscribe_finish(
                [data = this->data_] () -> manapi::future<> {
                //wgrpc_shutdown();
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

        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "grpc:Start failed", e.what());
        co_return error::status_internal("grpc:Start failed");
    }
}

manapi::error::status manapi::net::wgrpc::server::stop() {
    try {
        if (!this->data_->finishid)
            return manapi::error::status_not_found("wgrpc:Server isn't running");

        manapi::async::current()->eventloop()->unsubscribe_finish(std::exchange(this->data_->finishid, 0));
        manapi::async::run(server::stop_(this->data_));

        return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc stop:Something get wrong", e.what());
    }
    return error::status_internal("wgrpc stop:Something get wrong");
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

manapi::future<manapi::error::status> manapi::net::wgrpc::server::subscribe_() {
    try {
        this->data_->worker = co_await this->data_->ctx.storage().subscribe(
            [this] (const manapi::json &n) -> void {
                this->data_->data = n;
        });
        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "wgrpc:Worker subscribe failed", e.what());
    }

    co_return error::status_internal("wgrpc:Worker subscribe failed");
}

manapi::error::status manapi::net::wgrpc::server::setup_user_config_() {
    try {
        this->data_->config = std::make_shared<wgrpc::config>(this->data_->data["grpc"]);
        return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "grpc:setup_user_config failed", e.what());
        return error::status_internal("grpc:setup_user_config failed");
    }
}

manapi::error::status manapi::net::wgrpc::server::setup_config_(manapi::json data, manapi::json &n) {
    try {
        if (!data.is_object())
            return error::status_invalid_argument("wgrpc:Grpc config isn't object");

        n["grpc"] = std::move(data);

        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}


#endif
