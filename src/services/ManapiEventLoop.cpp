#include <csignal>

#include "services/ManapiEventLoop.hpp"

#include <memory>
#include <cstring>

#include "async/ManapiAsyncSocket.hpp"
#include "components/TimerObject.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "async/ManapiAsyncThreadsMutex.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

#define MANAPI_EV_UNWATCHER(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) { \
    if (w->data()) { \
        w->unbind(+[](uv_handle_t *handle) -> void { auto data = static_cast<ev::internal::ctxname *>(handle->data); \
        handle->data = nullptr; if (data) { data->s_.reset(); delete data; } }); \
    } }

#define MANAPI_EV_UNWATCHER2(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) { \
    if (w->data()) { \
        w->unbind(+[](uv_handle_t *handle) -> void { auto data = static_cast<ev::internal::ctxname *>(handle->data); \
        handle->data = nullptr; if (data->close_cb) { data->close_cb->operator()(data->s_); } if (data) { data->s_.reset(); delete data; } }); \
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

    struct tcp_ctx {
        char type;
        std::shared_ptr<ev::tcp> s_;
        std::unique_ptr<close_cb_t<ev::tcp>> close_cb;
    };

    struct tcp_accept_ctx : tcp_ctx {
        ev::tcp_accept_cb connection;
    };

    struct tcp_connection_ctx : tcp_ctx {
        ev::tcp_connection_cb read;
        ev::tcp_alloc_cb alloc_cb;
    };

    struct connect_tcp_ctx {
        ev::shared_tcp tcp;
        ev::connect_tcp_cb cb;
    };

    struct udp_ctx {
        std::unique_ptr<close_cb_t<ev::udp>> close_cb;
        std::shared_ptr<ev::udp> s_;
        ev::udp_cb recv;
        ev::udp_alloc_cb alloc_cb;
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

// #if MANAPIHTTP_CURL_DEPENDENCY
//     struct adding_curl_data_t {
//         int flag{0};
//         std::shared_ptr<CURL> curl{nullptr};
//         std::move_only_function<void(CURLcode result)> finish{nullptr};
//         manapi::async::promise<void>::resolve_t resolve{nullptr};
//         manapi::async::promise<void>::reject_t reject{nullptr};
//     };
// #endif

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
        //std::shared_ptr<manapi::async::mutex> curl_multi_mx{nullptr};
        // std::shared_ptr<ev::async> adding_curl_multi_async{nullptr};
        // manapi::chain<std::unique_ptr<adding_curl_data_t>> adding_curl_data{};
        //
        // std::move_only_function<void()> adding_curl_async_cb{nullptr};
        // std::queue<std::shared_ptr<ev::io>> curl_fds{};
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
        std::shared_ptr<manapi::async::tmutex> adding_mx;
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

void manapi::ev::callback_watcher_connect_tcp(uv_connect_t *s, int status) {
    static_cast<manapi::ev::internal::connect_tcp_ctx *> (s->data)
        ->cb (static_cast<manapi::ev::internal::connect_tcp_ctx *> (s->data)->tcp, status);
}


void manapi::ev::callback_watcher_fs(uv_fs_t *req) {
    if (req->data) {
        /* otherwise it was cancelled */
        auto w = std::move(static_cast<manapi::ev::internal::fs_ctx *> (req->data)->s_);
        {
            fs_req_deleter deleter (req);
            static_cast<manapi::ev::internal::fs_ctx *> (req->data)
                ->cb(w);
        }
        delete static_cast<manapi::ev::internal::fs_ctx *> (req->data);
        req->data = nullptr;
    }
    else {
        fs_req_deleter deleter (req);
    }
}

void manapi::ev::callback_watcher_random(uv_random_t *s, int status, void *buff, std::size_t size) {
    if (s->data) {
        /* otherwise it was cancelled */
        static_cast<manapi::ev::internal::random_ctx *> (s->data)
            ->cb(static_cast<manapi::ev::internal::random_ctx *> (s->data)->s_, status, buff, size);
        delete static_cast<manapi::ev::internal::random_ctx *> (s->data);
        s->data = nullptr;
    }
}

void manapi::ev::callback_watcher_tcp_connection_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    static_cast<manapi::ev::internal::tcp_connection_ctx *> (handle->data)
        ->alloc_cb(static_cast<manapi::ev::internal::tcp_connection_ctx *> (handle->data)->s_, suggested_size, buf);
}

void manapi::ev::callback_watcher_udp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    static_cast<manapi::ev::internal::udp_ctx *> (handle->data)
        ->alloc_cb(static_cast<manapi::ev::internal::udp_ctx *> (handle->data)->s_, suggested_size, buf);
}

