#include <csignal>

#include "services/ManapiEventLoop.hpp"

#include <memory>
#include <cstring>

#include "async/ManapiAsyncSocket.hpp"
#include "components/TimerObject.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

#define MANAPI_EV_UNWATCHER(classname, ctxname) template<> void manapi::event_loop::event_loop::stop_watcher(ev::classname *w) { \
    if (w->is_active()) { \
        w->unbind(+[](uv_handle_t *handle) -> void { auto data = static_cast<ev::internal::ctxname *>(handle->data); \
        handle->data = nullptr; if (data) { data->s_.reset(); delete data; } }); \
    } }

enum add_watcher_io_events {
    ADD_IO_EVENT_INIT_FD = 0,
    ADD_IO_EVENT_INIT_SD,
    ADD_IO_EVENT_STOP,
    ADD_IO_EVENT_START,
    ADD_IO_EVENT_REMOVE
};

enum add_watcher_async_events {
    ADD_ASYNC_EVENT_INIT = 0,
    ADD_ASYNC_EVENT_REMOVE
};

enum add_watcher_timer_events {
    ADD_TIMER_EVENT_INIT = 0,
    ADD_TIMER_EVENT_START,
    ADD_TIMER_EVENT_STOP,
    ADD_TIMER_EVENT_REMOVE,
    ADD_TIMER_EVENT_AGAIN
};

enum add_watcher_fs_events {
    ADD_FS_EVENT_OPEN = 0,
    ADD_FS_EVENT_WRITE,
    ADD_FS_EVENT_READ,
    ADD_FS_EVENT_CLOSE,
    ADD_FS_EVENT_UNLINK,
    ADD_FS_EVENT_MKDIR,
    ADD_FS_EVENT_MKDTEMP,
    ADD_FS_EVENT_MKSTEMP,
    ADD_FS_EVENT_RMDIR,
    ADD_FS_EVENT_OPENDIR,
    ADD_FS_EVENT_READDIR,
    ADD_FS_EVENT_CLOSEDIR,
    ADD_FS_EVENT_SCANDIR,
    ADD_FS_EVENT_SCANDIR_NEXT,
    ADD_FS_EVENT_STAT,
    ADD_FS_EVENT_FSTAT,
    ADD_FS_EVENT_LSTAT,
    ADD_FS_EVENT_STATFS,
    ADD_FS_EVENT_RENAME,
    ADD_FS_EVENT_FSYNC,
    ADD_FS_EVENT_FDATASYNC,
    ADD_FS_EVENT_FTRUNCATE,
    ADD_FS_EVENT_COPYFILE,
    ADD_FS_EVENT_SENDFILE,
    ADD_FS_EVENT_ACCESS,
    ADD_FS_EVENT_CHMOD,
    ADD_FS_EVENT_FCHMOD,
    ADD_FS_EVENT_UTIME,
    ADD_FS_EVENT_FUTIME,
    ADD_FS_EVENT_LUTIME,
    ADD_FS_EVENT_SYMLINK,
    ADD_FS_EVENT_READLINK,
    ADD_FS_EVENT_REALPATH,
    ADD_FS_EVENT_CHOWN,
    ADD_FS_EVENT_FCHOWN,
    ADD_FS_EVENT_LCHOWN,
    ADD_FS_EVENT_LINK
};

namespace manapi::ev::internal {
#if MANAPIHTTP_CURL_DEPENDENCY
    struct curl_multi_deleter {
        void operator()(CURLM *curl_multi) noexcept {
            curl_multi_cleanup(curl_multi);
        }
    };
#endif

    struct async_ctx {
        std::shared_ptr<ev::async> s_;
        ev::async_cb cb;
    };

    struct timer_ctx  {
        std::shared_ptr<ev::timer> s_;
        ev::timer_cb cb;
    };

    struct prepare_ctx {
        std::shared_ptr<ev::prepare> s_;
        ev::prepare_cb cb;
    };

    struct check_ctx {
        std::shared_ptr<ev::check> s_;
        ev::check_cb cb;
    };

    struct idle_ctx {
        std::shared_ptr<ev::idle> s_;
        ev::idle_cb cb;
    };

    struct io_ctx {
        std::shared_ptr<ev::io> s_;
        ev::io_cb cb;
    };

    struct tcp_accept_ctx {
        std::shared_ptr<ev::tcp> s_;
        ev::tcp_accept_cb connection;
    };

    struct tcp_connection_ctx {
        std::shared_ptr<ev::tcp> s_;
        ev::tcp_connection_cb read;
    };

    struct udp_ctx {
        std::shared_ptr<ev::udp> s_;
        ev::udp_cb recv;
    };

    struct udp_send_ctx {
        std::shared_ptr<ev::udp_send> s_;
        ev::udp_send_cb send;
    };

    struct write_ctx {
        std::shared_ptr<ev::write> s_;
        ev::write_cb write;
    };

    struct fs_ctx {
        std::shared_ptr<ev::fs> s_;
        fs_cb cb;
    };

    struct random_ctx {
        std::shared_ptr<ev::random> s_;
        random_cb cb;
    };

    struct adding_watcher_io_data_init_t {
        union {
            manapi::fd_t fd = 0;
            manapi::socket_t sd;
        };
        manapi::ev::io_cb cb = nullptr;
    };

    struct adding_watcher_io_data_payload_t {
        adding_watcher_io_data_init_t init{};
        std::shared_ptr<manapi::ev::io> s = nullptr;
    };

    struct adding_watcher_io_data_t {
        int flag;
        int flags;
        adding_watcher_io_data_payload_t payload;
        manapi::async::promise<std::shared_ptr<manapi::ev::io>>::resolve_t resolve;
    };

    struct adding_watcher_fs_data_t {
        int flag;
        const void *data;
        std::string path1;
        std::string path2;
        ssize_t s1;
        ssize_t s2;
        ev::file file;
        ev::fs_cb cb;
        manapi::async::promise<std::shared_ptr<manapi::ev::fs>>::resolve_t resolve;
        manapi::async::promise<std::shared_ptr<manapi::ev::fs>>::reject_t reject;
        ev::file file2{0};
    };

    struct adding_watcher_async_data_payload_t {
        manapi::ev::async_cb cb = nullptr;
        std::shared_ptr<manapi::ev::async> s = nullptr;
    };

    struct adding_watcher_async_data_t {
        int flag;
        adding_watcher_async_data_payload_t payload;
        manapi::async::promise<std::shared_ptr<manapi::ev::async>>::resolve_t resolve{nullptr};
    };

    struct adding_watcher_prepare_data_payload_t {
        manapi::ev::prepare_cb cb = nullptr;
        std::shared_ptr<manapi::ev::prepare> s = nullptr;
    };

    struct adding_watcher_prepare_data_t {
        int flag;
        adding_watcher_prepare_data_payload_t payload;
        manapi::async::promise<std::shared_ptr<manapi::ev::async>>::resolve_t resolve{nullptr};
    };

    struct adding_watcher_check_data_payload_t {
        manapi::ev::check_cb cb = nullptr;
        std::shared_ptr<manapi::ev::check> s = nullptr;
    };

    struct adding_watcher_check_data_t {
        int flag;
        adding_watcher_check_data_payload_t payload;
        manapi::async::promise<std::shared_ptr<manapi::ev::check>>::resolve_t resolve{nullptr};
    };

    struct adding_watcher_timer_data_payload_t {
        manapi::ev::timer_cb cb = nullptr;
        std::shared_ptr<manapi::ev::timer> s = nullptr;
    };

    struct adding_watcher_timer_data_t {
        int flag;
        uint64_t delay;
        uint64_t repeat;
        adding_watcher_timer_data_payload_t payload;
        manapi::async::promise<std::shared_ptr<manapi::ev::timer>>::resolve_t resolve{nullptr};
    };

#if MANAPIHTTP_CURL_DEPENDENCY
    struct adding_curl_data_t {
        int flag{0};
        std::shared_ptr<CURL> curl{nullptr};
        std::move_only_function<void(CURLcode result)> finish{nullptr};
        manapi::async::promise<void>::resolve_t resolve{nullptr};
        manapi::async::promise<void>::reject_t reject{nullptr};
    };
#endif

