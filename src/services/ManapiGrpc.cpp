#include "services/ManapiGrpc.hpp"

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

thread_local std::set<std::pair<uintptr_t, ssize_t>> wgrpc_tasks_exists;

decltype(wgrpc_tasks_exists)::iterator wgrpc_tasks_find (std::intptr_t keys[2]) {
    return wgrpc_tasks_exists.find({static_cast<std::uintptr_t>(keys[1]), static_cast<ssize_t>(keys[0])});
}

manapi::net::wgrpc::config::config(const manapi::json &n) {
    this->buffer_size = config::get_config_param<std::size_t>(n, "buffer_size", 4096);
    this->max_buffered_size = config::get_config_param<std::size_t>(n, "max_buffered_size", 65536);
    this->ssl = config::get_config_object_param(n, "ssl", json::object());
    this->backlog = config::get_config_param(n, "backlog", 10);
}

manapi::net::wgrpc::net_listener::net_listener(absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    this->on_shutdown = std::move(on_shutdown);
}

manapi::net::wgrpc::net_listener::~net_listener() {
    try {
        if (this->connection) {
            auto &ev = manapi::async::current()->eventloop();
            ev->stop_callback (this->connection, [cb = std::move(this->on_shutdown)] (const ev::shared_tcp &w) mutable
                -> void {
                if (cb) {
                    cb (absl::OkStatus());
                }
            });
            ev->stop_watcher(std::move(this->connection));
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc:Close tcp listener failed due to %s", e.what());
    }
}

absl::StatusOr<int> manapi::net::wgrpc::net_listener::Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) {
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
    if (this->connection->listen(10)) {
        return absl::InternalError("manapi:Listen failed");
    }

    return absl::OkStatus();
}

manapi::net::wgrpc::dns_resolved::dns_resolved() {
}

void manapi::net::wgrpc::dns_resolved::LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name, absl::string_view default_port) {
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
    manapi::async::current()->etaskpool()->append_task([on_resolve = std::move(on_resolve)] () mutable
        -> void {
        on_resolve(absl::UnimplementedError("not already"));
    });
}

void manapi::net::wgrpc::dns_resolved::LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) {
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
        this->local_addr = std::move(local_addr);
        this->conn = std::move(conn);
        this->peer_addr = std::move(peer_addr);
        this->memory_allocator = std::move(memory_allocator);

        manapi::async::current()->eventloop()->read_callback(this->conn,
            [this] (std::shared_ptr<manapi::ev::tcp> &, ssize_t nread, const manapi::ev::buff_t *buf) -> void {
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
            [this] (std::shared_ptr<manapi::ev::tcp> &, size_t suggested_size, manapi::ev::buff_t *buf)
            -> void {
                ssize_t size = suggested_size;
                if (!(this->flags & MANAPI_GRPC_ENDPOINT_WANT_READ)) {
                    size = std::min<ssize_t>(65536 - this->buffer.Length(), 65536);
                    if (size <= 0)
                        return;
                }

                auto buffer = manapi::async::current()->memory_fabric().buffer(size);
                buf->len = buffer.realsize();
                buf->base = static_cast<char *>(buffer.release());
        });
    }

manapi::net::wgrpc::net_endpoint::~net_endpoint() {
    try {
        if (this->conn->is_active())
            this->conn->read_stop();

        manapi::async::current()->eventloop()->stop_watcher(std::move(this->conn));
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc: close conn failed due to %s", e.what());
    }
}

bool manapi::net::wgrpc::net_endpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) {
    auto const already = this->buffer.Length();
    bool const want_more = already < args->read_hint_bytes;

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
        this->on_read_hints_bytes = args->read_hint_bytes - already;
        assert(this->on_read);
        this->flags |= MANAPI_GRPC_ENDPOINT_WANT_READ;
    }


    return false;
}