void manapi::ev::callback_close_cb(uv_handle_t *s) {
    switch (s->type) {
        case ev::EV_TCP: {
            if (static_cast<manapi::ev::internal::tcp_ctx *> (s->data)) {
                static_cast<manapi::ev::internal::tcp_ctx *> (s->data)->
                    close_cb->operator()(static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)->s_);
            }
            break;
        }
        default: {

            break;
        }
    }
}


ssize_t double_store_in_ssize (double a) { ssize_t b = 0; memcpy (&b, &a, sizeof (a)); return a; }
double ssize_store_in_double (ssize_t a) { double b = 0; memcpy (&b, &a, sizeof (a)); return b; };


manapi::event_loop::event_loop(std::shared_ptr<threadpool<task>> taskpool_, std::shared_ptr<manapi::logger> logger) {
    this->loop_ = std::make_unique<uv_loop_t>();
    assert(!(uv_loop_init(this->loop_.get())));

    // this->async_watcher = std::make_unique<ev::internal::async_watcher_t>();
    // this->io_watcher = std::make_unique<ev::internal::io_watcher_t>();
    // this->fs_watcher = std::make_unique<ev::internal::fs_watcher_t>();
    // this->timer_watcher = std::make_unique<ev::internal::timer_watcher_t>();
    //
#ifdef MANAPIHTTP_CURL_DEPENDENCY
    this->curl_watcher = std::make_unique<ev::internal::curl_watcher_t>();
#endif
    // this->timerloop = std::make_unique<ev::internal::timerloop_t>();
    this->callback_watcher_ = std::make_unique<ev::internal::custom_callback_t>();

    this->idle_tasks_ = this->create_watcher_idle([this] (ev::shared_idle &w)
        -> void {
        this->try_tasks_(w);
    });

    this->etaskpool_ = std::make_shared<manapi::ethreadpool<task>>(logger, [this] ()
        -> void {
        this->idle_tasks_->start();
    });

    dynamic_cast<ethreadpool<task> *>(this->etaskpool_.get())->set_notify();

    this->mx = std::make_shared<async::mutex>();
    this->logger_ = std::move(logger);

    // this->async_watcher->adding_watcher_mx = std::make_shared<async::mutex>();
    // this->io_watcher->adding_watcher_mx = std::make_shared<async::mutex>();
    // this->timer_watcher->adding_watcher_mx = std::make_shared<async::mutex>();
    // this->fs_watcher->adding_watcher_mx = std::make_shared<async::mutex>();
    //
    // this->async_watcher->adding_watcher_data = {};
    // this->io_watcher->adding_watcher_data = {};
    // this->timer_watcher->adding_watcher_data = {};
    // this->fs_watcher->adding_watcher_data = {};
    //
    // this->timerloop->adding_timer_mx = std::make_shared<async::mutex>();
    this->callback_watcher_->adding_mx = std::make_shared<async::tmutex>();

    this->taskpool_ = std::move(taskpool_);
    this->status = false;

    // this->fs_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_fs_async(w); });
    //
    // this->async_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_async_async(w); });
    //
    // this->io_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_poll_async(w); });
    //
    // this->timer_watcher->adding_watcher_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_timer_async(w); });
    //
    // this->timerloop->adding_timer_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_timerloop_async(w); });
    //
    this->callback_watcher_->adding_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_callback_async(w); });
    //
    // this->timer_watcher->adding_watcher_async_cb = [this] ()
    //     -> void { this->timer_watcher->adding_watcher_async->send(); };
    // this->io_watcher->adding_watcher_async_cb = [this] ()
    //     -> void { this->io_watcher->adding_watcher_async->send(); };
    // this->async_watcher->adding_watcher_async_cb = [this] ()
    //     -> void { this->async_watcher->adding_watcher_async->send(); };
    // this->timerloop->adding_timer_async_cb = [this] ()
    //     -> void { this->timerloop->adding_timer_async->send(); };
    this->callback_watcher_->adding_async_cb = [this] ()
        -> void { this->callback_watcher_->adding_async->send(); };
    // this->fs_watcher->adding_watcher_async_cb = [this] ()
    //     -> void { this->fs_watcher->adding_watcher_async->send(); };