    struct adding_custom_callback_data_t {
        std::move_only_function<void(manapi::event_loop *ev)> cb;
        manapi::async::promise<void, std::false_type>::resolve_t resolve{nullptr};
        manapi::async::promise<void, std::false_type>::reject_t reject{nullptr};
    };

    struct io_watcher_t {
        manapi::chain<std::unique_ptr<adding_watcher_io_data_t>> adding_watcher_data{};
        std::shared_ptr<manapi::async::mutex> adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        std::move_only_function<void()> adding_watcher_async_cb{nullptr};
    };

    struct fs_watcher_t {
        manapi::chain<std::unique_ptr<adding_watcher_fs_data_t>> adding_watcher_data{};
        std::shared_ptr<manapi::async::mutex> adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        std::move_only_function<void()> adding_watcher_async_cb{nullptr};
    };

    struct async_watcher_t {
        manapi::chain<std::unique_ptr<adding_watcher_async_data_t>> adding_watcher_data{};
        std::shared_ptr<manapi::async::mutex> adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        std::move_only_function<void()> adding_watcher_async_cb{nullptr};
    };

    struct timer_watcher_t {
        manapi::chain<std::unique_ptr<adding_watcher_timer_data_t>> adding_watcher_data{};
        std::shared_ptr<manapi::async::mutex> adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        std::move_only_function<void()> adding_watcher_async_cb{nullptr};
    };

    struct prepare_watcher_t {
        manapi::chain<std::unique_ptr<adding_watcher_prepare_data_t>> adding_watcher_data{};
        std::shared_ptr<manapi::async::mutex> adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        std::move_only_function<void()> adding_watcher_async_cb{nullptr};
    };

#if MANAPIHTTP_CURL_DEPENDENCY
    struct curl_res_value_t {
        std::shared_ptr<CURL> self;
        std::move_only_function<void(CURLcode result)> finish;
        std::shared_ptr<ev::io> watcher;
    };
    struct curl_watcher_t {
        std::unique_ptr<CURLM, curl_multi_deleter> curl_multi{nullptr};
        std::shared_ptr<manapi::async::mutex> curl_multi_mx{nullptr};
        std::shared_ptr<ev::async> adding_curl_multi_async{nullptr};
        manapi::chain<std::unique_ptr<adding_curl_data_t>> adding_curl_data{};

        std::move_only_function<void()> adding_curl_async_cb{nullptr};
        std::queue<std::shared_ptr<ev::io>> curl_fds{};
        std::map<CURL*, curl_res_value_t> curl_res{};
        std::shared_ptr<ev::timer> timeout_watcher{nullptr};
        std::map<socket_t, std::shared_ptr<ev::io>> watchers;
    };
#endif
    struct timerloop_t {
        manapi::chain<std::unique_ptr<adding_timerloop_data_t>> adding_timer_data{};
        std::shared_ptr<manapi::async::mutex> adding_timer_mx;
        std::shared_ptr <ev::async> adding_timer_async;
        std::move_only_function<void()> adding_timer_async_cb{nullptr};
        std::move_only_function<std::optional<manapi::timer>(manapi::ev::internal::adding_timerloop_data_t *data)> external_cb;
    };

    struct custom_callback_t {
        manapi::chain<std::unique_ptr<adding_custom_callback_data_t>> callback_data{};
        std::shared_ptr<manapi::async::mutex> adding_mx;
        std::shared_ptr <ev::async> adding_async;
        std::move_only_function<void()> adding_async_cb{nullptr};
    };
}

class fs_req_deleter {
    uv_fs_t *req;
public:
    fs_req_deleter (uv_fs_t *req) : req(req) { }
    ~fs_req_deleter() { uv_fs_req_cleanup(this->req); }
};

std::map<size_t, std::shared_ptr<manapi::event_loop>> manapi::event_loop::events = {};
std::atomic<bool> manapi::event_loop::interrupted = false;
std::mutex manapi::event_loop::stop_mx;

void handler_interrupt (int sig) {
    manapi::event_loop::interrupt();
}

void manapi::ev::callback_watcher_async (uv_async_t *s) {
    static_cast<manapi::ev::internal::async_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::async_ctx *> (s->data)->s_);
}

void manapi::ev::callback_watcher_timer (uv_timer_t *s) {
    static_cast<manapi::ev::internal::timer_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::timer_ctx *> (s->data)->s_);
}

void manapi::ev::callback_watcher_io (uv_poll_t *s, int status, int revents) {
    static_cast<manapi::ev::internal::io_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::io_ctx *> (s->data)->s_, status, revents);
}

void manapi::ev::callback_watcher_idle (uv_idle_t *s) {
    static_cast<manapi::ev::internal::idle_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::idle_ctx *> (s->data)->s_);
}

void manapi::ev::callback_watcher_check (uv_check_t *s) {
    static_cast<manapi::ev::internal::check_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::check_ctx *> (s->data)->s_);
}

void manapi::ev::callback_watcher_prepare (uv_prepare_t *s) {
    static_cast<manapi::ev::internal::prepare_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::prepare_ctx *> (s->data)->s_);
}

void manapi::ev::callback_watcher_tcp_accept (uv_tcp_t *s, int status) {
    static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)
        ->connection(static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)->s_, status);
}

void manapi::ev::callback_watcher_tcp_read (uv_stream_t *s, ssize_t nread, const uv_buf_t *buf) {
    static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)
        ->read(static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)->s_, nread, buf);
}

void manapi::ev::callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags) {
    static_cast<manapi::ev::internal::udp_ctx *> (s->data)
        ->recv(static_cast<manapi::ev::internal::udp_ctx *> (s->data)->s_, nread, buf, addr, flags);
}

void manapi::ev::callback_watcher_udp_send (uv_udp_send_t *s, int status) {
    static_cast<manapi::ev::internal::udp_send_ctx *> (s->data)
        ->send(static_cast<manapi::ev::internal::udp_send_ctx *> (s->data)->s_, status);
}

void manapi::ev::callback_watcher_write (uv_write_t *s, int status) {
    static_cast<manapi::ev::internal::write_ctx *> (s->data)
        ->write(static_cast<manapi::ev::internal::write_ctx *> (s->data)->s_, status);
}

void manapi::ev::callback_watcher_fs(uv_fs_t *req) {
    fs_req_deleter deleter (req);
    static_cast<manapi::ev::internal::fs_ctx *> (req->data)
        ->cb(static_cast<manapi::ev::internal::fs_ctx *> (req->data)->s_);
}

void manapi::ev::callback_watcher_random(uv_random_t *s, int status, void *buff, std::size_t size) {
    static_cast<manapi::ev::internal::random_ctx *> (s->data)
        ->cb(static_cast<manapi::ev::internal::random_ctx *> (s->data)->s_, status, buff, size);
}


void manapi::ev::callback_watcher_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {

}

ssize_t double_store_in_ssize (double a) { ssize_t b = 0; memcpy (&b, &a, sizeof (a)); return a; }
double ssize_store_in_double (ssize_t a) { double b = 0; memcpy (&b, &a, sizeof (a)); return b; };


