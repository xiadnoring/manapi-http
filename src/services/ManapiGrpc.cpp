#include "services/ManapiGrpc.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_GRPC_DEPENDENCY

enum manapi_grpc_endpoint_flags {
    MANAPI_GRPC_ENDPOINT_WANT_READ = 1,
    MANAPI_GRPC_ENDPOINT_FINISHED = 2
};

manapi::manapi_grpc_net_listener::manapi_grpc_net_listener(absl::AnyInvocable<void(absl::Status)> on_shutdown) {
    this->on_shutdown = std::move(on_shutdown);
}

manapi::manapi_grpc_net_listener::~manapi_grpc_net_listener() {
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

absl::StatusOr<int> manapi::manapi_grpc_net_listener::Bind(const grpc_event_engine::experimental::EventEngine::ResolvedAddress &addr) {
    auto res = this->connection->s_bind(addr.address(), manapi::ev::TCP_REUSEPORT);
    if (!res)
        return static_cast<int>(reinterpret_cast <const sockaddr_in *> (addr.address())->sin_port);
    return absl::InternalError("manapi:Bind failed");
}

absl::Status manapi::manapi_grpc_net_listener::Start() {
    if (this->connection->listen(10)) {
        return absl::InternalError("manapi:Listen failed");
    }

    return absl::OkStatus();
}

manapi::manapi_grpc_dns_resolved::manapi_grpc_dns_resolved() {
}

void manapi::manapi_grpc_dns_resolved::LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name,
    absl::string_view default_port) {
    on_resolve(absl::UnimplementedError("not already"));
}

void manapi::manapi_grpc_dns_resolved::LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) {
    on_resolve(absl::UnimplementedError("not already"));
}

void manapi::manapi_grpc_dns_resolved::LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) {
    on_resolve(absl::UnimplementedError("not already"));
}