#if MANAPIHTTP_CURL_DEPENDENCY
    //this->curl_watcher->curl_multi_mx = std::make_shared<async::mutex>(this->etaskpool_);
    this->curl_watcher->curl_multi.reset(curl_multi_init());
    // this->curl_watcher->adding_curl_multi_async = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
    //     -> void { this->custom_watcher_curl_async(w); });
    // this->curl_watcher->adding_curl_async_cb = [this] ()
    //     -> void { this->curl_watcher->adding_curl_multi_async->send(); };

    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETFUNCTION, event_loop::handle_curl_socket);
    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETDATA, this);
#endif

    this->idle_tasks_->start();

#if MANAPIHTTP_CURL_DEPENDENCY
    // this->curl_watcher.adding_curl_multi_async->start();

    /* curl fetch timeout */
    this->curl_watcher->timeout_watcher = create_watcher_timer([this] (std::shared_ptr<ev::timer> &w) -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();

        w->repeat(50);
        w->again();
    });

    this->curl_watcher->timeout_watcher->start(50, 0);
#endif

    /* interrupted */
    this->interrupted_watcher_ = create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void { async::run(this->stop()); });
}

manapi::event_loop::~event_loop() {

}

manapi::future<> manapi::event_loop::start(std::shared_ptr<event_loop> le) {
    auto lk = co_await this->mx->lock_guard();

    if (std::exchange(this->status,true)) {
        co_return;
    }

    this->pool_(std::move(lk), std::move(le));
}

void manapi::event_loop::sync_start(std::shared_ptr<event_loop> le) {
    if (this->mx->try_to_lock()) {
        auto lk = sbefore_delete([mx = this->mx]()->void{mx->unlock();});
        if (std::exchange(this->status,true)) {
            return;
        }

        this->pool_(std::move(lk), std::move(le));
    }
}

void manapi::event_loop::setup_handle_interrupt() {
#ifdef _WIN32
    signal (SIGSEGV, handler_interrupt);
    signal (SIGFPE, handler_interrupt);
    signal (SIGINT, handler_interrupt);
    signal (SIGBREAK, handler_interrupt);
    signal (SIGILL, handler_interrupt);
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
        auto promise = async::promise<void> ([this] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<> {
            this->resolve_stop = std::move(resolve);
            this->stop_watcher_->send();
            co_return;
        });

        co_await promise;
    }
}

void manapi::event_loop::wait() {
    this->stop_watcher (std::move(this->interrupted_watcher_));
    this->stop_watcher(std::move(this->callback_watcher_->adding_async));
    this->stop_watcher(std::move(this->idle_tasks_));
    this->stop_watcher(std::move(this->curl_watcher->timeout_watcher));

    std::shared_ptr<ev::idle> idle_tasks;

    while (uv_loop_alive(this->loop())) {
        auto etaskpool = dynamic_cast<ethreadpool<task> *>(this->etaskpool_.get());
        etaskpool->set_notify_cb([&] () -> void {
            idle_tasks = this->create_watcher_idle([&idle_tasks, etaskpool, this] (const ev::shared_idle &w) -> void {
                while (etaskpool->try_task()) {}
                etaskpool->set_notify();

                auto const loop_ = this->loop();
                this->stop_watcher(std::move(idle_tasks));

                if (!uv_loop_alive(loop_)) {
                    uv_stop(loop_);
                }
            });
        });

        manapi::async::current()->timerpool()->run_once();

        auto const rhs = uv_run(this->loop(), UV_RUN_DEFAULT);

        if (!rhs) {
            break;
        }
    }

    this->etaskpool_->stop();
    this->etaskpool_->join();

    uv_loop_close(this->loop_.get());
}