manapi::event_loop::event_loop(std::shared_ptr<threadpool<task>> taskpool_, std::shared_ptr<manapi::logger> logger) {
    assert(!(uv_loop_init(&this->loop_)));

    this->async_watcher = std::make_unique<ev::internal::async_watcher_t>();
    this->io_watcher = std::make_unique<ev::internal::io_watcher_t>();
    this->fs_watcher = std::make_unique<ev::internal::fs_watcher_t>();
    this->timer_watcher = std::make_unique<ev::internal::timer_watcher_t>();

    this->curl_watcher = std::make_unique<ev::internal::curl_watcher_t>();
    this->timerloop = std::make_unique<ev::internal::timerloop_t>();
    this->callback_watcher_ = std::make_unique<ev::internal::custom_callback_t>();


    this->mx = std::make_shared<async::mutex>(taskpool_);
    this->logger_ = std::move(logger);
    this->do_tasks_watcher = create_watcher_prepare([this] (std::shared_ptr<ev::prepare> &w)
        -> void { this->handle_tasks_do_event(w); });

    this->async_watcher->adding_watcher_mx = std::make_shared<async::mutex>(taskpool_);
    this->io_watcher->adding_watcher_mx = std::make_shared<async::mutex>(taskpool_);
    this->timer_watcher->adding_watcher_mx = std::make_shared<async::mutex>(taskpool_);
    this->fs_watcher->adding_watcher_mx = std::make_shared<async::mutex>(taskpool_);

    this->async_watcher->adding_watcher_data = {};
    this->io_watcher->adding_watcher_data = {};
    this->timer_watcher->adding_watcher_data = {};
    this->fs_watcher->adding_watcher_data = {};

    this->timerloop->adding_timer_mx = std::make_shared<async::mutex>(taskpool_);
    this->callback_watcher_->adding_mx = std::make_shared<async::mutex>(taskpool_);

    this->map_finish_cb_mx = std::make_shared<async::mutex>(taskpool_);
    this->map_clean_up_cb_mx = std::make_shared<async::mutex>(taskpool_);

    this->taskpool_ = std::move(taskpool_);
    this->status = false;

    this->fs_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_fs_async(w); });

    this->async_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_async_async(w); });

    this->io_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_poll_async(w); });

    this->timer_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_timer_async(w); });

    this->timerloop->adding_timer_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_timerloop_async(w); });

    this->callback_watcher_->adding_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_callback_async(w); });

    this->timer_watcher->adding_watcher_async_cb = [this] ()
        -> void { this->timer_watcher->adding_watcher_async->send(); };
    this->io_watcher->adding_watcher_async_cb = [this] ()
        -> void { this->io_watcher->adding_watcher_async->send(); };
    this->async_watcher->adding_watcher_async_cb = [this] ()
        -> void { this->async_watcher->adding_watcher_async->send(); };
    this->timerloop->adding_timer_async_cb = [this] ()
        -> void { this->timerloop->adding_timer_async->send(); };
    this->callback_watcher_->adding_async_cb = [this] ()
        -> void { this->callback_watcher_->adding_async->send(); };
    this->fs_watcher->adding_watcher_async_cb = [this] ()
        -> void { this->fs_watcher->adding_watcher_async->send(); };

#if MANAPIHTTP_CURL_DEPENDENCY
    this->curl_watcher->curl_multi_mx = std::make_shared<async::mutex>(this->taskpool_);
    this->curl_watcher->curl_multi.reset(curl_multi_init());
    this->curl_watcher->adding_curl_multi_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_curl_async(w); });
    this->curl_watcher->adding_curl_async_cb = [this] ()
        -> void { this->curl_watcher->adding_curl_multi_async->send(); };

    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETFUNCTION, event_loop::handle_curl_socket);
    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETDATA, this);
#endif

    this->do_tasks_watcher->start();

#if MANAPIHTTP_CURL_DEPENDENCY
    // this->curl_watcher.adding_curl_multi_async->start();

    /* curl fetch timeout */
    this->curl_watcher->timeout_watcher = create_watcher_timer([this] (std::shared_ptr<ev::timer> &w) -> void {
        this->curl_watcher->adding_curl_multi_async->send();

        w->repeat(50);
        w->again();
    });

    this->curl_watcher->timeout_watcher->start(50, 0);
#endif

    /* interrupted */
    this->interrupted_watcher_ = create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { async::run(this->taskpool_, this->stop()); });
}

manapi::event_loop::~event_loop() {
    this->stop()
        .get(this->taskpool_);
    this->stop_watcher (this->interrupted_watcher_);
    this->stop_watcher (this->timerloop->adding_timer_async);
#if MANAPIHTTP_CURL_DEPENDENCY
    this->stop_watcher (this->curl_watcher->adding_curl_multi_async);
#endif
    this->stop_watcher (this->async_watcher->adding_watcher_async);
    this->stop_watcher(this->callback_watcher_->adding_async);
    this->stop_watcher(this->do_tasks_watcher);
}

manapi::future<> manapi::event_loop::start(std::shared_ptr<event_loop> le) {
    auto lk = co_await this->mx->lock_guard();
    if (std::exchange(this->status,true)) {
        co_return;
    }

    this->pool_(std::move(lk), std::move(le));
}

void manapi::event_loop::sync_start(std::shared_ptr<event_loop> le) {
    auto lk = this->mx->lock_guard().get(this->taskpool_);
    if (std::exchange(this->status,true)) {
        return;
    }

    this->pool_(std::move(lk), std::move(le));
}

void manapi::event_loop::setup_handle_interrupt() {
#ifdef _WIN32
#else
    signal (SIGPIPE, SIG_IGN);
    signal (SIGKILL, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
#endif
    signal (SIGABRT, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
}

manapi::future<> manapi::event_loop::stop() {

    auto lk1 = co_await this->mx->lock_guard();

    if (!std::exchange(this->status,false)) {
        co_return;
    }

    co_await this->_call_on_finish_cb();

    {
        auto lk2 = co_await this->map_clean_up_cb_mx->lock_guard();
        auto promise = async::promise<void> (this->taskpool_,
            [this] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<> {
            this->resolve_stop = std::move(resolve);
            this->stop_watcher_->send();
            co_return;
        });

        co_await promise;
    }
}

manapi::future<size_t> manapi::event_loop::subscribe_finish(std::move_only_function<manapi::future<void>()> cb) {
    auto lk = co_await this->map_finish_cb_mx->lock_guard();

    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->map_finish_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_SUBSCRIBE_FAILURE, "index {} exists", id);
    }

    co_return id;
}

manapi::future<std::size_t> manapi::event_loop::subscribe_clean_up(std::move_only_function<void()> cb) {
    auto lk = co_await this->map_clean_up_cb_mx->lock_guard();

    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->map_clean_up_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_SUBSCRIBE_FAILURE, "index {} exists", id);
    }

    co_return id;

}

manapi::future<> manapi::event_loop::unsubscribe_clean_up(std::size_t id) {
    if (!id) { co_return; }
    auto lk = co_await this->map_clean_up_cb_mx->lock_guard();
    this->map_clean_up_cb.erase(id);

}

manapi::future<void> manapi::event_loop::unsubscribe_finish(std::size_t id) {
    if (!id) { co_return; }
    auto lk = co_await this->map_finish_cb_mx->lock_guard();
    this->map_finish_cb.erase(id);
}

manapi::ev::loop_ref manapi::event_loop::loop() {
    return &this->loop_;
}