manapi::manapi_grpc_net_endpoint::manapi_grpc_net_endpoint(manapi::ev::shared_tcp conn,
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

manapi::manapi_grpc_net_endpoint::~manapi_grpc_net_endpoint() {
    try {
        if (this->conn->is_active())
            this->conn->read_stop();

        manapi::async::current()->eventloop()->stop_watcher(std::move(this->conn));
    }
    catch (std::exception const &e) {
        manapi_log_error("grpc: close conn failed due to %s", e.what());
    }
}

bool manapi::manapi_grpc_net_endpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read, grpc_event_engine::experimental::SliceBuffer *buffer, const ReadArgs *args) {
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

bool manapi::manapi_grpc_net_endpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable, grpc_event_engine::experimental::SliceBuffer *data, const WriteArgs *args) {
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

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::manapi_grpc_net_endpoint::GetLocalAddress() const {
    return *this->local_addr;
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress & manapi::manapi_grpc_net_endpoint::GetPeerAddress() const {
    return *this->peer_addr;
}

manapi::manapi_grpc_event_engine_wrapper::manapi_grpc_event_engine_wrapper() : grpc_event_engine::experimental::EventEngine() {
    this->ev = manapi::async::current()->eventloop();
}

manapi::manapi_grpc_event_engine_wrapper::~manapi_grpc_event_engine_wrapper() {
}

bool manapi::manapi_grpc_event_engine_wrapper::Cancel(TaskHandle handle) {
    switch (handle.keys[0]) {
        case 0: return false;
        case 1: {
            /* timer */
            auto const timer = reinterpret_cast<manapi::timer *> (
                std::exchange(handle.keys[1], 0));

            if (!timer)
                return false;

            timer->stop();

            delete timer;
            break;
        }
        default: return false;
    }

    handle.keys[0] = 0;

    return true;
}

grpc_event_engine::experimental::EventEngine::ConnectionHandle manapi::manapi_grpc_event_engine_wrapper::Connect(
    OnConnectCallback on_connect, const ResolvedAddress &addr,
    const grpc_event_engine::experimental::EndpointConfig &args,
    grpc_event_engine::experimental::MemoryAllocator memory_allocator, Duration timeout) {

    auto timer = std::make_unique<manapi::timer>();
    auto const timer_ptr = timer.get();

    auto [connect, conn] = manapi::async::current()->eventloop()->connect_tcp (addr.address(),
        [timer = std::move(timer), on_connect = std::move(on_connect), memory_allocator = std::move(memory_allocator)]
        (const std::shared_ptr<manapi::ev::tcp> &w, int status) mutable
        -> void {
            try {
                timer->stop();

                if (status) {
                    /* error */
                    switch (status) {
                        case UV_ECANCELED: on_connect (absl::CancelledError()); break;
                        default: on_connect(absl::UnknownError("something gets wrong")); break;
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

                auto endpoint = std::make_unique<manapi_grpc_net_endpoint>(w, std::move(peer), std::move(local_addr), std::move(memory_allocator));

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

    *timer_ptr = manapi::async::current()->timerpool()->append_timer_sync(
        std::max(1UL, static_cast<std::size_t>(timeout.count() / 1000000)), [connect] (manapi::timer t)
        -> void {
        if (connect->is_active())
            connect->unbind();
    });

    ConnectionHandle handle{};

    handle.keys[0] = 2;
    handle.keys[1] = reinterpret_cast<std::intptr_t>(connect.get());

    return handle;
}

void manapi::manapi_grpc_event_engine_wrapper::Run(absl::AnyInvocable<void()> closure) {
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

void manapi::manapi_grpc_event_engine_wrapper::Run(Closure *closure) {
    //auto &ctx = manapi::async::current();
    this->Run([closure] ()
        -> void { closure->Run(); });
}

bool manapi::manapi_grpc_event_engine_wrapper::CancelConnect(ConnectionHandle handle) {
    if (handle.keys[0] == 2) {
        auto p = reinterpret_cast<manapi::ev::connect *> (handle.keys[1]);
        if (p->is_active()) {
            p->unbind();
            return true;
        }
    }
    return false;
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>> manapi::
manapi_grpc_event_engine_wrapper::CreateListener(Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown, const grpc_event_engine::experimental::EndpointConfig &config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory> memory_allocator_factory) {
    auto b = std::make_unique<manapi_grpc_net_listener>(std::move(on_shutdown));

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

        auto endpoint = std::make_unique<manapi_grpc_net_endpoint>(std::move(conn), std::move(peer), local_addr, memory_allocator_factory->CreateMemoryAllocator("tcp-endpoint"));
        on_accept (std::move(endpoint), memory_allocator_factory->CreateMemoryAllocator("tcp-handler"));
    });


    sockaddr_storage sock_addr{};
    int sock_len = sizeof (sock_addr);
    b->connection->getsockname(reinterpret_cast<sockaddr*>(&sock_addr), &sock_len);

    *local_addr = grpc_event_engine::experimental::EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(&sock_addr), sock_len);

    return std::move(b);
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::manapi_grpc_event_engine_wrapper::RunAfter(Duration when, Closure *closure) {
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

    task.keys[0] = 1;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

grpc_event_engine::experimental::EventEngine::TaskHandle manapi::manapi_grpc_event_engine_wrapper::RunAfter(Duration when, absl::AnyInvocable<void()> closure) {
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

    task.keys[0] = 1;
    task.keys[1] = reinterpret_cast<std::intptr_t> (timer.release());

    return task;
}

bool manapi::manapi_grpc_event_engine_wrapper::IsWorkerThread() {
    return !!manapi::async::current();
}

absl::StatusOr<std::unique_ptr<grpc_event_engine::experimental::EventEngine::DNSResolver>> manapi::manapi_grpc_event_engine_wrapper::GetDNSResolver(const DNSResolver::ResolverOptions &options) {
    return absl::UnimplementedError("dns");
}



#endif