size_t manapi::event_loop::subscribe_finish(std::move_only_function<manapi::future<void>()> cb) {
    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->map_finish_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_INTERNAL, "index {} exists", id);
    }

    return id;
}

std::size_t manapi::event_loop::subscribe_clean_up(std::move_only_function<void()> cb) {
    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->map_clean_up_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_INTERNAL, "index {} exists", id);
    }

    return id;

}

void manapi::event_loop::unsubscribe_clean_up(std::size_t id) {
    if (!id) { return; }
    this->map_clean_up_cb.erase(id);

}

void manapi::event_loop::unsubscribe_finish(std::size_t id) {
    if (!id) { return; }
    this->map_finish_cb.erase(id);
}

manapi::ev::loop_ref manapi::event_loop::loop() {
    return this->loop_.get();
}

std::shared_ptr<manapi::ev::tcp> manapi::event_loop::create_watcher_tcp_accept( ev::tcp_accept_cb callback) {
    auto w = std::make_shared<ev::tcp>();

    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }

    auto ctx = new ev::internal::tcp_accept_ctx;
    ctx->type = 1;
    ctx->s_ = w;
    ctx->connection = std::move(callback);
    ctx->close_cb = nullptr;

    w->data(ctx);
    return std::move(w);
}

std::shared_ptr<manapi::ev::tcp> manapi::event_loop::create_watcher_tcp_connection( ev::tcp_connection_cb read, ev::tcp_alloc_cb alloc_cb) {
    auto w = std::make_shared<ev::tcp>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    auto ctx = std::make_unique<ev::internal::tcp_connection_ctx>();
    ctx->type = 0;
    ctx->s_ = w;
    ctx->read = std::move(read);
    ctx->alloc_cb = std::move(alloc_cb);
    ctx->close_cb = nullptr;

    w->data(ctx.release());
    return std::move(w);
}

std::pair<std::shared_ptr<manapi::ev::connect>, std::shared_ptr<manapi::ev::tcp>> manapi::event_loop::connect_tcp(const sockaddr *addr, ev::connect_tcp_cb on_connect, ev::tcp_connection_cb read, ev::tcp_alloc_cb alloc_cb) {
    auto w = this->create_watcher_tcp_connection(std::move(read), std::move(alloc_cb));
    auto c = std::make_shared<ev::connect>();

    if (auto rhs = c->bind(w->custom(), addr, ev::callback_watcher_connect_tcp)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }

    auto ctx = std::make_unique<ev::internal::connect_tcp_ctx>();
    ctx->cb = std::move(on_connect);
    ctx->tcp = w;
    w->data(ctx.release());
    return {std::move(c), std::move(w)};
}

std::shared_ptr<manapi::ev::udp> manapi::event_loop::create_watcher_udp(ev::udp_cb recv, ev::udp_alloc_cb alloc_cb) {
    auto w = std::make_shared<ev::udp>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::udp_ctx {.s_ = w, .recv = std::move(recv), .alloc_cb = std::move(alloc_cb)});
    return std::move(w);
}