std::shared_ptr<manapi::ev::tcp> manapi::event_loop::create_watcher_tcp_accept( ev::tcp_accept_cb callback) {
    auto w = std::make_shared<ev::tcp>();

    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }

    w->data(new ev::internal::tcp_accept_ctx  {.s_ = w, .connection = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::tcp> manapi::event_loop::create_watcher_tcp_connection( ev::tcp_connection_cb read) {
    auto w = std::make_shared<ev::tcp>();
    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::tcp_connection_ctx  {.s_ = w, .read = std::move(read)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::udp> manapi::event_loop::create_watcher_udp(ev::udp_cb recv) {
    auto w = std::make_shared<ev::udp>();
    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::udp_ctx {.s_ = w, .recv = std::move(recv)});
    return std::move(w);
}

manapi::future<> manapi::event_loop::_call_on_finish_cb() {
    auto lk = co_await this->map_finish_cb_mx->lock_guard();
    while (!this->map_finish_cb.empty()) {
        const auto it = this->map_finish_cb.begin();
        std::size_t address = it->first;
        if (it->second) {
            auto callback = std::move(it->second);
            co_await async::invoke(std::move(callback));
        }
        this->map_finish_cb.erase(address);
    }
}

void manapi::event_loop::free_on_finish_cb_() {
    while (!this->map_clean_up_cb.empty()) {
        const auto it = this->map_clean_up_cb.begin();
        if (it->second) {
            it->second();
        }
        this->map_clean_up_cb.erase(it);
    }
}

void manapi::event_loop::stop_pool(async::promise<void>::resolve_t resolve) {
    // {
    //     std::lock_guard <std::mutex> lk (event_loop::stop_mx);
    //     event_loop::events.erase(reinterpret_cast<size_t>(this));
    // }

    resolve();
}

void manapi::event_loop::async_break_loop_(std::shared_ptr<ev::async> watcher) {
    this->taskpool_->stop();
    this->taskpool_->join();

    this->free_on_finish_cb_();

    uv_stop(&this->loop_);

    printf("1\n");
    this->map_clean_up_cb_mx->unlock();

    if (this->resolve_stop) {
    printf("2\n");
        /* if resolve caballback exists, break the loop_ otherwise */
        this->stop_pool(std::exchange(this->resolve_stop, nullptr));
    }

    printf("3\n");
    if (!this->taskpool_->size()) {
        while (this->taskpool_->try_todo_task()) {}
    }
    printf("4\n");
}

void handle_io_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_io_data_t> data) {
    switch (data->flag) {
        case ADD_IO_EVENT_INIT_FD: {
            /* init with fd */
            auto s = ev->create_watcher_fd(data->payload.init.fd, std::move(data->payload.init.cb));
            s->start(data->flags);

            data->payload.s= (std::move(s));
            break;
        }
        case ADD_IO_EVENT_INIT_SD: {
            /* init with sd */
            auto s = ev->create_watcher_socket(data->payload.init.sd, std::move(data->payload.init.cb));
            s->start(data->flags);

            data->payload.s= (std::move(s));
            break;
        }
        case ADD_IO_EVENT_START: {
            data->payload.s->start(data->flags);
            break;
        }
        case ADD_IO_EVENT_STOP: {
            data->payload.s->stop();
            break;
        }
        case ADD_IO_EVENT_REMOVE: {
            ev->stop_watcher(data->payload.s);
            break;
        }
    }

    data->resolve (std::move(data->payload.s));
}

void handle_async_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_async_data_t> data) {
    switch (data->flag) {
        case ADD_ASYNC_EVENT_INIT: {
            auto s = ev->create_watcher_async(std::move(data->payload.cb));
            data->payload.s= (std::move(s));
            break;
        }
        case ADD_ASYNC_EVENT_REMOVE: {
            ev->stop_watcher(data->payload.s);
            break;
        }
    }

    data->resolve (std::move(data->payload.s));
}

void handle_timer_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_timer_data_t> data) {
    int rhs = 0;
    switch (data->flag) {
        case ADD_TIMER_EVENT_INIT: {
            auto s = ev->create_watcher_timer(std::move(data->payload.cb));
            rhs = s->start(data->delay, data->repeat);
            data->payload.s=(std::move(s));
            break;
        }
        case ADD_TIMER_EVENT_REMOVE: {
            ev->stop_watcher(data->payload.s);
            break;
        }
        case ADD_TIMER_EVENT_STOP: {
            rhs = data->payload.s->stop();
            break;
        }
        case ADD_TIMER_EVENT_START: {
            rhs = data->payload.s->start(data->delay, data->repeat);
            break;
        }
        case ADD_TIMER_EVENT_AGAIN: {
            data->payload.s->repeat(data->repeat);
            rhs = data->payload.s->again();
            break;
        }
    }
    if (rhs) {
        data->resolve(nullptr);
    }
    else {
        data->resolve(std::move(data->payload.s));
    }
}

void manapi::event_loop::custom_watcher_poll_async(std::shared_ptr<ev::async> &w) {
    if (this->io_watcher->adding_watcher_mx->try_to_lock()) {
        while (!this->io_watcher->adding_watcher_data.empty()) {
            std::unique_ptr<ev::internal::adding_watcher_io_data_t> data = std::move(this->io_watcher->adding_watcher_data.front());
            this->io_watcher->adding_watcher_data.pop_front();

            handle_io_watcher_data (this, std::move(data));
        }

        this->io_watcher->adding_watcher_mx->unlock();
    }
}

void manapi::event_loop::custom_watcher_async_async(std::shared_ptr<ev::async> &w) {
    if (this->async_watcher->adding_watcher_mx->try_to_lock()) {
        while (!this->async_watcher->adding_watcher_data.empty()) {
            auto data = std::move(this->async_watcher->adding_watcher_data.front());
            this->async_watcher->adding_watcher_data.pop_front();

            handle_async_watcher_data(this, std::move(data));
        }

        this->async_watcher->adding_watcher_mx->unlock();
    }
}

void manapi::event_loop::custom_watcher_timer_async(std::shared_ptr<ev::async> &w) {
    if (this->timer_watcher->adding_watcher_mx->try_to_lock()) {
        while (!this->timer_watcher->adding_watcher_data.empty()) {
            auto data = std::move(this->timer_watcher->adding_watcher_data.front());
            this->timer_watcher->adding_watcher_data.pop_front();

            handle_timer_watcher_data(this, std::move(data));
        }

        this->timer_watcher->adding_watcher_mx->unlock();
    }
}

