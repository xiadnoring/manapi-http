#include <csignal>

#include "services/ManapiEventLoop.hpp"

#if MANAPIHTTP_CPPTRACE_DEPENDENCY
#   include <cpptrace/cpptrace.hpp>
#endif

#include <memory>
#include <cstring>
#include <stacktrace>

#include "../include/ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "components/ManapiTimerObject.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "async/ManapiAsyncThreadsMutex.hpp"
#include "../include/ManapiEventStructuresInternal.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

#define MANAPI_EV_TRY_CALLBACK try {
#define MANAPI_EV_CATCH_CALLBACK(s__) } catch (std::exception const &e) { manapi_log_error("ev:Callback '%s' failed due to %s", s__, e.what()); }

#define MANAPI_EV_UNWATCHER(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) { \
    if (w && w->data()) { \
        w->unbind(+[](uv_handle_t *handle) -> void { std::unique_ptr<ev::internal::ctxname>  data (static_cast<ev::internal::ctxname *>(handle->data)); \
        handle->data = nullptr; if (data) { data->s_.reset(); } }); \
    } }

#define MANAPI_EV_CANCEL(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) { \
    if (w && w->data()) { w->cancel(); std::unique_ptr<ev::internal::ctxname> data (static_cast<ev::internal::ctxname *>(w->data())); w->data(nullptr); \
        if (data) { data->token.disable(); data->s_.reset(); } \
    } }

#define MANAPI_EV_UNWATCHER2(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) { \
    if (w && w->data()) { \
        w->unbind(+[](uv_handle_t *handle) -> void { std::unique_ptr<ev::internal::ctxname> data (static_cast<ev::internal::ctxname *>(handle->data)); \
        handle->data = nullptr; if (data->close_cb) { MANAPI_EV_TRY_CALLBACK data->close_cb->operator()(data->s_); MANAPI_EV_CATCH_CALLBACK("close") } if (data) { data->s_.reset(); } }); \
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
        manapi::async::cancellation_action token;
    };

    struct random_ctx {
        std::shared_ptr<ev::random> s_;
        random_cb cb;
        manapi::async::cancellation_action token;
    };

    struct getaddrinfo_ctx {
        std::shared_ptr<ev::getaddrinfo> s_;
        getaddrinfo_cb cb;
        manapi::async::cancellation_action token;
    };

    struct getnameinfo_ctx {
        std::shared_ptr<ev::getnameinfo> s_;
        getnameinfo_cb cb;
        manapi::async::cancellation_action token;
    };

    struct work_ctx {
        std::shared_ptr<ev::work> s_;
        work_cb cb;
        after_work_cb after_cb;
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
        std::mutex adding_mx;
        std::shared_ptr <ev::async> adding_async;
        std::move_only_function<void()> adding_async_cb{nullptr};
    };
}

struct fs_req_deleter {
    void operator()(uv_fs_t *req) {
        uv_fs_req_cleanup(req);
    }
};

struct addrinfo_deleter {
    void operator()(addrinfo *ai) {
        uv_freeaddrinfo(ai);
    }
};

std::map<size_t, std::shared_ptr<manapi::event_loop>> manapi::event_loop::events = {};
std::atomic<bool> manapi::event_loop::interrupted = false;
std::mutex manapi::event_loop::stop_mx;

void handler_interrupt (int sig) {
    manapi::event_loop::interrupt(sig);
}

void evloop_stack_trace () {
    try {
#if MANAPIHTTP_CPPTRACE_DEPENDENCY
        cpptrace::generate_trace().print();
#else
        auto stack = std::stacktrace::current();
        for (std::size_t i = 0; i < stack.size(); i++) {
            auto &it = stack[i];
            manapi_log_info("#%zu %p in %.*s at %.*s:%zu", i, it.native_handle(),
                it.description().size(), it.description().data(),
                it.source_file().size(), it.source_file().data(),
                it.source_line());
        }
        //manapi_log_error("stack trace is disabled");
#endif
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stack trace print failed", e.what());
    }
}

void manapi::ev::callback_watcher_async (uv_async_t *s) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::async_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::async_ctx *> (s->data)->s_);
    MANAPI_EV_CATCH_CALLBACK("async")
}

void manapi::ev::callback_watcher_timer (uv_timer_t *s) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::timer_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (static_cast<manapi::ev::internal::timer_ctx *> (s->data)->s_);
    MANAPI_EV_CATCH_CALLBACK("timer")
}