manapi::future<> manapi::event_loop::_call_on_finish_cb() {
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

void manapi::event_loop::async_break_loop_() {
    this->free_on_finish_cb_();

    uv_stop(this->loop_.get());

    printf("1\n");

    if (this->resolve_stop) {
    printf("2\n");
        /* if resolve caballback exists, break the loop_ otherwise */
        this->stop_pool(std::exchange(this->resolve_stop, nullptr));
    }

    printf("3\n");

    printf("4\n");
}

void manapi::event_loop::try_tasks_(ev::shared_idle &w) {
    auto etaskpool = dynamic_cast<ethreadpool<task> *>(this->etaskpool_.get());
    while (!etaskpool->try_task()) {
        etaskpool->set_notify();
        w->stop();
        break;
    }
}

// void handle_io_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_io_data_t> data) {
//     switch (data->flag) {
//         case ADD_IO_EVENT_INIT_FD: {
//             /* init with fd */
//             auto s = ev->create_watcher_fd(data->payload.init.fd, std::move(data->payload.init.cb));
//             s->start(data->flags);
//
//             data->payload.s= (std::move(s));
//             break;
//         }
//         case ADD_IO_EVENT_INIT_SD: {
//             /* init with sd */
//             auto s = ev->create_watcher_socket(data->payload.init.sd, std::move(data->payload.init.cb));
//             s->start(data->flags);
//
//             data->payload.s= (std::move(s));
//             break;
//         }
//         case ADD_IO_EVENT_START: {
//             data->payload.s->start(data->flags);
//             break;
//         }
//         case ADD_IO_EVENT_STOP: {
//             data->payload.s->stop();
//             break;
//         }
//         case ADD_IO_EVENT_REMOVE: {
//             ev->stop_watcher(data->payload.s);
//             break;
//         }
//     }
//
//     data->resolve (std::move(data->payload.s));
// }
//
// void handle_async_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_async_data_t> data) {
//     switch (data->flag) {
//         case ADD_ASYNC_EVENT_INIT: {
//             auto s = ev->create_watcher_async(std::move(data->payload.cb));
//             data->payload.s= (std::move(s));
//             break;
//         }
//         case ADD_ASYNC_EVENT_REMOVE: {
//             ev->stop_watcher(data->payload.s);
//             break;
//         }
//     }
//
//     data->resolve (std::move(data->payload.s));
// }
//
// void handle_timer_watcher_data(manapi::event_loop *ev, std::unique_ptr<manapi::ev::internal::adding_watcher_timer_data_t> data) {
//     int rhs = 0;
//     switch (data->flag) {
//         case ADD_TIMER_EVENT_INIT: {
//             auto s = ev->create_watcher_timer(std::move(data->payload.cb));
//             rhs = s->start(data->delay, data->repeat);
//             data->payload.s=(std::move(s));
//             break;
//         }
//         case ADD_TIMER_EVENT_REMOVE: {
//             ev->stop_watcher(data->payload.s);
//             break;
//         }
//         case ADD_TIMER_EVENT_STOP: {
//             rhs = data->payload.s->stop();
//             break;
//         }
//         case ADD_TIMER_EVENT_START: {
//             rhs = data->payload.s->start(data->delay, data->repeat);
//             break;
//         }
//         case ADD_TIMER_EVENT_AGAIN: {
//             data->payload.s->repeat(data->repeat);
//             rhs = data->payload.s->again();
//             break;
//         }
//     }
//     if (rhs) {
//         data->resolve(nullptr);
//     }
//     else {
//         data->resolve(std::move(data->payload.s));
//     }
// }

// void manapi::event_loop::custom_watcher_poll_async(std::shared_ptr<ev::async> &w) {
//     if (this->io_watcher->adding_watcher_mx->try_to_lock()) {
//         auto list = std::move(this->io_watcher->adding_watcher_data);
//         this->io_watcher->adding_watcher_mx->unlock();
//
//         while (!list.empty()) {
//             std::unique_ptr<ev::internal::adding_watcher_io_data_t> data = std::move(list.front());
//             list.pop_front();
//
//             handle_io_watcher_data (this, std::move(data));
//         }
//
//     }
// }

// void manapi::event_loop::custom_watcher_async_async(std::shared_ptr<ev::async> &w) {
//     if (this->async_watcher->adding_watcher_mx->try_to_lock()) {
//         auto list = std::move(this->async_watcher->adding_watcher_data);
//         this->async_watcher->adding_watcher_mx->unlock();
//
//         while (!list.empty()) {
//             auto data = std::move(list.front());
//             list.pop_front();
//
//             handle_async_watcher_data(this, std::move(data));
//         }
//
//     }
// }
//
// void manapi::event_loop::custom_watcher_timer_async(std::shared_ptr<ev::async> &w) {
//     if (this->timer_watcher->adding_watcher_mx->try_to_lock()) {
//         auto list = std::move(this->timer_watcher->adding_watcher_data);
//         this->timer_watcher->adding_watcher_mx->unlock();
//
//         while (!list.empty()) {
//             auto data = std::move(list.front());
//             list.pop_front();
//
//             handle_timer_watcher_data(this, std::move(data));
//         }
//
//     }
// }

manapi::future<void> manapi::event_loop::custom_callback(std::move_only_function<void(event_loop *ev)> cb) {
    co_await async::promise<void> ([&](async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> manapi::future<> {
        auto this_ = this;
        auto data = std::make_unique<ev::internal::adding_custom_callback_data_t>(std::move(cb), std::move(resolve), std::move(reject));


        auto lk = co_await this->callback_watcher_->adding_mx->lock_guard();
        this->callback_watcher_->callback_data.push_back(std::move(data));
        lk.call();
        this_->callback_watcher_->adding_async_cb();
    });
}

#if MANAPIHTTP_CURL_DEPENDENCY
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

void manapi::event_loop::custom_watcher_callback_async(std::shared_ptr<ev::async> &w) {
    if (this->callback_watcher_->adding_mx->try_to_lock()) {
        auto list = std::move(this->callback_watcher_->callback_data);
        this->callback_watcher_->adding_mx->unlock();

        while (!list.empty()) {
            auto data = std::move(list.front());
            list.pop_front();

            try {
                data->cb(this);
            }
            catch (...) {
                data->reject(std::move(std::current_exception()));

                continue;
            }

            data->resolve();
        }
    }
}

#if MANAPIHTTP_CURL_DEPENDENCY
std::shared_ptr<manapi::ev::io> manapi::event_loop::handle_curl_watcher_gen (manapi::event_loop * data, manapi::socket_t fd) {
    return data->create_watcher_socket(fd, [data, fd] (std::shared_ptr<ev::io> &w, int status, int revents)
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
#endif

void manapi::event_loop::stop_watcher(std::shared_ptr<ev::tcp> s) {
    auto data = static_cast<ev::internal::tcp_ctx *>(s->data());
    if (data->type == 0) {
        s->unbind(+[] (uv_handle_t *s) -> void {
             auto data = static_cast<ev::internal::tcp_connection_ctx *>(s->data);
             if (data->close_cb) { data->close_cb->operator()(data->s_); }
             s->data = nullptr;
             if (data) { data->s_.reset(); delete data; }
         });
    }
    else {
        s->unbind(+[] (uv_handle_t *s) -> void {
            auto data = static_cast<ev::internal::tcp_accept_ctx *>(s->data);
            if (data->close_cb) { data->close_cb->operator()(data->s_); }
            s->data = nullptr;
            if (data) { data->s_.reset(); delete data; }
        });
    }
}

const std::shared_ptr<manapi::threadpool<manapi::task>> &manapi::event_loop::taskpool() const {
    return this->etaskpool_;
}

#if MANAPIHTTP_CURL_DEPENDENCY
void manapi::event_loop::watch_curl(std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb) {
    /* add */
    curl_easy_setopt (curl.get(), CURLOPT_OPENSOCKETFUNCTION, handle_curl_open_socket);
    curl_easy_setopt (curl.get(), CURLOPT_OPENSOCKETDATA, this);
    //
    curl_easy_setopt (curl.get(), CURLOPT_CLOSESOCKETFUNCTION, handle_curl_close_socket);
    curl_easy_setopt (curl.get(), CURLOPT_CLOSESOCKETDATA, this);

    CURLMcode const mcode = curl_multi_add_handle(this->curl_watcher->curl_multi.get(), curl.get());

    if (mcode != CURLM_OK) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL,
            "Failed to add the curl handle. curl_multi_add_handle(...) = {}", static_cast<int>(mcode));

    }
    auto curlptr = curl.get();
    this->curl_watcher->curl_res.insert({curlptr, {std::move(curl), std::move(cb), nullptr}});

    this->etaskpool_->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
}

void manapi::event_loop::unwatch_curl(std::shared_ptr<CURL> curl) {
    /* remove */
    auto curl_data = this->curl_watcher->curl_res.extract(curl.get());
    if (curl_data.empty()) {
        /** already was removed */
        return;
    }

    CURLMcode const mcode = curl_multi_remove_handle(this->curl_watcher->curl_multi.get(), curl.get());

    if (mcode != CURLM_OK) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL,
            "Failed to remove the curl handle. curl_multi_remove_handle(...) = {}", static_cast<int>(mcode));
    }

    if (curl_data.mapped().watcher) {
        this->stop_watcher(curl_data.mapped().watcher);
    }
    curl_data.mapped().finish(CURLE_ABORTED_BY_CALLBACK);

    this->etaskpool_->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
}