manapi::future<void> manapi::event_loop::custom_callback(std::move_only_function<void(event_loop *ev)> cb) {
    co_await async::promise<void> (this->taskpool_, [&](async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> manapi::future<> {
        auto this_ = this;
        auto data = std::make_unique<ev::internal::adding_custom_callback_data_t>(std::move(cb), std::move(resolve), std::move(reject));


        auto lk = co_await this->callback_watcher_->adding_mx->lock_guard();
        this->callback_watcher_->callback_data.push_back(std::move(data));
        lk.call();
        this_->callback_watcher_->adding_async_cb();
    });
}

#if MANAPIHTTP_CURL_DEPENDENCY
void manapi::event_loop::custom_watcher_curl_async(std::shared_ptr<ev::async> &w) {
    while (!this->curl_watcher->curl_fds.empty()) {
        this->stop_watcher(std::move(this->curl_watcher->curl_fds.front()));
        this->curl_watcher->curl_fds.pop();
    }

    if (this->curl_watcher->curl_multi_mx->try_to_lock()) {
        while (!this->curl_watcher->adding_curl_data.empty()) {
            this->handle_curl_watcher_data(std::move(this->curl_watcher->adding_curl_data.front()));
            this->curl_watcher->adding_curl_data.pop_front();
        }

        this->curl_watcher->curl_multi_mx->unlock();
    }

    this->handle_curl_exec_connections();
    this->handle_curl_check_connections();
}

void manapi::event_loop::handle_curl_exec_connections() {
    int running_handles = 0;
    auto mcode = curl_multi_perform(this->curl_watcher->curl_multi.get(), &running_handles);

    if (mcode != CURLM_OK) {
        return;
    }
}

void manapi::event_loop::handle_curl_check_connections() {

    int msgs_left;
    CURLMsg *msg;
    while (true) {
        msg = curl_multi_info_read(this->curl_watcher->curl_multi.get(), &msgs_left);

        if (!msg) {
            break;
        }

        if (msg->msg == CURLMSG_DONE) {
            curl_multi_remove_handle(this->curl_watcher->curl_multi.get(), msg->easy_handle);
            auto data = this->curl_watcher->curl_res.extract(msg->easy_handle);
            if(data.empty()) {
                continue;
            }
            if (data.mapped().watcher) {
                this->stop_watcher(data.mapped().watcher);
            }
            data.mapped().finish(msg->data.result);
        }
    }
}
#endif

void manapi::event_loop::custom_watcher_timerloop_async(std::shared_ptr<ev::async> &w) {
    if (this->timerloop->adding_timer_mx->try_to_lock()) {
        while (!this->timerloop->adding_timer_data.empty()) {
            std::unique_ptr<manapi::ev::internal::adding_timerloop_data_t> data = std::move(this->timerloop->adding_timer_data.front ());
            this->timerloop->adding_timer_data.pop_front();

            auto resolve = std::move(data->resolve);
            auto res = this->timerloop->external_cb(data.get());
            resolve(std::move(res));
        }

        this->timerloop->adding_timer_mx->unlock();
    }
}

void manapi::event_loop::custom_watcher_callback_async(std::shared_ptr<ev::async> &w) {
    if (this->callback_watcher_->adding_mx->try_to_lock()) {
        while (!this->callback_watcher_->callback_data.empty()) {
            auto data = std::move(this->callback_watcher_->callback_data.front());
            this->callback_watcher_->callback_data.pop_front();

            try {
                data->cb(this);
            }
            catch (...) {
                data->reject(std::move(std::current_exception()));

                continue;
            }

            data->resolve();
        }

        this->callback_watcher_->adding_mx->unlock();
    }
}

void manapi::event_loop::handle_tasks_do_event(std::shared_ptr<ev::prepare> &w) {
    if (!this->taskpool_->size()) {
        /** without workers */
        while (this->taskpool_->try_todo_task()) {};
    }
}
#if MANAPIHTTP_CURL_DEPENDENCY
std::shared_ptr<manapi::ev::io> manapi::event_loop::handle_curl_watcher_gen (manapi::event_loop * data, manapi::socket_t fd) {
    return data->create_watcher_fd(fd, [data, fd] (std::shared_ptr<ev::io> &w, int status, int revents)
            -> void {
        auto data2 = data;
        //MANAPIHTTP_LOG("CURL EV: {} {}", revents, (int)fd);
        int cnt; auto rhs = curl_multi_socket_action(data->curl_watcher->curl_multi.get(), fd, (revents & 0b11), &cnt);
        if (rhs != CURLM_OK) {
            data->logger_->debug(manapi::logger::default_service, "curl_multi_socket_action(...) returned an invalid response: {}", static_cast<int>(rhs));
        }
        data2->handle_curl_check_connections();
    });
}
curl_socket_t manapi::event_loop::handle_curl_open_socket(void *cbp, curlsocktype type, curl_sockaddr *addr) {
    auto data = static_cast<event_loop *>(cbp);

    manapi::socket_t fd;

    try {
        fd = manapi::async::create_socket(addr->family, addr->protocol, addr->socktype, &addr->addr, addr->addrlen);
    }
    catch (...) {
        return -1;
    }

    data->logger_->debug(manapi::logger::default_service, std::format("curl open {}",(int)fd));
    return fd;
}

int manapi::event_loop::handle_curl_socket(CURL *curl, curl_socket_t fd, int revents, void *userp, void *) {
    auto data = static_cast<event_loop *> (userp);

    if ((revents & 0b11)) {
        auto it = data->curl_watcher->watchers.find(fd);

        if (it != data->curl_watcher->watchers.end()) {
            it->second->restart(revents);
        }
        else {
            auto watcher = handle_curl_watcher_gen(data, fd);
            watcher->start(revents & 0b11);
            data->curl_watcher->watchers.insert({fd, std::move(watcher)});

            auto self = data->curl_watcher->curl_res.find(curl);
            if (self != data->curl_watcher->curl_res.end()) {
                self->second.watcher = watcher;
            }
        }
    }
    else if ((revents & CURL_POLL_REMOVE)) {
        // auto it = data->curl_watcher.watchers.find(fd);
        // it->second->events = ev::READ;
        //MANAPIHTTP_LOG("curl ev shutdown: {}",(int) fd);
        auto mapped = data->curl_watcher->watchers.extract(fd);
        if (!mapped.empty() && mapped.mapped()) {
            data->stop_watcher(mapped.mapped());
        }
    }

    return 0;
}

int manapi::event_loop::handle_curl_close_socket(void *cbp, curl_socket_t socket) {
    auto data = static_cast<event_loop *>(cbp);
    auto watcher_data = data->curl_watcher->watchers.extract(static_cast<socket_t>(socket));
    if (!watcher_data.empty() && watcher_data.mapped()) {
        data->stop_watcher(watcher_data.mapped());
    }
    data->logger_->debug(manapi::logger::default_service, std::format("curl close {}",(int)socket));
    async::close_descriptor(static_cast<socket_t>(socket));
    return 0;
}

void manapi::event_loop::handle_curl_watcher_data(std::unique_ptr<ev::internal::adding_curl_data_t> data) {
    CURLMcode mcode;

    if (data->flag==0) {
        /* add */
        curl_easy_setopt (data->curl.get(), CURLOPT_OPENSOCKETFUNCTION, handle_curl_open_socket);
        curl_easy_setopt (data->curl.get(), CURLOPT_OPENSOCKETDATA, this);
        //
        curl_easy_setopt (data->curl.get(), CURLOPT_CLOSESOCKETFUNCTION, handle_curl_close_socket);
        curl_easy_setopt (data->curl.get(), CURLOPT_CLOSESOCKETDATA, this);

        mcode = curl_multi_add_handle(this->curl_watcher->curl_multi.get(), data->curl.get());

        if (mcode != CURLM_OK) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "Failed to add the curl handle. curl_multi_add_handle(...) = {}", static_cast<int>(mcode)));
            data->reject(std::move(err));

        }
        else {
            this->curl_watcher->curl_res.insert({data->curl.get(), {data->curl, std::move(data->finish), nullptr}});

            data->resolve();
        }
    }
    else if (data->flag==1) {
        /* remove */
        auto curl_data = this->curl_watcher->curl_res.extract(data->curl.get());
        if (curl_data.empty()) {
            /** already was removed */
            data->resolve();
            return;
        }

        mcode = curl_multi_remove_handle(this->curl_watcher->curl_multi.get(), data->curl.get());

        if (mcode != CURLM_OK) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "Failed to remove the curl handle. curl_multi_remove_handle(...) = {}", static_cast<int>(mcode)));
            data->reject(std::move(err));
        }
        else {
            data->resolve();
        }

        if (curl_data.mapped().watcher) {
            this->stop_watcher(curl_data.mapped().watcher);
        }
        curl_data.mapped().finish(CURLE_ABORTED_BY_CALLBACK);
    }
    else if (data->flag==2) {
        /* pause */
        const auto rhs = curl_easy_pause(data->curl.get(), CURLPAUSE_ALL);
        if (CURLE_OK != rhs) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl_easy_pause(...) with CURLPAUSE_ALL failed -> {}", static_cast<int>(rhs)));

            data->reject(std::move(err));
        }
        else {
            data->resolve();
        }
    }
    else if (data->flag==3) {
        /* unpause */
        const auto rhs = curl_easy_pause(data->curl.get(), CURLPAUSE_CONT);
        if (CURLE_OK != rhs) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "curl_easy_pause(...) with CURLPAUSE_CONT failed -> {}", static_cast<int>(rhs)));
            data->reject(std::move(err));
        }
        else {
            data->resolve();
        }
    }
    else if (data->flag==4) {
        /* callback */
        try {
            data->finish (CURLE_OK);
        }
        catch (...) {

        }
        data->resolve();
    }
}
#endif

manapi::future<std::shared_ptr<manapi::ev::fs>> template_fs_watcher_(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_,
    manapi::ev::internal::fs_watcher_t *w, std::unique_ptr<manapi::ev::internal::adding_watcher_fs_data_t> data) {

    typedef std::shared_ptr<manapi::ev::fs> ret;
    co_return co_await manapi::async::promise<ret> (taskpool_,
        [&] (manapi::async::promise<ret>::resolve_t resolve, manapi::async::promise<ret>::reject_t reject) mutable
        -> manapi::future<> {
            data->resolve = std::move(resolve);
            data->reject = std::move(reject);

            auto w_ = w;
            auto lk = co_await w_->adding_watcher_mx->lock_guard();
            w_->adding_watcher_data.push_back(std::move(data));
            lk.call();
            w_->adding_watcher_async_cb();
    });
}

manapi::future<std::shared_ptr<manapi::ev::io>> template_io_watcher_(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_,
    manapi::ev::internal::io_watcher_t *w, std::unique_ptr<manapi::ev::internal::adding_watcher_io_data_t> data) {

    typedef std::shared_ptr<manapi::ev::io> ret;
    co_return co_await manapi::async::promise<ret> (taskpool_,
        [&] (manapi::async::promise<ret>::resolve_t resolve, manapi::async::promise<ret>::reject_t reject) mutable
        -> manapi::future<> {
            data->resolve = std::move(resolve);
            auto w_ = w;
            auto lk = co_await w_->adding_watcher_mx->lock_guard();
            w_->adding_watcher_data.push_back(std::move(data));
            lk.call();
            w_->adding_watcher_async_cb();
    });
}

manapi::future<std::shared_ptr<manapi::ev::async>> template_async_watcher_(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_,
    manapi::ev::internal::async_watcher_t *w, std::unique_ptr<manapi::ev::internal::adding_watcher_async_data_t> data) {

    typedef std::shared_ptr<manapi::ev::async> ret;
    co_return co_await manapi::async::promise<ret> (taskpool_,
        [&] (manapi::async::promise<ret>::resolve_t resolve, manapi::async::promise<ret>::reject_t reject) mutable
        -> manapi::future<> {
            data->resolve = std::move(resolve);
            auto w_ = w;
            auto lk = co_await w_->adding_watcher_mx->lock_guard();
            w_->adding_watcher_data.push_back(std::move(data));
            lk.call();
            w_->adding_watcher_async_cb();
    });
}