void manapi::ev::callback_watcher_io (uv_poll_t *s, int status, int revents) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::io_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::io_ctx *> (s->data)->s_, status, revents);
    MANAPI_EV_CATCH_CALLBACK("io")
}

void manapi::ev::callback_watcher_idle (uv_idle_t *s) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::idle_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::idle_ctx *> (s->data)->s_);
    MANAPI_EV_CATCH_CALLBACK("idle")
}

void manapi::ev::callback_watcher_check (uv_check_t *s) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::check_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    (static_cast<manapi::ev::internal::check_ctx *> (s->data)->s_);
    MANAPI_EV_CATCH_CALLBACK("check")
}

void manapi::ev::callback_watcher_prepare (uv_prepare_t *s) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::prepare_ctx *> (s->data)
        ->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (static_cast<manapi::ev::internal::prepare_ctx *> (s->data)->s_);
    MANAPI_EV_CATCH_CALLBACK("prepare")
}

void manapi::ev::callback_watcher_tcp_accept (uv_tcp_t *s, int status) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)
        ->connection;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)->s_, status);
    MANAPI_EV_CATCH_CALLBACK("tcp accept")
}

void manapi::ev::callback_watcher_tcp_read (uv_stream_t *s, ssize_t nread, const uv_buf_t *buf) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)
        ->read;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)->s_, nread, buf);
    MANAPI_EV_CATCH_CALLBACK("tcp read")
}

void manapi::ev::callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags) {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::udp_ctx *> (s->data)
        ->recv;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::udp_ctx *> (s->data)->s_, nread, buf, addr, flags);
    MANAPI_EV_CATCH_CALLBACK("udp recv")
}

void manapi::ev::callback_watcher_udp_send (uv_udp_send_t *s, int status) {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::udp_send_ctx> ss (static_cast<manapi::ev::internal::udp_send_ctx *> (s->data));
    s->data = nullptr;
    auto &cb =ss->send;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (ss->s_, status);
    MANAPI_EV_CATCH_CALLBACK("udp send")
}

void manapi::ev::callback_watcher_write (uv_write_t *s, int status) {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::write_ctx> ss (static_cast<manapi::ev::internal::write_ctx *> (s->data));
    s->data = nullptr;
    auto &cb = ss->write;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (ss->s_, status);
    MANAPI_EV_CATCH_CALLBACK("write")
}

void manapi::ev::callback_watcher_connect_tcp(uv_connect_t *s, int status) {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::connect_tcp_ctx> ss (static_cast<manapi::ev::internal::connect_tcp_ctx *> (s->data));
    s->data = nullptr;
    auto &cb = ss->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPI_EV_TRY_CALLBACK
    cb (ss->tcp, status);
    MANAPI_EV_CATCH_CALLBACK("connect tcp")
}


void manapi::ev::callback_watcher_fs(uv_fs_t *req) {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::fs_ctx> ss (static_cast<manapi::ev::internal::fs_ctx *> (req->data));
        std::unique_ptr<uv_fs_t, fs_req_deleter> req_own (req);
        req->data = nullptr;

        MANAPI_EV_TRY_CALLBACK
        if (ss->cb)
            ss->cb(ss->s_);
        MANAPI_EV_CATCH_CALLBACK("fs")
    }
    else {
        std::unique_ptr<uv_fs_t, fs_req_deleter> req_own (req);
    }
}

void manapi::ev::callback_watcher_random(uv_random_t *s, int status, void *buff, std::size_t size) {
    assert(s->data && "ev:User data wasn't set");
    if (s->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::random_ctx> ss (static_cast<manapi::ev::internal::random_ctx *> (s->data));
        s->data = nullptr;
        MANAPI_EV_TRY_CALLBACK
        if (ss->cb)
            ss->cb(ss->s_, status, buff, size);
        MANAPI_EV_CATCH_CALLBACK("random")
    }
}

void manapi::ev::callback_watcher_getnameinfo(uv_getnameinfo_t *req, int status, const char *hostname, const char *service) {
    assert(req->data && "ev:User data wasn't set");
    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::getnameinfo_ctx> s (static_cast<manapi::ev::internal::getnameinfo_ctx *> (req->data));
        req->data = nullptr;
        MANAPI_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_, status, hostname, service);
        MANAPI_EV_CATCH_CALLBACK("getnameinfo")
    }
}