bool manapi::net::wgrpc::net_endpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) {
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

bool manapi::net::wgrpc::event_engine_wrapper::Cancel(TaskHandle handle) {
    auto it = wgrpc_tasks_find(handle.keys);
    if (it == wgrpc_tasks_exists.end())
        return false;

    wgrpc_tasks_exists.erase(it);

    /* timer */
    std::cout << "cancel " << handle.keys[1] << "\n";
    std::unique_ptr<manapi::timer> timer (reinterpret_cast<manapi::timer *> (
        std::exchange(handle.keys[1], 0)));

    if (!timer)
        return false;

    timer->stop();

    return true;
}

grpc_event_engine::experimental::EventEngine::ConnectionHandle manapi::net::wgrpc::event_engine_wrapper::Connect(
    OnConnectCallback on_connect, const ResolvedAddress &addr,
    const grpc_event_engine::experimental::EndpointConfig &args,
    grpc_event_engine::experimental::MemoryAllocator memory_allocator, Duration timeout) {
    auto data = std::make_unique<wgrpc_connection_data_t>();

    auto timer = std::make_unique<manapi::timer>();

    auto [connect, conn] = manapi::async::current()->eventloop()->connect_tcp (addr.address(),
        [timer = timer.get(), on_connect = std::move(on_connect), memory_allocator = std::move(memory_allocator)]
        (const std::shared_ptr<manapi::ev::tcp> &w, int status) mutable
        -> void {
            try {
                timer->stop();

                if (status) {
                    /* error */
                    switch (status) {
                        case manapi::ev::ERR_CANCELED: on_connect (absl::CancelledError()); break;
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

                auto endpoint = std::make_unique<net_endpoint>(w, std::move(peer), std::move(local_addr), std::move(memory_allocator));

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

    *timer = manapi::async::current()->timerpool()->append_timer_sync(
        std::max(1UL, static_cast<std::size_t>(timeout.count() / 1000000)), [connect] (manapi::timer t)
        -> void {
        if (connect->is_active())
            connect->unbind();
    });

    ConnectionHandle handle{};

    data->connect = std::move(connect);
    data->timer = std::move(timer);

    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();

    assert(wgrpc_tasks_exists.insert({reinterpret_cast<std::uintptr_t>(data.get()), time}).second);

    handle.keys[0] = time;
    handle.keys[1] = reinterpret_cast<std::intptr_t>(data.release());

    return handle;
}

void manapi::net::wgrpc::event_engine_wrapper::Run(absl::AnyInvocable<void()> closure) {
    auto &ctx = manapi::async::current();
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
    auto it = wgrpc_tasks_find(handle.keys);
    if (it == wgrpc_tasks_exists.end())
        return false;

    wgrpc_tasks_exists.erase(it);

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

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>> manapi::
net::wgrpc::event_engine_wrapper::CreateListener(Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown, const grpc_event_engine::experimental::EndpointConfig &config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) {
    auto b = std::make_unique<net_listener>(std::move(on_shutdown));

    auto local_addr = std::make_shared<grpc_event_engine::experimental::EventEngine::ResolvedAddress> ();
    b->connection = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
        [local_addr, on_accept = std::move(on_accept), memory_allocator_factory = std::move(memory_allocator_factory)]
        (std::shared_ptr<manapi::ev::tcp> & w, int status) mutable
        -> void {
        auto conn = manapi::async::current()->eventloop()->create_watcher_tcp_connection(nullptr, nullptr);

        if (conn->accept(w.get())) {
            conn->unbind();
            return;
        }

        sockaddr_storage sock_addr{};
        int sock_len = sizeof (sock_addr);
        w->getpeername(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);
        auto peer = std::make_unique<grpc_event_engine::experimental::EventEngine::ResolvedAddress>(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

        auto endpoint = std::make_unique<net_endpoint>(std::move(conn), std::move(peer), local_addr, memory_allocator_factory->CreateMemoryAllocator("tcp-endpoint"));
        on_accept (std::move(endpoint), memory_allocator_factory->CreateMemoryAllocator("tcp-handler"));
    });


    sockaddr_storage sock_addr{};
    int sock_len = sizeof (sock_addr);
    b->connection->getsockname(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

    *local_addr = grpc_event_engine::experimental::EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

    return std::move(b);
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, Closure *closure) {
    TaskHandle task{};
    /* oh no way */
    auto timer = std::make_unique<manapi::timer>();

    auto const ms = std::max(static_cast<std::size_t>(1),
        static_cast<std::size_t>(when.count() / 1000000));
    *timer = manapi::async::current()->timerpool()->append_timer_sync(ms,
        [closure, ptr = timer.get()] (manapi::timer timer) -> void {
            try { closure->Run(); }
            catch (std::exception const &e) { MANAPIHTTP_LOG("gRPC send a error: {}", e.what()); }
            delete ptr;
        });


    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();

    assert(wgrpc_tasks_exists.insert({reinterpret_cast<std::uintptr_t>(timer.get()), time}).second);

    task.keys[0] = time;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::net::wgrpc::event_engine_wrapper::RunAfter(Duration when, absl::AnyInvocable<void()> closure) {
    TaskHandle task{};
    /* oh no way */
    auto timer = std::make_unique<manapi::timer>();

    auto const ms = std::max(static_cast<std::size_t>(1),
        static_cast<std::size_t>(when.count() / 1000000));
    *timer = manapi::async::current()->timerpool()->append_timer_sync(ms,
        [closure = std::move(closure), ptr = timer.get()] (manapi::timer timer) mutable -> void {
            try { closure (); }
            catch (std::exception const &e) { MANAPIHTTP_LOG("gRPC send a error: {}", e.what()); }
            delete ptr;
        });
    auto const time = std::chrono::steady_clock::now().time_since_epoch().count();

    assert(wgrpc_tasks_exists.insert({reinterpret_cast<std::uintptr_t>(timer.get()), time}).second);

    task.keys[0] = time;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

bool manapi::net::wgrpc::event_engine_wrapper::IsWorkerThread() {
    return !!manapi::async::current();
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
};

manapi::future<manapi::error::status_or<std::shared_ptr<grpc::ChannelCredentials>>> manapi::net::wgrpc::secure_channel_credentials(std::string certfile) {
    try {
        grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs=co_await manapi::filesystem::async_read(certfile);

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

manapi::multithread_storage & manapi::net::wgrpc::server_ctx::storage() {
    return this->data_->ms;
}

const manapi::async::shared_cthread & manapi::net::wgrpc::server_ctx::ctx() {
    return this->data_->ctx;
}

manapi::net::wgrpc::server::server(wgrpc::server_ctx ctx) {
    this->data_ = std::make_shared<data_t>(std::move(ctx), nullptr);
}

manapi::future<manapi::error::status> manapi::net::wgrpc::server::config(std::string path) {
    if (this->data_->ctx.ctx() != manapi::async::current())
        co_return error::status_already_exists("grpc:Server can only be run in a signle instance");

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
            if (!co_await manapi::filesystem::async_exists(path))
                co_await manapi::filesystem::async_write(path, "{}", ev::IRUSR|ev::IWUSR|ev::IXUSR|ev::IRGRP|ev::IWGRP);
            auto text = co_await manapi::filesystem::async_read(path);
            res = this->setup_config_(manapi::json::parse(text), n);
            if (res.ok())
                n["grpc_path"] = std::move(path);

            update = true;
        }

        this->data_->data = n;
        co_return update;
    });


err:
    if (!res.ok())
        co_return std::move(res);

    this->setup_user_config_();

    co_return error::status_ok();
}

manapi::future<manapi::error::status> manapi::net::wgrpc::server::config_object(manapi::json config) {
    if (this->data_->ctx.ctx() != manapi::async::current())
        co_return error::status_already_exists("grpc:Server can only be run in a single instance");

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

    this->setup_user_config_();

    co_return error::status_ok();
}

manapi::future<manapi::error::status> manapi::net::wgrpc::server::start(std::move_only_function<manapi::error::status(grpc::ServerBuilder &b)> cb) {
    if (this->data_->ctx.ctx() != manapi::async::current())
        co_return error::status_already_exists("grpc:Server can only be run in a signle instance");

    using ci = manapi::internal::config_interface;

    error::status res;
    if (!this->data_->worker) {
        res = co_await this->subscribe_();
        if (!res.ok())
            co_return std::move(res);

        this->setup_user_config_();
    }

    auto grpc_ = &this->data_->data["grpc"];
    auto const ip = ci::get_config_param<std::string>(*grpc_, "address", "localhost");
    auto const port = ci::get_config_param<std::string>(*grpc_, "port", "8080");
    auto const ssl_it = grpc_->find("ssl");

    std::string server_address = absl::StrFormat("%s:%s", ip.data(), port.data());

    grpc::ServerBuilder builder;
    auto cq = builder.AddCompletionQueue();

    std::shared_ptr<grpc::ServerCredentials> creds;
    if (ssl_it == grpc_->end<json::OBJECT>() || !ssl_it->second.is_object())
        creds = grpc::InsecureServerCredentials();
    else {
        auto const cert = ci::get_config_param<std::string>(ssl_it->second, "cert", {});
        auto const key = ci::get_config_param<std::string>(ssl_it->second, "key", {});

        try {
            grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
            pkcp.cert_chain = co_await manapi::filesystem::async_read(cert);
            pkcp.private_key = co_await manapi::filesystem::async_read(key);
            grpc::SslServerCredentialsOptions ssl_opts;
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

    MANAPIHTTP_LOG("grpc:Server listening on {}", server_address);
    co_return error::status_ok();
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

void manapi::net::wgrpc::server::setup_user_config_() {
    this->data_->config = std::make_shared<wgrpc::config>(this->data_->data["grpc"]);
}

manapi::error::status manapi::net::wgrpc::server::setup_config_(manapi::json data, manapi::json &n) {
    if (!data.is_object())
        return error::status_invalid_argument("wgrpc:Grpc config isn't object");

    n["grpc"] = std::move(data);

    return error::status_ok();
}


#endif