manapi::future<std::shared_ptr<manapi::ev::timer>> template_timer_watcher_(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_,
    manapi::ev::internal::timer_watcher_t *w, std::unique_ptr<manapi::ev::internal::adding_watcher_timer_data_t> data) {

    typedef std::shared_ptr<manapi::ev::timer> ret;
    auto res =  co_await manapi::async::promise<ret> (taskpool_,
        [&] (manapi::async::promise<ret>::resolve_t resolve, manapi::async::promise<ret>::reject_t reject) mutable
        -> manapi::future<> {
            data->resolve = std::move(resolve);
            auto w_ = w;
            auto lk = co_await w_->adding_watcher_mx->lock_guard();
            w_->adding_watcher_data.push_back(std::move(data));
            lk.call();
            w_->adding_watcher_async_cb();
    });
    if (res) {
        co_return std::move(res);
    }

    THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_WATCHER_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_WATCHER_COMMAND_FAILED]);
}

manapi::future<std::shared_ptr<manapi::ev::io>> _template_async_watcher(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_,
    manapi::ev::internal::io_watcher_t *w, std::unique_ptr<manapi::ev::internal::adding_watcher_io_data_t> data) {

    typedef std::shared_ptr<manapi::ev::io> ret;
    co_return co_await manapi::async::promise<ret> (taskpool_,
        [&] (manapi::async::promise<ret>::resolve_t resolve, manapi::async::promise<ret>::reject_t reject) mutable
        -> manapi::future<> {
            data->resolve = std::move(resolve);
            auto w_ = w;
            auto lk = co_await w_->adding_watcher_mx->lock_guard();
            w_->adding_watcher_data.push_back(std::move(data));
            lk.call();
            w_->adding_watcher_async_cb();
    });
}

#if MANAPIHTTP_CURL_DEPENDENCY
manapi::future<> template_watcher_curl_(std::shared_ptr<manapi::threadpool<manapi::task>> taskpool_, manapi::ev::internal::curl_watcher_t *curl_watcher,
    int flag, std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb) {

    co_await manapi::async::promise<void> (taskpool_, [&] (manapi::async::promise<void>::resolve_t resolve,
        manapi::async::promise<void>::reject_t reject) -> manapi::future<void> {

        auto data = std::make_unique<manapi::ev::internal::adding_curl_data_t>(
            flag,
            curl,
            std::move(cb),
            std::move(resolve),
            std::move(reject)
        );

        auto curl_watcher_ = curl_watcher;

        auto lk = co_await curl_watcher_->curl_multi_mx->lock_guard();
        curl_watcher_->adding_curl_data.push_back(std::move(data));
        lk.call();

        curl_watcher_->adding_curl_async_cb();
    });
}
#endif
manapi::future<std::optional<manapi::timer>> manapi::event_loop::template_cmd_timer_(int flag, size_t data, std::move_only_function<manapi::future<>(manapi::timer t)> cb_async, std::move_only_function<void(manapi::timer t)> cb_sync) {

    co_return co_await async::promise<std::optional<manapi::timer>> (this->taskpool_, [&] (async::promise<std::optional<manapi::timer>>::resolve_t resolve, async::promise<std::optional<manapi::timer>>::reject_t reject) -> future<void> {
        auto data1 = std::make_unique<ev::internal::adding_timerloop_data_t>(
            flag,
            data,
            std::move(cb_sync),
            std::move(cb_async),
            std::move(resolve),
            std::move(reject)
        );



        auto this_ = this;
        auto lk = co_await this->timerloop->adding_timer_mx->lock_guard();
        this->timerloop->adding_timer_data.push_back(std::move(data1));
        lk.call();
        this_->timerloop->adding_timer_async_cb();

        co_return;
    });
}

void manapi::event_loop::stop_watcher_tcp_accept(std::shared_ptr<ev::tcp> s) {
    s->unbind(); auto data = static_cast<ev::internal::tcp_accept_ctx *>(s->data());
    s->data(nullptr);
    if (data) { data->s_.reset(); delete data; }
}