void manapi::ev::callback_watcher_getaddrinfo(uv_getaddrinfo_t *req, int status, addrinfo *res) {
    assert(req->data && "ev:User data wasn't set");
    std::unique_ptr<addrinfo, addrinfo_deleter> res_own (res);

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::getaddrinfo_ctx> s (static_cast<manapi::ev::internal::getaddrinfo_ctx *> (req->data));
        req->data = nullptr;
        MANAPI_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_, status, res_own.release());
        MANAPI_EV_CATCH_CALLBACK("getaddrinfo")
    }
}

void manapi::ev::callback_watcher_after_work(uv_work_t *req, int status) {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::work_ctx> s (static_cast<manapi::ev::internal::work_ctx *> (req->data));
        req->data = nullptr;
        MANAPI_EV_TRY_CALLBACK
        if (s->after_cb)
            s->after_cb(s->s_, status);
        MANAPI_EV_CATCH_CALLBACK("after work")
    }
}

void manapi::ev::callback_watcher_work(uv_work_t *req) {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        auto s = static_cast<manapi::ev::internal::work_ctx *> (req->data);
        /* otherwise it was cancelled */
        MANAPI_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_);
        MANAPI_EV_CATCH_CALLBACK("work")
    }
}


void manapi::ev::callback_watcher_tcp_connection_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    assert(handle->data && "ev:User data wasn't set");
    auto s = static_cast<manapi::ev::internal::tcp_connection_ctx *> (handle->data);
    assert((s->alloc_cb && "ev:User cb wasn't set"));
    MANAPI_EV_TRY_CALLBACK
    s->alloc_cb(s->s_, suggested_size, buf);
    MANAPI_EV_CATCH_CALLBACK("tcp conn alloc")
}

void manapi::ev::callback_watcher_udp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    assert(handle->data && "ev:User data wasn't set");
    auto s = static_cast<manapi::ev::internal::udp_ctx *> (handle->data);
    assert((s->alloc_cb && "ev:User cb wasn't set"));
    MANAPI_EV_TRY_CALLBACK
    s->alloc_cb(s->s_, suggested_size, buf);
    MANAPI_EV_CATCH_CALLBACK("tcp udp alloc")
}

void manapi::ev::callback_close_cb(uv_handle_t *s) {
    assert(s->data && "ev:User data wasn't set");

    switch (s->type) {
        case ev::EV_TCP: {
            auto ss = static_cast<manapi::ev::internal::tcp_ctx *> (s->data);
            MANAPI_EV_TRY_CALLBACK
            if (ss->close_cb)
                ss->close_cb->operator()(ss->s_);
            MANAPI_EV_CATCH_CALLBACK("close")
            break;
        }
        default: {

            break;
        }
    }
}


ssize_t double_store_in_ssize (double a) { ssize_t b = 0; memcpy (&b, &a, sizeof (a)); return a; }

double ssize_store_in_double (ssize_t a) { double b = 0; memcpy (&b, &a, sizeof (a)); return b; };


manapi::event_loop::event_loop(std::shared_ptr<threadpool> taskpool_, std::shared_ptr<manapi::logger> logger) {
    this->loop_ = std::make_unique<uv_loop_t>();
    assert(!(uv_loop_init(this->loop_.get())));

#ifdef MANAPIHTTP_CURL_DEPENDENCY
    this->curl_watcher = std::make_unique<ev::internal::curl_watcher_t>();
#endif
    // this->timerloop = std::make_unique<ev::internal::timerloop_t>();
    this->callback_watcher_ = std::make_unique<ev::internal::custom_callback_t>();

    this->idle_tasks_ = this->create_watcher_idle([this] (const ev::shared_idle &w)
        -> void {
        this->try_tasks_(w);
    });

    this->etaskpool_ = std::make_shared<manapi::ethreadpool>(logger, [this] ()
        -> void {
        this->idle_tasks_->start();
    });

    dynamic_cast<ethreadpool *>(this->etaskpool_.get())->set_notify();

    this->mx = std::make_shared<async::mutex>();
    this->logger_ = std::move(logger);

    this->taskpool_ = std::move(taskpool_);
    this->status = false;

    this->callback_watcher_->adding_async = this->create_watcher_async([this] (const std::shared_ptr<ev::async> &w)
        -> void { this->custom_watcher_callback_async(w); });

    this->callback_watcher_->adding_async_cb = [this] ()
        -> void { this->callback_watcher_->adding_async->send(); };

#if MANAPIHTTP_CURL_DEPENDENCY
    this->curl_watcher->curl_multi.reset(curl_multi_init());

    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETFUNCTION, event_loop::handle_curl_socket);
    curl_multi_setopt(this->curl_watcher->curl_multi.get(), CURLMOPT_SOCKETDATA, this);