void manapi::event_loop::pause_watch_curl(std::shared_ptr<CURL> curl) {
    /* pause */
    const auto rhs = curl_easy_pause(curl.get(), CURLPAUSE_ALL);
    if (CURLE_OK != rhs) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "curl_easy_pause(...) with CURLPAUSE_ALL failed -> {}", static_cast<int>(rhs));
    }
    this->etaskpool_->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
}

void manapi::event_loop::unpause_watch_curl(std::shared_ptr<CURL> curl) {
    /* unpause */
    const auto rhs = curl_easy_pause(curl.get(), CURLPAUSE_CONT);
    if (CURLE_OK != rhs) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL,
            "curl_easy_pause(...) with CURLPAUSE_CONT failed -> {}", static_cast<int>(rhs));
    }
    this->etaskpool_->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
}
#endif


void manapi::event_loop::interrupt() {
    std::unique_lock <std::mutex> lk (event_loop::stop_mx, std::try_to_lock);
    event_loop::interrupted.store(true);

    for (auto &loop_ :  manapi::event_loop::events) {
        loop_.second->interrupted_watcher_->send();
    }

    manapi::event_loop::events.clear();
}

std::shared_ptr<manapi::ev::io> manapi::event_loop::create_watcher_fd(int fd, ev::io_cb callback) {
    auto w = std::make_shared<ev::io>();
    if (const auto rhs = w->bind(this->loop_.get(), fd))
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    
    w->data(new ev::internal::io_ctx  {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::idle> manapi::event_loop::create_watcher_idle(ev::idle_cb callback) {
    auto w = std::make_shared<ev::idle>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::idle_ctx  {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::io> manapi::event_loop::create_watcher_socket(socket_t sock, ev::io_cb callback) {
    auto w = std::make_shared<ev::io>();
    if (auto const rhs = w->bind(this->loop_.get(), sock))
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    w->data(new ev::internal::io_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::async> manapi::event_loop::create_watcher_async(ev::async_cb callback) {
    auto w = std::make_shared<ev::async>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::async_ctx { .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::timer> manapi::event_loop::create_watcher_timer(ev::timer_cb callback) {
    auto w = std::make_shared<ev::timer>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::timer_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::prepare> manapi::event_loop::create_watcher_prepare(ev::prepare_cb callback) {
    auto w = std::make_shared<ev::prepare>();
    if (auto rhs = w->bind(this->loop_.get())) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::prepare_ctx {.s_ = w, .cb = std::move(callback)});
    return std::move(w);
}

std::shared_ptr<manapi::ev::fs> manapi::event_loop::create_watcher_fs(ev::fs_cb callback) {
    auto w = std::make_shared<ev::fs>(this->loop_.get());
    w->data(new ev::internal::fs_ctx{ .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::random> manapi::event_loop::create_watcher_random(ev::random_cb callback, char *buff, std::size_t size) {
    auto w = std::make_shared<ev::random>();
    if (auto rhs = w->bind(this->loop_.get(), buff, size)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::random_ctx{ .s_ = w, .cb = std::move(callback) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::write> manapi::event_loop::create_watcher_write(ev::tcp *conn, ev::write_cb callback, const ev::buff_t *bufs, uint32_t nbuf) {
    auto w = std::make_shared<ev::write>();
    if (auto rhs = w->bind(reinterpret_cast <ev::stream_t *>(conn->custom()), bufs, nbuf)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    w->data(new ev::internal::write_ctx{.s_ = w, .write = std::move(callback)});
    return std::move(w);
}

void manapi::event_loop::stop_callback(const std::shared_ptr<ev::tcp> &s, ev::close_cb_t<ev::tcp> cb) {
    auto const data = static_cast<ev::internal::tcp_ctx*> (s->data());
    if (data) {
        data->close_cb = std::make_unique<decltype(cb)>(std::move(cb));
    }
}

void manapi::event_loop::stop_callback(const std::shared_ptr<ev::udp> &s, ev::close_cb_t<ev::udp> cb) {
    auto const data = static_cast<ev::internal::udp_ctx*> (s->data());
    if (data) {
        data->close_cb = std::make_unique<decltype(cb)>(std::move(cb));
    }
}

void manapi::event_loop::read_callback(const std::shared_ptr<ev::tcp> &s, ev::tcp_connection_cb cb) {
    auto const data = static_cast<ev::internal::tcp_connection_ctx *> (s->data());
    if (data) {
        data->read = std::move(cb);
    }
}

void manapi::event_loop::alloc_callback(const std::shared_ptr<ev::tcp> &s, ev::tcp_alloc_cb cb) {
    auto const m = static_cast<ev::internal::tcp_ctx *> (s->data());
    if (m && m->type == 0) {
        auto const data = static_cast<ev::internal::tcp_connection_ctx *> (m);
        data->alloc_cb = std::move(cb);
    }
}

MANAPI_EV_UNWATCHER(idle, idle_ctx);
MANAPI_EV_UNWATCHER(io, io_ctx);
MANAPI_EV_UNWATCHER2(udp, udp_ctx);
MANAPI_EV_UNWATCHER(async, async_ctx);
MANAPI_EV_UNWATCHER(check, check_ctx);
MANAPI_EV_UNWATCHER(timer, timer_ctx);
MANAPI_EV_UNWATCHER(prepare, prepare_ctx);

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::fs *w) {
    if (w->data()) {
        w->cancel();

        auto data = static_cast<ev::internal::fs_ctx *>(w->data());
        w->data(nullptr);

        if (data) {
            data->s_.reset();
            delete data;
        }
    }
}

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::random *w) {
    if (w->data()) {
        w->cancel();

        auto data = static_cast<ev::internal::random_ctx *>(w->data());
        w->data(nullptr);

        if (data) {
            data->s_.reset();
            delete data;
        }
    }
}

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::write *w) {
    if (w->data()) {
        auto data = static_cast<ev::internal::write_ctx *>(w->data());
        w->data(nullptr);
        if (data) {
            data->s_.reset();
            delete data;
        }
    }
}

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::udp_send *w) {
    if (w->data()) {
        auto data = static_cast<ev::internal::udp_send_ctx *>(w->data());
        w->data( nullptr);
        if (data) {
            data->s_.reset();
            delete data;
        }
    }
}

void manapi::event_loop::pool_(manapi::sbefore_delete lk2, std::shared_ptr<event_loop> le) {
    {
        std::lock_guard<std::mutex> lk (event_loop::stop_mx);

        if (event_loop::interrupted) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_ABORTED, "Failed to create a events loop_");
        }

        event_loop::events.insert({reinterpret_cast<size_t> (this), std::move(le)});
    }

    this->stop_watcher_ = this->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
        -> void {
        this->async_break_loop_();
        this->stop_watcher(std::move(this->stop_watcher_));
    });

    auto init_watcher = this->create_watcher_async([&lk2, this] (std::shared_ptr<ev::async> &w)
        -> void {  lk2.call(); this->stop_watcher(std::move(w)); });

    init_watcher->send();

    this->etaskpool_->start();
    uv_run(this->loop_.get(), UV_RUN_DEFAULT);
    this->etaskpool_->stop();

    /* if init_watcher(...) was not called */
    lk2.call();
}