void manapi::event_loop::stop_watcher_tcp_connection(std::shared_ptr<ev::tcp> s) {
    s->unbind(); auto data = static_cast<ev::internal::tcp_connection_ctx *>(s->data());
    s->data(nullptr);
    if (data) { data->s_.reset(); delete data; }
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_open(std::string path, int flags, int mode, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_OPEN, nullptr, std::move(path), std::string{}, flags, mode, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_write(ev::file file, const void *buffer, ssize_t size, ev::fs_cb callback, int64_t offset) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_WRITE, buffer, std::string{}, std::string{}, size, offset, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_read(ev::file file, void *buffer, ssize_t size, ev::fs_cb callback, int64_t offset) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_READ, buffer, std::string{}, std::string{}, size, offset, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_close(ev::file file, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_CLOSE, nullptr, std::string{}, std::string{}, 0, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_fstat(ev::file file, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FSTAT, nullptr, std::string{}, std::string{}, 0, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_unlink(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_UNLINK, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_mkdir(std::string path, int mode,
ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_MKDIR, nullptr, std::move(path), std::string{}, mode, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_mkdtemp(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_MKDTEMP, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_mkstemp(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_MKSTEMP, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_rmdir(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_RMDIR, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_opendir(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_OPENDIR, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_closedir(ev::dir_t *dir, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_CLOSEDIR, nullptr, std::string{}, std::string{},
        reinterpret_cast<ssize_t>(dir), 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_readdir(ev::dir_t *dir, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_READDIR, nullptr, std::string{}, std::string{},
        reinterpret_cast<ssize_t>(dir), 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_scandir(std::string path, int flags,
    ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_SCANDIR, nullptr, std::move(path), std::string{}, flags, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_scandir_next(ev::dirent_t *dirent,
    ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_SCANDIR_NEXT, nullptr, std::string{}, std::string{},
        reinterpret_cast<ssize_t>(dirent), 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_stat(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_STAT, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_lstat(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_LSTAT, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_statfs(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_STATFS, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_rename(std::string path, std::string new_path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_RENAME, nullptr, std::move(path), std::move(new_path), 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_fsync(ev::file file, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FSYNC, nullptr, std::string{}, std::string{}, 0, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_fdatasync(ev::file file, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FDATASYNC, nullptr, std::string{}, std::string{}, 0, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_ftruncate(ev::file file, int64_t offset, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FTRUNCATE, nullptr, std::string{}, std::string{}, offset, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_copyfile(std::string path, std::string new_path, int flags, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_COPYFILE, nullptr, std::move(path), std::move(new_path), flags, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_sendfile(ev::file outfd, ev::file infd, int64_t offset, size_t length, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_SENDFILE, nullptr, std::string{}, std::string{}, offset, static_cast<ssize_t>(length), outfd, std::move(callback), nullptr, nullptr, infd);
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_access(std::string path, int mode, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_ACCESS, nullptr, std::move(path), std::string{}, mode, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_chmod(std::string path, int mode, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_CHMOD, nullptr, std::move(path), std::string{}, mode, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_fchmod(ev::file file, int mode, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FCHMOD, nullptr, std::string{}, std::string{}, mode, 0, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_utime(std::string path, double atime, double mtime, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_UTIME, nullptr, std::move(path), std::string{},  double_store_in_ssize(atime), double_store_in_ssize(mtime), 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_futime(ev::file file, double atime, double mtime, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FUTIME, nullptr, std::string{}, std::string{}, double_store_in_ssize(atime), double_store_in_ssize(mtime), file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_lutime(std::string path, double atime, double mtime, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_LUTIME, nullptr, std::move(path), std::string{},  double_store_in_ssize(atime), double_store_in_ssize(mtime), 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_link(std::string path, std::string new_path,ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_LINK, nullptr, std::move(path), std::move(new_path), 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_symlink(std::string path, std::string new_path,int flags, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_SYMLINK, nullptr, std::move(path), std::move(new_path), flags, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_readlink(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_READLINK, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_realpath(std::string path, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_REALPATH, nullptr, std::move(path), std::string{}, 0, 0, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_chown(std::string path, ev::uid_t uid,ev::gid_t gid, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_CHOWN, nullptr, std::move(path), std::string{}, uid, gid, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_fchown(ev::file file, ev::uid_t uid,ev::gid_t gid, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_FCHOWN, nullptr, std::string{}, std::string{}, uid, gid, file, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::fs_lchown(std::string path, ev::uid_t uid,ev::gid_t gid, ev::fs_cb callback) {
    auto data = std::make_unique<ev::internal::adding_watcher_fs_data_t>(ADD_FS_EVENT_LCHOWN, nullptr, std::move(path), std::string{}, uid, gid, 0, std::move(callback));
    return template_fs_watcher_ (this->taskpool_, this->fs_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::io>> manapi::event_loop::watch_poll(fd_t fd, int flags, ev::io_cb callback) {

    auto data = std::make_unique<manapi::ev::internal::adding_watcher_io_data_t>(ADD_IO_EVENT_INIT_FD, flags, ev::internal::adding_watcher_io_data_payload_t{}, nullptr);
    data->payload.init.cb = std::move(callback);
    data->payload.init.fd = fd;

    co_return co_await template_io_watcher_(this->taskpool_, this->io_watcher.get(), std::move(data));
}

manapi::future<> manapi::event_loop::unwatch_poll(std::shared_ptr<manapi::ev::io> w) {
    ev::internal::adding_watcher_io_data_payload_t payload;
    payload.s = std::move(w);
    auto data = std::make_unique<ev::internal::adding_watcher_io_data_t>(ADD_IO_EVENT_REMOVE, 0, std::move(payload), nullptr);
    co_await template_io_watcher_(this->taskpool_, this->io_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::io>> manapi::event_loop::watch_poll_socket(socket_t sock, int flags, ev::io_cb callback) {

    auto data = std::make_unique<manapi::ev::internal::adding_watcher_io_data_t>(ADD_IO_EVENT_INIT_SD, flags, ev::internal::adding_watcher_io_data_payload_t{}, nullptr);

    data->payload.init.cb = std::move(callback);
    data->payload.init.sd = sock;

    co_return co_await template_io_watcher_(this->taskpool_, this->io_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::async>> manapi::event_loop::watch_async(ev::async_cb callback) {
    auto data = std::make_unique<manapi::ev::internal::adding_watcher_async_data_t>(ADD_ASYNC_EVENT_INIT, ev::internal::adding_watcher_async_data_payload_t{}, nullptr);
    data->payload.cb = std::move(callback);
    co_return co_await template_async_watcher_(this->taskpool_, this->async_watcher.get(), std::move(data));
}

manapi::future<> manapi::event_loop::unwatch_async(std::shared_ptr<ev::async> w) {
    auto data = std::make_unique<manapi::ev::internal::adding_watcher_async_data_t>(ADD_ASYNC_EVENT_REMOVE, ev::internal::adding_watcher_async_data_payload_t{}, nullptr);
    data->payload.s = std::move(w);
    co_await template_async_watcher_(this->taskpool_, this->async_watcher.get(), std::move(data));
}

manapi::future<std::shared_ptr<manapi::ev::timer>> manapi::event_loop::watch_timer(uint64_t delay, uint64_t repeat, ev::timer_cb cb) {
    auto data = std::make_unique<ev::internal::adding_watcher_timer_data_t>(ADD_TIMER_EVENT_INIT, delay, repeat, ev::internal::adding_watcher_timer_data_payload_t{}, nullptr);
    data->payload.cb = (std::move(cb));
    co_return co_await template_timer_watcher_(this->taskpool_, this->timer_watcher.get(), std::move(data));
}

manapi::future<> manapi::event_loop::unwatch_timer(std::shared_ptr<ev::timer> w) {
    auto data = std::make_unique<ev::internal::adding_watcher_timer_data_t>(ADD_TIMER_EVENT_REMOVE, 0, 0, ev::internal::adding_watcher_timer_data_payload_t{}, nullptr);
    data->payload.s= (std::move(w));
    co_await template_timer_watcher_(this->taskpool_, this->timer_watcher.get(), std::move(data));
}

manapi::future<> manapi::event_loop::again_timer(uint64_t repeat, std::shared_ptr<ev::timer> w) {
    auto data = std::make_unique<ev::internal::adding_watcher_timer_data_t>(ADD_TIMER_EVENT_AGAIN, 0, repeat, ev::internal::adding_watcher_timer_data_payload_t{}, nullptr);
    data->payload.s=(std::move(w));
    co_await template_timer_watcher_(this->taskpool_, this->timer_watcher.get(), std::move(data));
}

manapi::future<> manapi::event_loop::stop_poll(std::shared_ptr<ev::io> w) {
    ev::internal::adding_watcher_io_data_payload_t payload;
    payload.s = std::move(w);
    auto data = std::make_unique<ev::internal::adding_watcher_io_data_t>(ADD_IO_EVENT_STOP, 0, std::move(payload), nullptr);
    co_await template_io_watcher_(this->taskpool_, this->io_watcher.get(), std::move(data));
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::event_loop::taskpool() const {
    return this->taskpool_;
}
#if MANAPIHTTP_CURL_DEPENDENCY
manapi::future<> manapi::event_loop::watch_curl(std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb) {
    return template_watcher_curl_(this->taskpool_, this->curl_watcher.get(), 0, curl, std::move(cb));
}

manapi::future<> manapi::event_loop::unwatch_curl(std::shared_ptr<CURL> curl) {
    return template_watcher_curl_(this->taskpool_, this->curl_watcher.get(), 1, curl, nullptr);
}

manapi::future<> manapi::event_loop::pause_watch_curl(std::shared_ptr<CURL> curl) {
    return template_watcher_curl_(this->taskpool_, this->curl_watcher.get(), 2, curl, nullptr);
}

manapi::future<> manapi::event_loop::unpause_watch_curl(std::shared_ptr<CURL> curl) {
    return template_watcher_curl_(this->taskpool_, this->curl_watcher.get(), 3, curl, nullptr);
}

manapi::future<> manapi::event_loop::custom_cb_curl(std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb) {
    return template_watcher_curl_(this->taskpool_, this->curl_watcher.get(), 4, curl, std::move(cb));
}
#endif

manapi::future<manapi::timer> manapi::event_loop::append_async_timer(size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb) {
    co_return std::move((co_await this->template_cmd_timer_ (0, time, std::move(cb), nullptr)).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_sync_timer(size_t time, std::move_only_function<void(manapi::timer t)> cb) {
    co_return std::move((co_await this->template_cmd_timer_ (0, time, nullptr, std::move(cb))).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_async_interval(size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb) {
    co_return std::move((co_await this->template_cmd_timer_ (1, time, std::move(cb), nullptr)).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_sync_interval(size_t time, std::move_only_function<void(manapi::timer t)> cb) {
    co_return std::move((co_await this->template_cmd_timer_ (1, time, nullptr, std::move(cb))).value());
}

manapi::future<void> manapi::event_loop::update_state_interval(size_t id) {
    co_await this->template_cmd_timer_(3, id, nullptr, nullptr);
}

manapi::future<> manapi::event_loop::remove_timer(size_t id) {
    co_await this->template_cmd_timer_ (2, id, nullptr, nullptr);
}

void manapi::event_loop::callback_io_watcher(std::shared_ptr<ev::io> w, ev::io_cb cb) {
    auto data = static_cast<ev::internal::io_ctx *> (w->data());
    data->cb = std::move(cb);
}

void manapi::event_loop::timer_callback(std::move_only_function<std::optional<manapi::timer>(ev::internal::adding_timerloop_data_t *data)> cb) {
    this->timerloop->external_cb = std::move(cb);
}

void manapi::event_loop::interrupt() {
    std::unique_lock <std::mutex> lk (event_loop::stop_mx, std::try_to_lock);
    event_loop::interrupted.store(true);

    for (auto &loop_ :  manapi::event_loop::events) {
        loop_.second->interrupted_watcher_->send();
    }

    manapi::event_loop::events.clear();
}

void manapi::event_loop::custom_watcher_fs_async(std::shared_ptr<ev::async> &w) {
    if (this->fs_watcher->adding_watcher_mx->try_to_lock()) {
        while (!this->fs_watcher->adding_watcher_data.empty()) {
            auto data = std::move(this->fs_watcher->adding_watcher_data.front());
            this->fs_watcher->adding_watcher_data.pop_front();

            std::shared_ptr<ev::fs> s = this->create_watcher_fs(std::move(data->cb));
            int rhs = 0;

            switch (data->flag) {
                case ADD_FS_EVENT_OPEN:
                    rhs = s->open(data->path1.data(), static_cast<int>(data->s1), static_cast<int>(data->s2));
                    break;

                case ADD_FS_EVENT_READ: {
                    const uv_buf_t buf[] = {{.base = (char*)(data->data), .len = static_cast<std::size_t>(data->s1)}};
                    rhs = s->read(data->file, buf, 1, data->s2);
                    break;
                }
                case ADD_FS_EVENT_WRITE: {
                    const uv_buf_t buf[] = {{.base = (char*)(data->data), .len = static_cast<std::size_t>(data->s1)}};
                    rhs = s->write(data->file, buf, 1, data->s2);
                    break;
                }
                case ADD_FS_EVENT_CLOSE: {
                    rhs = s->close(data->file);
                    break;
                }
                case ADD_FS_EVENT_UNLINK:
                    rhs = s->unlink(data->path1.data());
                    break;
                case ADD_FS_EVENT_MKDIR:
                    rhs = s->mkdir(data->path1.data(), static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_MKDTEMP:
                    rhs = s->mkdtemp(data->path1.data());
                    break;
                case ADD_FS_EVENT_MKSTEMP:
                    rhs = s->mkstemp(data->path1.data());
                    break;
                case ADD_FS_EVENT_RMDIR:
                    rhs = s->rmdir(data->path1.data());
                    break;
                case ADD_FS_EVENT_OPENDIR:
                    rhs = s->opendir(data->path1.data());
                    break;
                case ADD_FS_EVENT_READDIR:
                    rhs = s->readdir(reinterpret_cast<ev::dir_t *>(data->s1));
                    break;
                case ADD_FS_EVENT_CLOSEDIR:
                    rhs = s->closedir(reinterpret_cast<ev::dir_t *>(data->s1));
                    break;
                case ADD_FS_EVENT_SCANDIR:
                    rhs = s->scandir(data->path1.data(), static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_SCANDIR_NEXT:
                    rhs = s->scandir_next(reinterpret_cast<ev::dirent_t *>(data->s1));
                    break;
                case ADD_FS_EVENT_STAT:
                    rhs = s->stat(data->path1.data());
                    break;
                case ADD_FS_EVENT_FSTAT:
                    rhs = s->fstat(data->file);
                    break;
                case ADD_FS_EVENT_LSTAT:
                    rhs = s->lstat(data->path1.data());
                    break;
                case ADD_FS_EVENT_STATFS:
                    rhs = s->statfs(data->path1.data());
                    break;
                case ADD_FS_EVENT_RENAME:
                    rhs = s->rename(data->path1.data(), data->path2.data());
                    break;
                case ADD_FS_EVENT_FSYNC:
                    rhs = s->fsync(data->file);
                    break;
                case ADD_FS_EVENT_FDATASYNC:
                    rhs = s->fdatasync(data->file);
                    break;
                case ADD_FS_EVENT_FTRUNCATE:
                    rhs = s->ftruncate(data->file, data->s1);
                    break;
                case ADD_FS_EVENT_COPYFILE:
                    rhs = s->copyfile(data->path1.data(), data->path2.data(), data->s1);
                    break;
                case ADD_FS_EVENT_SENDFILE:
                    rhs = s->sendfile(data->file, data->file2, data->s1, (size_t)(data->s2));
                    break;
                case ADD_FS_EVENT_ACCESS:
                    rhs = s->access(data->path1.data(), static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_CHMOD:
                    rhs = s->chmod(data->path1.data(), static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_FCHMOD:
                    rhs = s->fchmod(data->file, static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_UTIME:
                    rhs = s->utime(data->path1.data(), ssize_store_in_double(data->s1), ssize_store_in_double(data->s2));
                    break;
                case ADD_FS_EVENT_FUTIME:
                    rhs = s->futime(data->file, ssize_store_in_double(data->s1), ssize_store_in_double(data->s2));
                    break;
                case ADD_FS_EVENT_LUTIME:
                    rhs = s->lutime(data->path1.data(), ssize_store_in_double(data->s1), ssize_store_in_double(data->s2));
                    break;
                case ADD_FS_EVENT_SYMLINK:
                    rhs = s->symlink(data->path1.data(), data->path2.data(), static_cast<int>(data->s1));
                    break;
                case ADD_FS_EVENT_READLINK:
                    rhs = s->readlink(data->path1.data());
                    break;
                case ADD_FS_EVENT_REALPATH:
                    rhs = s->realpath(data->path1.data());
                    break;
                case ADD_FS_EVENT_CHOWN:
                    rhs = s->chown(data->path1.data(), data->s1, data->s2);
                    break;
                case ADD_FS_EVENT_FCHOWN:
                    rhs = s->fchown(data->file, data->s1, data->s2);
                    break;
                case ADD_FS_EVENT_LCHOWN:
                    rhs = s->lchown(data->path1.data(), data->s1, data->s2);
                    break;
                case ADD_FS_EVENT_LINK:
                    rhs = s->link(data->path1.data(), data->path2.data());
                    break;
            }

            if (rhs) {
                /* error */
                data->reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "fs command({}) failed with error: {}",
                    static_cast<int>(data->flag), rhs)));
                return;
            }

            data->resolve(std::move(s));
        }

        this->fs_watcher->adding_watcher_mx->unlock();
    }
}

std::shared_ptr<manapi::ev::io> manapi::event_loop::create_watcher_fd(fd_t fd, ev::io_cb callback) {
    auto w = std::make_shared<ev::io>(&this->loop_, fd);
    w->data(new ev::internal::io_ctx  {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::io> manapi::event_loop::create_watcher_socket(socket_t sock, ev::io_cb callback) {
    auto w = std::make_shared<ev::io>(&this->loop_, sock);
    w->data(new ev::internal::io_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::async> manapi::event_loop::create_watcher_async(ev::async_cb callback) {
    auto w = std::make_shared<ev::async>();
    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::async_ctx { .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::timer> manapi::event_loop::create_watcher_timer(ev::timer_cb callback) {
    auto w = std::make_shared<ev::timer>();
    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::timer_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::prepare> manapi::event_loop::create_watcher_prepare(ev::prepare_cb callback) {
    auto w = std::make_shared<ev::prepare>();
    if (auto rhs = w->bind(&this->loop_)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::prepare_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::fs> manapi::event_loop::create_watcher_fs(ev::fs_cb callback) {
    auto w = std::make_shared<ev::fs>(&this->loop_);
    w->data(new ev::internal::fs_ctx{ .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::random> manapi::event_loop::create_watcher_random(ev::random_cb callback, char *buff, std::size_t size) {
    auto w = std::make_shared<ev::random>();
    if (auto rhs = w->bind(&this->loop_, buff, size)) {
        throw manapi::exception (manapi::ERR_WATCHER_BIND, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED],
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
    w->data(new ev::internal::random_ctx{ .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

MANAPI_EV_UNWATCHER(idle, idle_ctx);
MANAPI_EV_UNWATCHER(io, io_ctx);
MANAPI_EV_UNWATCHER(udp, udp_ctx);
MANAPI_EV_UNWATCHER(async, async_ctx);
MANAPI_EV_UNWATCHER(check, check_ctx);
MANAPI_EV_UNWATCHER(timer, timer_ctx);
MANAPI_EV_UNWATCHER(write, write_ctx);
MANAPI_EV_UNWATCHER(udp_send, udp_send_ctx);
MANAPI_EV_UNWATCHER(prepare, prepare_ctx);
MANAPI_EV_UNWATCHER(fs, fs_ctx);

void manapi::event_loop::pool_(manapi::before_delete lk2, std::shared_ptr<event_loop> le) {
    {
        std::lock_guard<std::mutex> lk (event_loop::stop_mx);

        if (event_loop::interrupted) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INTERRUPTED, "Failed to create a events loop_");
        }

        event_loop::events.insert({reinterpret_cast<size_t> (this), std::move(le)});
    }

    this->stop_watcher_ = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->async_break_loop_(w); });

    auto init_watcher = this->create_watcher_async([&lk2] (std::shared_ptr<ev::async> &w)
        -> void { w->unbind(); lk2.call(); });

    init_watcher->send();

    uv_run(&this->loop_, UV_RUN_DEFAULT);

    /* if init_watcher(...) was not called */
    lk2.call();

    this->stop_watcher(init_watcher);
    this->stop_watcher(this->stop_watcher_);
}