#endif

    this->idle_tasks_->start();

#if MANAPIHTTP_CURL_DEPENDENCY

    /* curl fetch timeout */
    this->curl_watcher->timeout_watcher = create_watcher_timer([this] (const std::shared_ptr<ev::timer> &w) -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();

        w->repeat(50);
        w->again();
    });

    this->curl_watcher->timeout_watcher->start(50, 0);
#endif

    /* interrupted */
    this->interrupted_watcher_ = create_watcher_async([this] (const std::shared_ptr<ev::async> &w)
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
    signal (SIGBREAK, handler_interrupt);
#else
    signal (SIGPIPE, SIG_IGN);
    signal (SIGKILL, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
#endif
    signal (SIGINT, handler_interrupt);
    signal (SIGSEGV, handler_interrupt);
    signal (SIGFPE, handler_interrupt);
    signal (SIGILL, handler_interrupt);
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
        auto etaskpool = dynamic_cast<ethreadpool *>(this->etaskpool_.get());
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
    c->data(ctx.release());

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

    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "eventloop:uv_stop() has been finished");

    if (this->resolve_stop) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "eventloop:stop_pool() has been started");
        /* if resolve caballback exists; otherwise, break the loop */
        this->stop_pool(std::exchange(this->resolve_stop, nullptr));
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "eventloop:stop_pool() has been finished");
    }
}

void manapi::event_loop::try_tasks_(const ev::shared_idle &w) {
    auto etaskpool = dynamic_cast<ethreadpool *>(this->etaskpool_.get());
    while (!etaskpool->try_task()) {
        etaskpool->set_notify();
        w->stop();
        break;
    }
}

void manapi::event_loop::custom_callback(std::move_only_function<void(event_loop *ev)> cb) {
    auto data = std::make_unique<ev::internal::adding_custom_callback_data_t>(std::move(cb));
    std::unique_lock<std::mutex> lk (this->callback_watcher_->adding_mx);
    this->callback_watcher_->callback_data.push_back(std::move(data));
    lk.unlock();
    this->callback_watcher_->adding_async_cb();
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

void manapi::event_loop::custom_watcher_callback_async(const std::shared_ptr<ev::async> &w) {
    if (this->callback_watcher_->adding_mx.try_lock()) {
        auto list = std::move(this->callback_watcher_->callback_data);
        this->callback_watcher_->adding_mx.unlock();

        while (!list.empty()) {
            auto data = std::move(list.front());
            list.pop_front();

            try {
                data->cb(this);
            }
            catch (std::exception const &e) {
                manapi_log_error("custom cb:Failed callback due to %s", e.what());
            }
        }
    }
}

#if MANAPIHTTP_CURL_DEPENDENCY
std::shared_ptr<manapi::ev::io> manapi::event_loop::handle_curl_watcher_gen (manapi::event_loop * data, manapi::socket_t fd) {
    return data->create_watcher_socket(fd, [data, fd] (const std::shared_ptr<ev::io> &w, int status, int revents)
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

const std::shared_ptr<manapi::threadpool> &manapi::event_loop::taskpool() const {
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
    auto &mapped = curl_data.mapped();
    if (mapped.watcher) {
        this->stop_watcher(curl_data.mapped().watcher);
    }
    mapped.finish(CURLE_ABORTED_BY_CALLBACK);

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

void manapi::event_loop::interrupt(int sig) {
    std::unique_lock <std::mutex> lk (event_loop::stop_mx, std::try_to_lock);
    event_loop::interrupted.store(true);

    switch (sig) {
        case SIGABRT:
        case SIGTERM:
        case SIGINT:
            break;
        default:
            evloop_stack_trace ();
            exit(-1);
    }

    // if (sig == SIGFPE)
    //     exit(-1);

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

std::shared_ptr<manapi::ev::fs> manapi::event_loop::create_watcher_fs(ev::fs_cb callback, manapi::async::cancellation_action token) {
    auto w = std::make_shared<ev::fs>(this->loop_.get());
    if (token) {
        token.cancel_callback([w = std::weak_ptr (w)] () -> void {
            manapi::async::current()->eventloop()->stop_watcher(w.lock());
        });
    }
    w->data(new ev::internal::fs_ctx{ .s_ = w, .cb = std::move(callback), .token = std::move(token) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::getaddrinfo> manapi::event_loop::create_watcher_getaddrinfo(const char *node, const char *service, const addrinfo *hints, ev::getaddrinfo_cb callback, manapi::async::cancellation_action token) {
    auto w = std::make_shared<ev::getaddrinfo>();
    if (auto rhs = w->bind(this->loop_.get(), node, service, hints)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    if (token) {
        token.cancel_callback([w = std::weak_ptr (w)] () -> void {
            manapi::async::current()->eventloop()->stop_watcher(w.lock());
        });
    }
    w->data(new ev::internal::getaddrinfo_ctx{ .s_ = w, .cb = std::move(callback), .token = std::move(token) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::getnameinfo> manapi::event_loop::create_watcher_getnameinfo(const sockaddr *addr, int flags, ev::getnameinfo_cb callback, manapi::async::cancellation_action token) {
    auto w = std::make_shared<ev::getnameinfo>();
    if (auto rhs = w->bind(this->loop_.get(), addr, flags)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    if (token) {
        token.cancel_callback([w = std::weak_ptr (w)] () -> void {
            manapi::async::current()->eventloop()->stop_watcher(w.lock());
        });
    }
    w->data(new ev::internal::getnameinfo_ctx{ .s_ = w, .cb = std::move(callback), .token = std::move(token) });
    return std::move(w);
}

std::shared_ptr<manapi::ev::random> manapi::event_loop::create_watcher_random(char *buff, std::size_t size, ev::random_cb callback, manapi::async::cancellation_action token) {
    auto w = std::make_shared<ev::random>();
    if (auto rhs = w->bind(this->loop_.get(), buff, size)) {
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    }
    if (token) {
        token.cancel_callback([w = std::weak_ptr (w)] () -> void {
            manapi::async::current()->eventloop()->stop_watcher(w.lock());
        });
    }
    w->data(new ev::internal::random_ctx{ .s_ = w, .cb = std::move(callback), .token = std::move(token) });
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

manapi::error::status_or<std::shared_ptr<manapi::ev::udp_send>> manapi::event_loop::create_watcher_udp_send(ev::udp *conn, ev::udp_send_cb callback, const ev::buff_t *bufs, uint32_t nbuf, sockaddr *addr) {
    try {
        auto ctx = std::make_unique<ev::internal::udp_send_ctx>(nullptr, std::move(callback));
        auto w = std::make_shared<ev::udp_send>();

        if (auto rhs = w->bind(conn->custom(), bufs, nbuf, addr))
            return error::status_internal("eventloop:Bind failed");

        ctx->s_ = w;
        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "eventloop:Create watcher failed", e.what());
    }
    return manapi::error::status_internal("eventloop:Create watcher failed");
}

manapi::ev::shared_work manapi::event_loop::append_task(std::move_only_function<void(const ev::shared_work &w)> work, std::move_only_function<void(const ev::shared_work &w, int status)> after_work) {
    auto w = std::make_shared<ev::work>();
    if (auto rhs = w->bind(this->loop()))
        throw manapi::exception (manapi::ERR_INTERNAL, manapi::error::default_msgs[error::ERRMSG_WATCHER_BIND_FAILED]);
    w->data(new ev::internal::work_ctx{.s_ = w, .cb = std::move(work), .after_cb = std::move(after_work)});
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

MANAPI_EV_CANCEL (fs, fs_ctx);
MANAPI_EV_CANCEL (getaddrinfo, getaddrinfo_ctx);
MANAPI_EV_CANCEL (getnameinfo, getnameinfo_ctx);
MANAPI_EV_CANCEL (random, random_ctx);

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::write *w) {
    if (w && w->data()) {
        auto data = static_cast<ev::internal::write_ctx *>(w->data());
        w->data(nullptr);
        if (data) {
            data->s_.reset();
            delete data;
        }
    }
}

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::udp_send *w) {
    if (w && w->data()) {
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

    this->stop_watcher_ = this->create_watcher_async([this] (const std::shared_ptr<ev::async> &w)
        -> void {
        this->async_break_loop_();
        this->stop_watcher(std::move(this->stop_watcher_));
    });

    auto init_watcher = this->create_watcher_async([&lk2, this] (const std::shared_ptr<ev::async> &w)
        -> void {  lk2.call(); this->stop_watcher(w); });

    init_watcher->send();

    this->etaskpool_->start();
    uv_run(this->loop_.get(), UV_RUN_DEFAULT);
    this->etaskpool_->stop();

    /* if init_watcher(...) was not called */
    lk2.call();
}