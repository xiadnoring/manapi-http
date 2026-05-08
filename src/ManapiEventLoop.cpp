#include <csignal>
#include <memory>
#include <cstring>
#include <algorithm>

#include "ManapiEventLoop.hpp"
#include "ManapiUtils.hpp"
#include "ManapiTimerObject.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiTimerPool.hpp"

#include "std/ManapiAsyncSocket.hpp"
#include "std/ManapiAsyncThreadsMutex.hpp"
#include "./include/ManapiUtils.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"
#include "./include/ManapiDefaultErrors.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY
#   include <curl/curl.h>
static_assert(manapi::ev::READ == CURL_POLL_IN && manapi::ev::WRITE == CURL_POLL_OUT, "need for review");
#endif

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

#define MANAPIHTTP_EV_TRY_CALLBACK try {
#define MANAPIHTTP_EV_CATCH_CALLBACK(s__) } catch (std::exception const &e) { manapi_log_error("ev:Callback '%s' failed due to %s", s__, e.what()); }

#define MANAPIHTTP_EV_UNWATCHER(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) MANAPIHTTP_NOEXCEPT { \
    if (w) { \
        w->unbind(+[](uv_handle_t *handle) -> void { std::unique_ptr<ev::internal::ctxname>  data (static_cast<ev::internal::ctxname *>(handle->data)); \
        handle->data = nullptr; if (data) { data->s_.reset(); } }); \
    } }

#define MANAPIHTTP_EV_CANCEL(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) MANAPIHTTP_NOEXCEPT { \
    if (w) { w->cancel(); std::unique_ptr<ev::internal::ctxname> data (static_cast<ev::internal::ctxname *>(w->data())); w->data(nullptr); \
        if (data) { data->token.disable(); data->s_.reset(); } \
    } }

#define MANAPIHTTP_EV_UNWATCHER2(classname, ctxname) void manapi::event_loop::event_loop::stop_watcher_ptr(ev::classname *w) MANAPIHTTP_NOEXCEPT { \
    if (w) { \
        w->unbind(+[](uv_handle_t *handle) -> void { std::unique_ptr<ev::internal::ctxname> data (static_cast<ev::internal::ctxname *>(handle->data)); \
        handle->data = nullptr; if (data) { if (data->close_cb) { MANAPIHTTP_EV_TRY_CALLBACK data->close_cb->operator()(data->s_); MANAPIHTTP_EV_CATCH_CALLBACK("close") } data->s_.reset(); } }); \
    } }

enum event_loop_flags {
    EVENT_LOOP_FLAG_ACTIVE = 1,
    EVENT_LOOP_FLAG_STOPPING = 2
};

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
        void operator()(CURLM *curl_multi) MANAPIHTTP_NOEXCEPT {
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

    struct connect_base_ctx {
        char type;
    };

    struct connect_tcp_ctx : connect_base_ctx {
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
        manapi::ctoken token;
    };

    struct random_ctx {
        std::shared_ptr<ev::random> s_;
        random_cb cb;
        manapi::ctoken token;
    };

    struct getaddrinfo_ctx {
        std::shared_ptr<ev::getaddrinfo> s_;
        getaddrinfo_cb cb;
        manapi::ctoken token;
    };

    struct getnameinfo_ctx {
        std::shared_ptr<ev::getnameinfo> s_;
        getnameinfo_cb cb;
        manapi::ctoken token;
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
        int m_flags;
        adding_watcher_io_data_payload_t payload;
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::io>>::resolve_t resolve;
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
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::fs>>::resolve_t resolve;
        ev::file file2{0};
    };

    struct adding_watcher_async_data_payload_t {
        manapi::ev::async_cb cb = nullptr;
        std::shared_ptr<manapi::ev::async> s = nullptr;
    };

    struct adding_watcher_async_data_t {
        int flag;
        adding_watcher_async_data_payload_t payload;
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::async>>::resolve_t resolve{nullptr};
    };

    struct adding_watcher_prepare_data_payload_t {
        manapi::ev::prepare_cb cb = nullptr;
        std::shared_ptr<manapi::ev::prepare> s = nullptr;
    };

    struct adding_watcher_prepare_data_t {
        int flag;
        adding_watcher_prepare_data_payload_t payload;
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::async>>::resolve_t resolve{nullptr};
    };

    struct adding_watcher_check_data_payload_t {
        manapi::ev::check_cb cb = nullptr;
        std::shared_ptr<manapi::ev::check> s = nullptr;
    };

    struct adding_watcher_check_data_t {
        int flag;
        adding_watcher_check_data_payload_t payload;
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::check>>::resolve_t resolve{nullptr};
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
        manapi::async::promise_sync<std::shared_ptr<manapi::ev::timer>>::resolve_t resolve{nullptr};
    };

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
        std::unordered_map<CURL*, curl_res_value_t> curl_res{};
        manapi::timer timeout_watcher{nullptr};
        std::unordered_map<socket_t, std::shared_ptr<ev::io>> watchers;
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
        manapi::chain<adding_custom_callback_data_t> callback_data{};
        std::mutex adding_mx;
        std::mutex adding_mcv;
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

std::unordered_map <size_t, std::shared_ptr<manapi::event_loop>> manapi::event_loop::m_events = {};
std::atomic<bool> manapi::event_loop::m_interrupted = false;
std::mutex manapi::event_loop::m_stop_mx;

static void handler_interrupt (int sig) {
    manapi::event_loop::interrupt(sig);
}

void manapi::ev::callback_watcher_async (uv_async_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::async_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        cb(static_cast<manapi::ev::internal::async_ctx *> (s->data)->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("async")
    }
}

void manapi::ev::callback_watcher_timer (uv_timer_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::timer_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        cb (static_cast<manapi::ev::internal::timer_ctx *> (s->data)->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("timer")
    }
}

void manapi::ev::callback_watcher_io (uv_poll_t *s, int status, int revents) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::io_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        cb(static_cast<manapi::ev::internal::io_ctx *> (s->data)->s_, status, revents);
        MANAPIHTTP_EV_CATCH_CALLBACK("io")
    }
}

void manapi::ev::callback_watcher_idle (uv_idle_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::idle_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        cb(static_cast<manapi::ev::internal::idle_ctx *> (s->data)->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("idle")
    }
}

void manapi::ev::callback_watcher_check (uv_check_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::check_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        (static_cast<manapi::ev::internal::check_ctx *> (s->data)->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("check")
    }
}

void manapi::ev::callback_watcher_prepare (uv_prepare_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::prepare_ctx *> (s->data)
        ->cb;
    if (cb) {
        MANAPIHTTP_EV_TRY_CALLBACK
        cb (static_cast<manapi::ev::internal::prepare_ctx *> (s->data)->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("prepare")
    }
}

void manapi::ev::callback_watcher_tcp_accept (uv_tcp_t *s, int status) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)
        ->connection;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb (static_cast<manapi::ev::internal::tcp_accept_ctx *> (s->data)->s_, status);
    MANAPIHTTP_EV_CATCH_CALLBACK("tcp accept")
}

void manapi::ev::callback_watcher_tcp_read (uv_stream_t *s, ssize_t nread, const uv_buf_t *buf) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)
        ->read;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::tcp_connection_ctx *> (s->data)->s_, nread, buf);
    MANAPIHTTP_EV_CATCH_CALLBACK("tcp read")
}

void manapi::ev::callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned m_flags) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    auto &cb = static_cast<manapi::ev::internal::udp_ctx *> (s->data)
        ->recv;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb(static_cast<manapi::ev::internal::udp_ctx *> (s->data)->s_, nread, buf, addr, m_flags);
    MANAPIHTTP_EV_CATCH_CALLBACK("udp recv")
}

void manapi::ev::callback_watcher_udp_send (uv_udp_send_t *s, int status) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::udp_send_ctx> ss (static_cast<manapi::ev::internal::udp_send_ctx *> (s->data));
    s->data = nullptr;
    auto &cb =ss->send;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb (ss->s_, status);
    MANAPIHTTP_EV_CATCH_CALLBACK("udp send")
}

void manapi::ev::callback_watcher_write (uv_write_t *s, int status) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::write_ctx> ss (static_cast<manapi::ev::internal::write_ctx *> (s->data));
    s->data = nullptr;
    auto &cb = ss->write;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb (ss->s_, status);
    MANAPIHTTP_EV_CATCH_CALLBACK("write")
}

void manapi::ev::callback_watcher_connect_tcp(uv_connect_t *s, int status) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    std::unique_ptr<manapi::ev::internal::connect_tcp_ctx> ss (static_cast<manapi::ev::internal::connect_tcp_ctx *> (s->data));
    s->data = nullptr;
    auto &cb = ss->cb;
    assert(cb && "ev:User callback wasn't set");
    MANAPIHTTP_EV_TRY_CALLBACK
    cb (ss->tcp, status);
    MANAPIHTTP_EV_CATCH_CALLBACK("connect tcp")
}


void manapi::ev::callback_watcher_fs(uv_fs_t *req) MANAPIHTTP_NOEXCEPT {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::fs_ctx> ss (static_cast<manapi::ev::internal::fs_ctx *> (req->data));
        std::unique_ptr<uv_fs_t, fs_req_deleter> req_own (req);
        req->data = nullptr;

        MANAPIHTTP_EV_TRY_CALLBACK
        if (ss->cb)
            ss->cb(ss->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("fs")
        ss->token.disable();
    }
    else {
        std::unique_ptr<uv_fs_t, fs_req_deleter> req_own (req);
    }
}

void manapi::ev::callback_watcher_random(uv_random_t *s, int status, void *buff, std::size_t size) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");
    if (s->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::random_ctx> ss (static_cast<manapi::ev::internal::random_ctx *> (s->data));
        s->data = nullptr;
        MANAPIHTTP_EV_TRY_CALLBACK
        if (ss->cb)
            ss->cb(ss->s_, status, buff, size);
        MANAPIHTTP_EV_CATCH_CALLBACK("random")
        ss->token.disable();
    }
}

void manapi::ev::callback_watcher_getnameinfo(uv_getnameinfo_t *req, int status, const char *hostname, const char *service) MANAPIHTTP_NOEXCEPT {
    assert(req->data && "ev:User data wasn't set");
    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::getnameinfo_ctx> s (static_cast<manapi::ev::internal::getnameinfo_ctx *> (req->data));
        req->data = nullptr;
        MANAPIHTTP_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_, status, hostname, service);
        MANAPIHTTP_EV_CATCH_CALLBACK("getnameinfo")
        s->token.disable();
    }
}

void manapi::ev::callback_watcher_getaddrinfo(uv_getaddrinfo_t *req, int status, addrinfo *res) MANAPIHTTP_NOEXCEPT {
    assert(req->data && "ev:User data wasn't set");
    std::unique_ptr<addrinfo, addrinfo_deleter> res_own (res);

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::getaddrinfo_ctx> s (static_cast<manapi::ev::internal::getaddrinfo_ctx *> (req->data));
        req->data = nullptr;
        MANAPIHTTP_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_, status, res_own.release());
        MANAPIHTTP_EV_CATCH_CALLBACK("getaddrinfo")
        s->token.disable();
    }
}

void manapi::ev::callback_watcher_after_work(uv_work_t *req, int status) MANAPIHTTP_NOEXCEPT {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        /* otherwise it was cancelled */
        std::unique_ptr<manapi::ev::internal::work_ctx> s (static_cast<manapi::ev::internal::work_ctx *> (req->data));
        req->data = nullptr;
        MANAPIHTTP_EV_TRY_CALLBACK
        if (s->after_cb)
            s->after_cb(s->s_, status);
        MANAPIHTTP_EV_CATCH_CALLBACK("after work")
    }
}

void manapi::ev::callback_watcher_work(uv_work_t *req) MANAPIHTTP_NOEXCEPT {
    assert(req->data && "ev:User data wasn't set");

    if (req->data) {
        auto s = static_cast<manapi::ev::internal::work_ctx *> (req->data);
        /* otherwise it was cancelled */
        MANAPIHTTP_EV_TRY_CALLBACK
        if (s->cb)
            s->cb(s->s_);
        MANAPIHTTP_EV_CATCH_CALLBACK("work")
    }
}


void manapi::ev::callback_watcher_tcp_connection_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) MANAPIHTTP_NOEXCEPT {
    assert(handle->data && "ev:User data wasn't set");
    auto s = static_cast<manapi::ev::internal::tcp_connection_ctx *> (handle->data);
    assert((s->alloc_cb && "ev:User cb wasn't set"));
    MANAPIHTTP_EV_TRY_CALLBACK
    s->alloc_cb(s->s_, suggested_size, buf);
    MANAPIHTTP_EV_CATCH_CALLBACK("tcp conn alloc")
}

void manapi::ev::callback_watcher_udp_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) MANAPIHTTP_NOEXCEPT {
    assert(handle->data && "ev:User data wasn't set");
    auto s = static_cast<manapi::ev::internal::udp_ctx *> (handle->data);
    assert((s->alloc_cb && "ev:User cb wasn't set"));
    MANAPIHTTP_EV_TRY_CALLBACK
    s->alloc_cb(s->s_, suggested_size, buf);
    MANAPIHTTP_EV_CATCH_CALLBACK("tcp udp alloc")
}

void manapi::ev::callback_close_cb(uv_handle_t *s) MANAPIHTTP_NOEXCEPT {
    assert(s->data && "ev:User data wasn't set");

    switch (s->type) {
        case ev::EV_TCP: {
            auto ss = static_cast<manapi::ev::internal::tcp_ctx *> (s->data);
            MANAPIHTTP_EV_TRY_CALLBACK
            if (ss->close_cb)
                ss->close_cb->operator()(ss->s_);
            MANAPIHTTP_EV_CATCH_CALLBACK("close")
            break;
        }
        default: {

            break;
        }
    }
}

static void m_event_loop_try_tasks_(const manapi::ev::shared_idle &, std::shared_ptr<manapi::threadpool> &tp,
    const manapi::ev::shared_prepare &prepare_tasks, const manapi::ev::shared_idle &idle_tasks) {
    auto etaskpool = dynamic_cast<manapi::ethreadpool *>(tp.get());
    if (!etaskpool->try_task()) {
        etaskpool->set_notify();
        prepare_tasks->stop();
        idle_tasks->stop();
    }
}

static void m_event_loop_free_on_finish_cb_( std::map <size_t, std::move_only_function<void()>> &m ) {
    while (!m.empty()) {
        const auto it = m.begin();
        if (it->second) {
            try {
                it->second();
            }
            catch (std::exception const &e) {
                manapi_log_error("eventloop:finish callback failed due to %s", e.what());
            }
        }
        m.erase(it);
    }
}

static manapi::future<> m_event_loop_call_on_finish_cb(std::map <size_t, std::pair<int, std::move_only_function<manapi::future<>()>>> &m) {
    while (!m.empty()) {
        using item_t = std::pair<int, std::move_only_function<manapi::future<void>()>>;
        std::vector<item_t> values;
        values.reserve(m.size());
        while (!m.empty()) {
            const auto it = m.extract(m.begin());
            values.push_back(std::move(it.mapped()));
        }

        std::sort(values.begin(), values.end(), +[](const item_t& a, const item_t& b)
            -> bool { return a.first > b.first; });

        for (auto &it : values) {
            if (it.second) {
                try {
                    auto callback = std::move(it.second);
                    co_await manapi::async::invoke(std::move(callback));
                }
                catch (std::exception const &e) {
                    manapi_log_error("eventloop:finish callback failed due to %s", e.what());
                }
            }
        }
    }
}

static manapi::ev::status m_event_loop_register_(std::mutex &stop_mx, std::unordered_map <size_t, std::shared_ptr<manapi::event_loop>> &events, std::shared_ptr<manapi::event_loop> loop) MANAPIHTTP_NOEXCEPT {
    std::lock_guard<std::mutex> lk (stop_mx);

    try {
        events.insert({reinterpret_cast<std::size_t> (loop.get()),
            loop});
        return manapi::ev::status_ok();
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

static void m_event_loop_unregister_(std::mutex &stop_mx, std::unordered_map <size_t, std::shared_ptr<manapi::event_loop>> &events, manapi::event_loop *loop) MANAPIHTTP_NOEXCEPT {
    std::lock_guard<std::mutex> lk (stop_mx);
    events.erase(reinterpret_cast<std::size_t> (loop));
}

manapi::event_loop::event_loop() {
    this->m_deps = 0;
    this->m_flags = 0;
}

manapi::ev::status_or<std::shared_ptr<manapi::event_loop>> manapi::event_loop::create(std::shared_ptr<threadpool> taskpool, std::shared_ptr<manapi::logger> logger) {
    auto ev = std::shared_ptr<manapi::event_loop>(new manapi::event_loop());
    ev::status status;
    try {
        ev->m_loop = std::make_unique<uv_loop_t>();

#if MANAPIHTTP_CURL_DEPENDENCY
        ev->m_curl_watcher = std::make_unique<ev::internal::curl_watcher_t>();
#endif

        ev->m_callback_watcher = std::make_unique<ev::internal::custom_callback_t>();
        ev->m_logger = std::move(logger);
        ev->m_taskpool = std::move(taskpool);
        ev->m_flags = 0;

        if (auto rhs = uv_loop_init(ev->m_loop.get())) {
            status = ev::status_internal("uv_loop_init:Failed", rhs);
            goto err;
        }

        auto idle_tasks_res = ev->create_watcher_idle([ev = ev.get()] (const ev::shared_idle &w) mutable
            -> void {
            ::m_event_loop_try_tasks_(w, ev->m_etaskpool, ev->m_prepare_tasks, ev->m_idle_tasks);
        });

        if (!idle_tasks_res)
            return idle_tasks_res.err();

        auto iter_tasks_res = ev->create_watcher_prepare([ev = ev.get()] (const ev::shared_prepare &w) mutable
            -> void {
            auto etaskpool = dynamic_cast<ethreadpool *>(ev->m_etaskpool.get());
            while (etaskpool->try_task()) {}
            etaskpool->set_notify();
            w->stop();
            ev->m_prepare_tasks->stop();
        });

        ev->m_idle_tasks = idle_tasks_res.unwrap();
        ev->m_prepare_tasks = iter_tasks_res.unwrap();

        ev->m_etaskpool = std::make_shared<manapi::ethreadpool>(ev->m_logger, [ev = ev.get()] ()
            -> void {
            ev->m_idle_tasks->start();
            ev->m_prepare_tasks->start();
        });

        dynamic_cast<ethreadpool *>(ev->m_etaskpool.get())->set_notify();

        auto adding_async_res = ev->create_watcher_async([ev = ev.get()] (const std::shared_ptr<ev::async> &w)
            -> void { ev->custom_watcher_callback_async(w); });

        if (!adding_async_res) {
            status = adding_async_res.err();
            goto err;
        }

        ev->m_callback_watcher->adding_async = adding_async_res.unwrap();
        ev->m_callback_watcher->adding_async->unref();
        ev->m_callback_watcher->adding_async_cb = [ev = ev.get()] ()
            -> void { ev->m_callback_watcher->adding_async->send(); };

#if MANAPIHTTP_CURL_DEPENDENCY
        ev->m_curl_watcher->curl_multi.reset(curl_multi_init());

        if (!ev->m_curl_watcher->curl_multi) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "curl_multi_init");
            status = ev::status_internal("curl_multi_init", ev::ERR_NOMEM);
            goto err;
        }

        curl_multi_setopt(ev->m_curl_watcher->curl_multi.get(), CURLMOPT_SOCKETFUNCTION, event_loop::handle_curl_socket);
        curl_multi_setopt(ev->m_curl_watcher->curl_multi.get(), CURLMOPT_SOCKETDATA, ev.get());
#endif

        if (auto rhs = ev->m_prepare_tasks->start()) {
            status = ev::status_internal("prepare_tasks::start", rhs);
            goto err;
        }

        if (auto rhs =ev->m_idle_tasks->start()) {
            status = ev::status_internal("idle_tasks::start", rhs);
            goto err;
        }

        /* interrupted */
        auto interrupt_watcher_res = ev->create_watcher_async([ev = ev.get()] (const std::shared_ptr<ev::async> &w)
            -> void { async::run(ev->stop()); });

        if (!interrupt_watcher_res) {
            status = interrupt_watcher_res.err();
            goto err;
        }

        ev->m_interrupted_watcher = interrupt_watcher_res.unwrap();

        return std::move(ev);
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "event_loop:create failed", e.what());
        status = ev::status_internal("event_loop:create failed", ev::ERR_UNKNOWN);
    }

    err: {
        return std::move(status);
    }
}

manapi::event_loop::~event_loop() {
    ::m_event_loop_unregister_ (event_loop::m_stop_mx, event_loop::m_events, this);

    if (this->m_callback_watcher) {
        std::lock_guard<std::mutex> lk (this->m_callback_watcher->adding_mx);
        this->stop_watcher(std::move(this->m_callback_watcher->adding_async));
        this->m_callback_watcher->adding_async_cb = nullptr;
    }
    this->stop_watcher(std::move(this->m_interrupted_watcher));
    this->stop_watcher(std::move(this->m_prepare_tasks));
    this->stop_watcher(std::move(this->m_idle_tasks));

#if MANAPIHTTP_CURL_DEPENDENCY
    this->m_curl_watcher->curl_multi.reset();
#endif
    this->m_etaskpool.reset();
}

void manapi::event_loop::setup_handle_interrupt() MANAPIHTTP_NOEXCEPT {
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
    if (this->m_flags & EVENT_LOOP_FLAG_STOPPING) {
        co_return;
    }

    this->m_flags |= EVENT_LOOP_FLAG_STOPPING;

    co_await ::m_event_loop_call_on_finish_cb(this->m_map_finish_cb);

    ::m_event_loop_free_on_finish_cb_(this->m_map_clean_up_cb);

    this->m_map_finish_cb.clear();
    this->m_map_clean_up_cb.clear();

    if (this->m_flags & EVENT_LOOP_FLAG_ACTIVE) {
        ::uv_stop(this->m_loop.get());
    }

    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "eventloop:stop() has been finished");
}

void manapi::event_loop::wait_all (bool shutdown) MANAPIHTTP_NOEXCEPT {
    if (!(this->m_flags & EVENT_LOOP_FLAG_STOPPING)) {
        manapi::async::run (this->stop());
    }

    ::m_event_loop_unregister_ (event_loop::m_stop_mx, event_loop::m_events, this);

    this->stop_watcher(std::move(this->m_interrupted_watcher));
    this->stop_watcher(std::move(this->m_idle_tasks));
    this->stop_watcher(std::move(this->m_prepare_tasks));

    if (this->m_loop) {
        auto etaskpool = dynamic_cast<ethreadpool *>(this->m_etaskpool.get());

        while (true) {
            MANAPIHTTP_MUST_ALLOC_START
            etaskpool->set_notify_cb([etaskpool, this] () -> void {
                MANAPIHTTP_MUST_ALLOC_START
                auto idle_tasks_res = this->create_watcher_idle(
                    [etaskpool, this] (const ev::shared_idle &w) -> void {
                    while (true) {
                        if (!etaskpool->try_task()) {
                            break;
                        }
                    }
                    etaskpool->set_notify();

                    auto const m_loop = this->loop();
                    this->stop_watcher(w);

                    // try again (idle_tasks can be the last one)
                    manapi::async::current()->timerpool()->stop();

                    if (!::uv_loop_alive(m_loop)) {
                        ::uv_stop(m_loop);
                    }
                });

                if (!idle_tasks_res) {
                    if (!this->m_loop) {
                        manapi_log_warn("New tasks were added when event loop is deactivated");
                        manapi::print_stacktrace();
                        return;
                    }

                    idle_tasks_res.err().log();
                    throw std::bad_alloc{};
                }

                auto idle_tasks = idle_tasks_res.unwrap();
                if (auto rhs = idle_tasks->start()) {
                    manapi_log_error("%s:%s failed due to %s", "event loop", "idle taskpool start",
                        ev::strerror(rhs));
                }
                MANAPIHTTP_MUST_ALLOC_END
            });
            MANAPIHTTP_MUST_ALLOC_END

            this->m_flags |= EVENT_LOOP_FLAG_ACTIVE;
            while (etaskpool->try_task()) {}

            etaskpool->set_notify();

            // it's useful if TimeEvent is removed in TimerPool
            manapi::async::current()->timerpool()->run_once();

            manapi::async::current()->timerpool()->stop();

            auto const rhs = ::uv_run(this->loop(), UV_RUN_DEFAULT);
            if (this->m_flags & EVENT_LOOP_FLAG_ACTIVE) {
                this->m_flags ^= EVENT_LOOP_FLAG_ACTIVE;
            }

            if (!rhs && !::uv_loop_alive(this->loop()) && !this->m_etaskpool->tasks_size()) {

                std::lock_guard<std::mutex> lk (this->m_callback_watcher->adding_mx);
                if (shutdown && this->m_callback_watcher->adding_async) {
                    if (!uv_has_ref((uv_handle_t *)this->m_callback_watcher->adding_async->custom())) {
                        this->m_callback_watcher->adding_async->ref();
                    }
                    if (!this->m_deps) {
                        this->stop_watcher(std::move(this->m_callback_watcher->adding_async));
                    }
                }
                else {
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                        "%s:%s", "eventloop", "uv_loop_alive has returned 0");
                    break;
                }
            }
        }

        etaskpool->set_notify_cb(nullptr);

        if (shutdown) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "eventloop:well done");

            if (this->m_curl_watcher->timeout_watcher)
                this->m_curl_watcher->timeout_watcher.stop();

            if (auto rhs = ::uv_loop_close(this->m_loop.get())) {
#ifndef MANAPIHTTP_DISABLE_TRACE_HARD
                ::uv_print_all_handles(this->loop(), stderr);
#endif
                manapi_log_error("%s failed due to %s", "uv_loop_close", ev::strerror(rhs));
            }
            else {
                this->m_loop.reset();
            }

            if (this->m_flags & EVENT_LOOP_FLAG_STOPPING) {
                this->m_flags ^= EVENT_LOOP_FLAG_STOPPING;
            }
            if (this->m_flags & EVENT_LOOP_FLAG_ACTIVE) {
                this->m_flags ^= EVENT_LOOP_FLAG_ACTIVE;
            }
        }
    }
}

void manapi::event_loop::timerpool_init (std::shared_ptr<manapi::timerpool> tp) {
#if MANAPIHTTP_CURL_DEPENDENCY
    /* curl fetch timeout */
    this->m_curl_watcher->timeout_watcher = tp->append_interval_sync(200, [this] (manapi::timer t) -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    }).unwrap();
#endif
}

size_t manapi::event_loop::subscribe_finish(int priority, std::move_only_function<manapi::future<void>()> cb) {
    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->m_map_finish_cb.insert({id, std::make_pair(priority, std::move(cb))}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_INTERNAL, "index {} exists", id);
    }

    return id;
}

std::size_t manapi::event_loop::subscribe_clean_up(std::move_only_function<void()> cb) {
    auto id = *reinterpret_cast<const std::size_t *> (&cb);

    if (!this->m_map_clean_up_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_INTERNAL, "index {} exists", id);
    }

    return id;

}

void manapi::event_loop::unsubscribe_clean_up(std::size_t id) {
    if (!id) { return; }
    this->m_map_clean_up_cb.erase(id);

}

void manapi::event_loop::unsubscribe_finish(std::size_t id) {
    if (!id) { return; }
    this->m_map_finish_cb.erase(id);
}

manapi::ev::loop_ref manapi::event_loop::loop() const MANAPIHTTP_NOEXCEPT {
    return this->m_loop.get();
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::tcp>> manapi::event_loop::create_watcher_tcp_accept( ev::tcp_accept_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::tcp>();
        auto ctx = new ev::internal::tcp_accept_ctx;

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");

        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);

        ctx->type = 1;
        ctx->s_ = w;
        ctx->connection = std::move(callback);
        ctx->close_cb = nullptr;

        w->data(ctx);

        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::tcp>> manapi::event_loop::create_watcher_tcp_connection( ev::tcp_connection_cb read, ev::tcp_alloc_cb alloc_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::tcp>();
        auto ctx = std::make_unique<ev::internal::tcp_connection_ctx>();
        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        ctx->type = 0;
        ctx->s_ = w;
        ctx->read = std::move(read);
        ctx->alloc_cb = std::move(alloc_cb);
        ctx->close_cb = nullptr;

        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::pair<std::shared_ptr<manapi::ev::connect>, std::shared_ptr<manapi::ev::tcp>>> manapi::event_loop::connect_tcp(const sockaddr *addr, ev::connect_tcp_cb on_connect, ev::tcp_connection_cb read, ev::tcp_alloc_cb alloc_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto c = std::make_shared<ev::connect>();
        auto ctx = std::make_unique<ev::internal::connect_tcp_ctx>();
        auto status = this->create_watcher_tcp_connection(std::move(read), std::move(alloc_cb));
        if (!status)
            return status.err();
        auto w = status.unwrap();

        if (auto rhs = c->bind(w->custom(), addr, ev::callback_watcher_connect_tcp)) {
            this->stop_watcher(std::move(w));
            return manapi::ev::status_invalid_argument("ev:Bind(tcp) failed", rhs);
        }

        ctx->cb = std::move(on_connect);
        ctx->tcp = w;
        c->data(ctx.release());

        return std::make_pair(std::move(c), std::move(w));
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::udp>> manapi::event_loop::create_watcher_udp(ev::udp_cb recv, ev::udp_alloc_cb alloc_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::udp>();
        auto ctx = std::make_unique<ev::internal::udp_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop)) {
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }
        ctx->s_ = w;
        ctx->recv = std::move(recv);
        ctx->alloc_cb = std::move(alloc_cb);

        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

manapi::status manapi::event_loop::custom_callback(std::move_only_function<void(event_loop *ev)> cb) MANAPIHTTP_NOEXCEPT {
    return this->custom_callback(&cb);
}

manapi::status manapi::event_loop::custom_callback(std::move_only_function<void(event_loop *ev)> *cb) MANAPIHTTP_NOEXCEPT {
    try {
        std::unique_lock<std::mutex> lk (this->m_callback_watcher->adding_mx);
        if (!this->m_callback_watcher->adding_async) {
            return status_internal("custom_callback:eventloop was stopped");
        }
        this->m_callback_watcher->callback_data.push_back({nullptr});
        this->m_callback_watcher->callback_data.back().cb = std::move(*cb);
        this->m_deps++;
        lk.unlock();
        this->m_callback_watcher->adding_mcv.unlock();
        this->m_callback_watcher->adding_async_cb();
        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "custom_callback:Failed",e.what());
        return status_internal("custom_callback:Failed");
    }
}

#if MANAPIHTTP_CURL_DEPENDENCY
void manapi::event_loop::handle_curl_exec_connections() {
    int running_handles = 0;
    auto mcode = curl_multi_perform(this->m_curl_watcher->curl_multi.get(), &running_handles);

    if (mcode != CURLM_OK) {
        return;
    }
}

void manapi::event_loop::handle_curl_check_connections() {

    int msgs_left;
    CURLMsg *msg;
    while (true) {
        msg = curl_multi_info_read(this->m_curl_watcher->curl_multi.get(), &msgs_left);

        if (!msg) {
            break;
        }

        if (msg->msg == CURLMSG_DONE) {
            curl_multi_remove_handle(this->m_curl_watcher->curl_multi.get(), msg->easy_handle);
            auto data = this->m_curl_watcher->curl_res.extract(msg->easy_handle);
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
    if (this->m_callback_watcher->adding_mx.try_lock()) {
        auto list = std::move(this->m_callback_watcher->callback_data);
        this->m_callback_watcher->adding_mx.unlock();

        while (!list.empty()) {
            auto data = std::move(list.front());
            list.pop_front();

            this->decrease_deps();

            try {
                data.cb(this);
            }
            catch (std::exception const &e) {
                manapi_log_error("custom cb:Failed callback due to %s", e.what());
            }
        }
    }
}

#if MANAPIHTTP_CURL_DEPENDENCY
manapi::ev::status_or<std::shared_ptr<manapi::ev::io>> manapi::event_loop::handle_curl_watcher_gen (manapi::event_loop * data, manapi::socket_t fd) MANAPIHTTP_NOEXCEPT {
    try {
        return data->create_watcher_socket(fd, [data, fd] (const std::shared_ptr<ev::io> &w, int status, int revents)
                -> void {
            auto data2 = data;
            //MANAPIHTTP_LOG("CURL EV: {} {}", revents, static_cast<int>(fd));
            int cnt; auto rhs = curl_multi_socket_action(data->m_curl_watcher->curl_multi.get(), fd, (revents & 0b11), &cnt);
            if (rhs != CURLM_OK) {
                data->m_logger->debug("curl_multi_socket_action(...) returned an invalid response: {}", static_cast<int>(rhs));
            }
            data2->handle_curl_check_connections();
        });
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::socket_t manapi::event_loop::handle_curl_open_socket(void *cbp, int socktype, void *addr) {
    auto data = static_cast<event_loop *>(cbp);

    manapi::socket_t fd;

    try {
        auto curladdr = static_cast<curl_sockaddr*>(addr);
        auto res = manapi::async::create_socket(curladdr->family, curladdr->protocol, curladdr->socktype);
        if (!res) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "curl open sock failed: %.*s",
                res.message().size(), res.message().data());
            return -1;
        }
        fd = res.unwrap();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "curl open sock failed: %s", e.what());
        return -1;
    }

    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl open sock: %d", fd);

    return fd;
}

int manapi::event_loop::handle_curl_socket(void *curl, manapi::socket_t fd, int revents, void *userp, void *) {
    try {
        auto data = static_cast<event_loop *> (userp);

        if (!data)
            return -1;

        if ((revents & 0b11)) {
            auto it = data->m_curl_watcher->watchers.find(fd);

            if (it != data->m_curl_watcher->watchers.end()) {
                it->second->restart(revents);
            }
            else {
                auto watcher_res = handle_curl_watcher_gen(data, fd);
                if (!watcher_res) {
                    return watcher_res.code();
                }

                auto watcher = watcher_res.unwrap();
                watcher->start(revents & 0b11);
                data->m_curl_watcher->watchers.insert({fd, std::move(watcher)});

                auto self = data->m_curl_watcher->curl_res.find(curl);
                if (self != data->m_curl_watcher->curl_res.end()) {
                    self->second.watcher = watcher;
                }
            }
        }
        else if ((revents & CURL_POLL_REMOVE)) {
            // auto it = data->m_curl_watcher.watchers.find(fd);
            // it->second->m_events = ev::READ;
            //MANAPIHTTP_LOG("curl ev shutdown: {}",(int) fd);
            auto mapped = data->m_curl_watcher->watchers.extract(fd);
            if (!mapped.empty() && mapped.mapped()) {
                data->stop_watcher(mapped.mapped());
            }
        }

        return 0;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s:%s failed due to %s", "eventloop", "handle_curl_socket", e.what());
        return -1;
    }
}

int manapi::event_loop::handle_curl_close_socket(void *cbp, curl_socket_t socket) {
    auto data = static_cast<event_loop *>(cbp);
    auto watcher_data = data->m_curl_watcher->watchers.extract(static_cast<socket_t>(socket));
    if (!watcher_data.empty() && watcher_data.mapped()) {
        data->stop_watcher(watcher_data.mapped());
    }
    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl close sock: %d", socket);
    async::close_descriptor(static_cast<socket_t>(socket));
    return 0;
}
#endif

void manapi::event_loop::stop_watcher(std::shared_ptr<ev::tcp> s) MANAPIHTTP_NOEXCEPT {
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

const std::shared_ptr<manapi::threadpool> &manapi::event_loop::taskpool() const MANAPIHTTP_NOEXCEPT {
    return this->m_etaskpool;
}

#if MANAPIHTTP_CURL_DEPENDENCY
manapi::status manapi::event_loop::watch_curl(void * shared_curl, std::move_only_function<void(int result)> cb) MANAPIHTTP_NOEXCEPT {
    auto &curl = *static_cast<std::shared_ptr<CURL> *> (shared_curl);
    /* add */
    if (CURLE_OK != curl_easy_setopt (curl.get(), CURLOPT_OPENSOCKETFUNCTION, handle_curl_open_socket))
        return status_invalid_argument ("watch_curl:curl_easy_setopt failed");
    if (CURLE_OK != curl_easy_setopt (curl.get(), CURLOPT_OPENSOCKETDATA, this))
        return status_invalid_argument ("watch_curl:curl_easy_setopt failed");

    if (CURLE_OK != curl_easy_setopt (curl.get(), CURLOPT_CLOSESOCKETFUNCTION, handle_curl_close_socket))
        return status_invalid_argument ("watch_curl:curl_easy_setopt failed");
    if (CURLE_OK != curl_easy_setopt (curl.get(), CURLOPT_CLOSESOCKETDATA, this))
        return status_invalid_argument ("watch_curl:curl_easy_setopt failed");

    try {
        ev::internal::curl_res_value_t curl_res_value{};
        curl_res_value.self = curl;
        curl_res_value.finish = std::move(cb);
        if (!this->m_curl_watcher->curl_res.insert({curl.get(), std::move(curl_res_value)}).second)
            return status_already_exists("duplicate");

        CURLMcode const mcode = curl_multi_add_handle(this->m_curl_watcher->curl_multi.get(), curl.get());

        if (mcode != CURLM_OK) {
            this->m_curl_watcher->curl_res.erase(curl.get());
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl_multi_add_handle() using %p returned %d",
                curl.get(), static_cast<int>(mcode));
            return status_invalid_argument("curl_multi_add_handle failed");
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "watch_curl:Failed", e.what());
        return manapi::status_internal("watch_curl:Failed");
    }

    MANAPIHTTP_MUST_ALLOC_START
    this->m_etaskpool->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
    MANAPIHTTP_MUST_ALLOC_END

    return manapi::status_ok();
}

manapi::status manapi::event_loop::unwatch_curl(void * shared_curl) MANAPIHTTP_NOEXCEPT {
    auto &curl = *static_cast<std::shared_ptr<CURL> *> (shared_curl);
    /* remove */
    auto curl_data = this->m_curl_watcher->curl_res.extract(curl.get());
    if (curl_data.empty()) {
        /** already was removed */
        return manapi::status_ok();
    }

    CURLMcode const mcode = curl_multi_remove_handle(this->m_curl_watcher->curl_multi.get(), curl.get());

    if (mcode != CURLM_OK) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl_multi_remove_handle() using %p returned %d", curl.get(),
            static_cast<int>(mcode));
        return status_invalid_argument("curl_multi_remove_handle failed");
    }

    auto &mapped = curl_data.mapped();
    if (mapped.watcher) {
        this->stop_watcher(curl_data.mapped().watcher);
    }

    try {
        mapped.finish(CURLE_ABORTED_BY_CALLBACK);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "curl", "mapped.finish", e.what());
    }

    MANAPIHTTP_MUST_ALLOC_START
    this->m_etaskpool->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
    MANAPIHTTP_MUST_ALLOC_END

    return manapi::status_ok();
}

manapi::status manapi::event_loop::pause_watch_curl(void * shared_curl) MANAPIHTTP_NOEXCEPT {
    auto &curl = *static_cast<std::shared_ptr<CURL> *> (shared_curl);
    /* pause */
    const auto rhs = curl_easy_pause(curl.get(), CURLPAUSE_ALL);
    if (CURLE_OK != rhs) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl_easy_pause() using %p returned %d",
            curl.get(), static_cast<int>(rhs));
        return status_invalid_argument("curl_easy_pause failed");
    }
    MANAPIHTTP_MUST_ALLOC_START
    this->m_etaskpool->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
    MANAPIHTTP_MUST_ALLOC_END
    return status_ok();
}

manapi::status manapi::event_loop::unpause_watch_curl(void *shared_curl) MANAPIHTTP_NOEXCEPT {
    auto &curl = *static_cast<std::shared_ptr<CURL> *> (shared_curl);
    /* unpause */
    const auto rhs = curl_easy_pause(curl.get(), CURLPAUSE_CONT);

    if (CURLE_OK != rhs) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "curl_easy_pause() using %p returned %d", curl.get(),
            static_cast<int>(rhs));
        return status_invalid_argument("curl_easy_pause failed");
    }

    MANAPIHTTP_MUST_ALLOC_START
    this->m_etaskpool->append_task([this] () -> void {
        this->handle_curl_exec_connections();
        this->handle_curl_check_connections();
    });
    MANAPIHTTP_MUST_ALLOC_END

    return manapi::status_ok();
}
#endif

void manapi::event_loop::interrupt(int sig) MANAPIHTTP_NOEXCEPT {
    std::unique_lock <std::mutex> lk (event_loop::m_stop_mx, std::try_to_lock);
    if (!lk.owns_lock()) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "interrupt:try_to_lock failed");
        switch (sig) {
            case SIGABRT:
            case SIGTERM:
            case SIGINT:
                break;
            /* well well well */
            default:
                print_stacktrace ();
                std::quick_exit(1);
            return;
        }
        lk.lock();
    }
    auto prev = event_loop::m_interrupted.exchange(true);

    switch (sig) {
        case SIGABRT:
        case SIGTERM:
        case SIGINT:
            if (!prev)
                break;
            manapi_log_error("eventloop:2nd interrupt was received");
            /* well well well */
        default:
            print_stacktrace ();
            std::quick_exit(1);
        return;
    }

    for (auto &m_loop :  manapi::event_loop::m_events) {
        if (auto rhs = m_loop.second->m_interrupted_watcher->send())
            manapi_log_error("%s:%s failed due to %s",
                "eventloop", "send interrupt signal", ev::strerror(rhs));
    }

    manapi::event_loop::m_events.clear();
}

void manapi::event_loop::lock(async::shared_cthread ctx, std::mutex &mx) MANAPIHTTP_NOEXCEPT {
    if (ctx) {
        auto &ev = ctx->eventloop();
        while (true) {
            auto trying_was_ok = ev->m_callback_watcher->adding_mcv.try_lock();

            ev->custom_watcher_callback_async(ev->m_callback_watcher->adding_async);

            if (mx.try_lock()) {
                mx.unlock();
                break;
            }

            std::lock_guard<std::mutex> lk (ev->m_callback_watcher->adding_mcv);
        }
    }
    else {
        mx.lock();
    }
}

void manapi::event_loop::unlock(async::shared_cthread ctx, std::mutex &mx) MANAPIHTTP_NOEXCEPT {
    if (ctx) {
        auto &ev = ctx->eventloop();
        mx.unlock();
        ev->m_callback_watcher->adding_mcv.unlock();
    }
    else {
        mx.unlock();
    }
}

manapi::ev::status manapi::event_loop::run() MANAPIHTTP_NOEXCEPT {
    return this->run(manapi::ev::RUN_DEFAULT);
}

manapi::ev::status manapi::event_loop::start() MANAPIHTTP_NOEXCEPT {
    if (this->m_custom_event_loop) {
        if (this->m_flags & EVENT_LOOP_FLAG_STOPPING) {
            return manapi::status_aborted("event loop is stopping");
        }

        if (this->m_flags & EVENT_LOOP_FLAG_ACTIVE) {
            return manapi::status_already_exists("event loop already has started");
        }

        try {
            this->m_custom_event_loop ();
        }
        catch (std::exception const &e) {
            manapi_log_error("%s:%s due to %s", "event loop", "custom event loop failed", e.what());
            return ev::status_internal("custom event loop failed", ev::ERR_UNKNOWN);
        }
        return status_ok();
    }
    return this->run();
}

void manapi::event_loop::breakit() MANAPIHTTP_NOEXCEPT {
    if (this->m_loop)
        ::uv_stop(this->m_loop.get());
}

manapi::ev::status manapi::event_loop::run(manapi::ev::run_modes mode) MANAPIHTTP_NOEXCEPT {
    manapi::ev::status status;
    if (this->m_flags & EVENT_LOOP_FLAG_STOPPING) {
        return manapi::status_aborted("event loop is stopping");
    }

    if (this->m_flags & EVENT_LOOP_FLAG_ACTIVE) {
        return manapi::status_already_exists("event loop already has started");
    }

    status = ::m_event_loop_register_ (event_loop::m_stop_mx, event_loop::m_events, this->shared_from_this());
    if (status) {
        try {
            assert(this->m_loop.get());
            this->m_flags |= EVENT_LOOP_FLAG_ACTIVE;
            if (mode == (manapi::ev::RUN_ONCE|manapi::ev::RUN_NOWAIT)) {
                ::m_event_loop_try_tasks_(this->m_idle_tasks, this->m_etaskpool, this->m_prepare_tasks, this->m_idle_tasks);
            }
            ::uv_run(this->m_loop.get(), static_cast<uv_run_mode>(mode));
            this->m_flags ^= EVENT_LOOP_FLAG_ACTIVE;
            status = ev::status_ok();
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "eventloop", e.what());
            ::m_event_loop_unregister_ (event_loop::m_stop_mx, event_loop::m_events, this);
            status = ev::status_internal("eventloop", ev::ERR_UNKNOWN);
        }
    }

    return std::move(status);
}

bool manapi::event_loop::is_stopping() const MANAPIHTTP_NOEXCEPT {
    return this->m_flags & EVENT_LOOP_FLAG_STOPPING;
}

bool manapi::event_loop::is_active() const MANAPIHTTP_NOEXCEPT {
    return this->m_flags & EVENT_LOOP_FLAG_ACTIVE;
}

void manapi::event_loop::custom_event_loop(std::move_only_function<void()> block_cb) MANAPIHTTP_NOEXCEPT {
    this->m_custom_event_loop = std::move(block_cb);
}

void manapi::event_loop::increase_deps() MANAPIHTTP_NOEXCEPT {
    std::lock_guard<std::mutex> lk (this->m_callback_watcher->adding_mx);
    assert(this->m_callback_watcher->adding_async);
    this->m_deps++;
}

void manapi::event_loop::decrease_deps() MANAPIHTTP_NOEXCEPT {
    std::lock_guard<std::mutex> lk (this->m_callback_watcher->adding_mx);
    assert(this->m_callback_watcher->adding_async);
    if (!--this->m_deps && uv_has_ref((uv_handle_t *)this->m_callback_watcher->adding_async->custom())) {
        this->m_callback_watcher->adding_async->unref();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::io>> manapi::event_loop::create_watcher_fd(int fd, ev::io_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::io>();
        auto ctx = std::make_unique<ev::internal::io_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (const auto rhs = w->bind(loop, fd))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);

        ctx->s_ = w;
        ctx->cb = std::move(callback);

        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::idle>> manapi::event_loop::create_watcher_idle(ev::idle_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::idle>();
        auto ctx = std::make_unique<ev::internal::idle_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        ctx->s_ = w;
        ctx->cb = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::io>> manapi::event_loop::create_watcher_socket(socket_t sock, ev::io_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::io>();
        auto ctx = std::make_unique<ev::internal::io_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto const rhs = w->bind(loop, sock))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        ctx->s_ = w;
        ctx->cb = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::async>> manapi::event_loop::create_watcher_async(ev::async_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::async>();
        auto ctx = std::make_unique<ev::internal::async_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        ctx->s_ = w;
        ctx->cb = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::timer>> manapi::event_loop::create_watcher_timer(ev::timer_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::timer>();
        auto ctx = std::make_unique<ev::internal::timer_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);

        ctx->s_ = w;
        ctx->cb = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::prepare>> manapi::event_loop::create_watcher_prepare(ev::prepare_cb callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::prepare>();
        auto ctx = std::make_unique<ev::internal::prepare_ctx>();

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop))
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);

        ctx->s_ = w;
        ctx->cb = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::fs>> manapi::event_loop::create_watcher_fs(ev::fs_cb callback, manapi::ctoken token) MANAPIHTTP_NOEXCEPT {
    try {
        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        auto w = std::make_shared<ev::fs>(loop);
        auto ctx = std::make_unique<ev::internal::fs_ctx>();

        if (token) {
            token.cancel_callback([w = std::weak_ptr (w)] () -> void {
                auto ww = w.lock();
                if (ww)
                    manapi::async::current()->eventloop()->stop_watcher(std::move(ww));
            });
        }

        ctx->s_ = w;
        ctx->cb = std::move(callback);
        ctx->token = std::move(token);

        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::getaddrinfo>> manapi::event_loop::create_watcher_getaddrinfo(const char *node, const char *service, const addrinfo *hints, ev::getaddrinfo_cb callback, manapi::ctoken token) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::getaddrinfo>();
        auto ctx = std::make_unique<ev::internal::getaddrinfo_ctx>();

        if (token) {
            token.cancel_callback([w = std::weak_ptr (w)] () -> void {
                auto ww = w.lock();
                if (ww)
                    manapi::async::current()->eventloop()->stop_watcher(std::move(ww));
            });
        }

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop, node, service, hints)) {
            token.disable();
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }

        ctx->s_ = w;
        ctx->cb = std::move(callback);
        ctx->token = std::move(token);

        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::getnameinfo>> manapi::event_loop::create_watcher_getnameinfo(const sockaddr *addr, int m_flags, ev::getnameinfo_cb callback, manapi::ctoken token) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::getnameinfo>();
        auto ctx = std::make_unique<ev::internal::getnameinfo_ctx>();

        if (token) {
            token.cancel_callback([w = std::weak_ptr (w)] () -> void {
                auto ww = w.lock();
                if (ww)
                    manapi::async::current()->eventloop()->stop_watcher(std::move(ww));
            });
        }

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop, addr, m_flags)) {
            token.disable();
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }

        ctx->s_ = w;
        ctx->cb = std::move(callback);
        ctx->token = std::move(token);

        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::random>> manapi::event_loop::create_watcher_random(char *buff, std::size_t size, ev::random_cb callback, manapi::ctoken token) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::random>();
        auto ctx = std::make_unique<ev::internal::random_ctx>();

        if (token) {
            token.cancel_callback([w = std::weak_ptr (w)] () -> void {
                manapi::async::current()->eventloop()->stop_watcher(w.lock());
            });
        }

        auto const loop = this->m_loop.get();
        if (!loop)
            return ev::status_not_found("ev:loop");
        if (auto rhs = w->bind(loop, buff, size)) {
            token.disable();
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }
        ctx->s_ = w;
        ctx->cb = std::move(callback);
        ctx->token = std::move(token);

        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::write>> manapi::event_loop::create_watcher_write(ev::tcp *conn, ev::write_cb callback, const ev::buff_t *bufs, uint32_t nbuf) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = std::make_shared<ev::write>();
        auto ctx = std::make_unique<ev::internal::write_ctx>();
        if (auto rhs = w->bind(reinterpret_cast <ev::stream_t *>(conn->custom()), bufs, nbuf)) {
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }
        ctx->s_ = w;
        ctx->write = std::move(callback);
        w->data(ctx.release());
        return std::move(w);
    }
    catch (std::exception const &) {
        return ev::status_resource_exhausted();
    }
}

manapi::ev::status_or<std::shared_ptr<manapi::ev::udp_send>> manapi::event_loop::create_watcher_udp_send(ev::udp *conn, ev::udp_send_cb callback, const ev::buff_t *bufs, uint32_t nbuf, sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    try {
        auto ctx = std::make_unique<ev::internal::udp_send_ctx>(nullptr, std::move(callback));
        auto w = std::make_shared<ev::udp_send>();

        if (auto rhs = w->bind(conn->custom(), bufs, nbuf, addr))
            return ev::status_internal("eventloop:Bind failed", rhs);

        ctx->s_ = w;
        w->data(ctx.release());

        return std::move(w);
    }
    catch (std::bad_alloc const &) {
        return ev::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "eventloop:Create watcher failed", e.what());
    }

    return manapi::ev::status_internal("eventloop:Create watcher failed", ev::ERR_UNKNOWN);
}

manapi::ev::status_or<manapi::ev::shared_work> manapi::event_loop::append_task(std::move_only_function<void(const ev::shared_work &w)> work, std::move_only_function<void(const ev::shared_work &w, int status)> after_work) MANAPIHTTP_NOEXCEPT {
    try {
        assert(work);

        auto w = std::make_shared<ev::work>();

        std::unique_ptr<ev::internal::work_ctx> data_ctx (new ev::internal::work_ctx{.s_ = w, .cb = std::move(work), .after_cb = std::move(after_work)});

        w->data(data_ctx.get());

        if (auto rhs = w->bind(this->loop())) {
            w->data(nullptr);
            return manapi::ev::status_invalid_argument("ev:Bind failed", rhs);
        }

        data_ctx.release();

        return std::move(w);
    }
    catch (std::exception const &e) {
        return manapi::ev::status_resource_exhausted();
    }
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

MANAPIHTTP_EV_UNWATCHER(idle, idle_ctx);
MANAPIHTTP_EV_UNWATCHER(io, io_ctx);
MANAPIHTTP_EV_UNWATCHER2(udp, udp_ctx);
MANAPIHTTP_EV_UNWATCHER(async, async_ctx);
MANAPIHTTP_EV_UNWATCHER(check, check_ctx);
MANAPIHTTP_EV_UNWATCHER(timer, timer_ctx);
MANAPIHTTP_EV_UNWATCHER(prepare, prepare_ctx);

MANAPIHTTP_EV_CANCEL (fs, fs_ctx);
MANAPIHTTP_EV_CANCEL (getaddrinfo, getaddrinfo_ctx);
MANAPIHTTP_EV_CANCEL (getnameinfo, getnameinfo_ctx)
MANAPIHTTP_EV_CANCEL (random, random_ctx);

void manapi::event_loop::stop_watcher_ptr(ev::connect *w) MANAPIHTTP_NOEXCEPT {
    if (w) {
        auto data_base = static_cast<ev::internal::connect_base_ctx *>(w->data());
        if (data_base) {
            if (data_base->type == ev::EV_TCP) {
                std::unique_ptr<ev::internal::connect_tcp_ctx> data (static_cast<ev::internal::connect_tcp_ctx *>(w->data()));
                w->data(nullptr);
            }
            else {
                assert(false && "ev::connect has invalid type");
            }
        }

        w->unbind();
    }
};

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::write *w) MANAPIHTTP_NOEXCEPT {
    if (w) {
        std::unique_ptr<ev::internal::write_ctx> data (static_cast<ev::internal::write_ctx *>(w->data()));
        w->data(nullptr);
        w->unbind();
        if (data) {
            data->s_.reset();
        }
    }
}

void manapi::event_loop::event_loop::stop_watcher_ptr(ev::udp_send *w) MANAPIHTTP_NOEXCEPT {
    if (w) {
        std::unique_ptr<ev::internal::udp_send_ctx> data {static_cast<ev::internal::udp_send_ctx *>(w->data())};
        w->data( nullptr);
        w->unbind();
        if (data) {
            data->s_.reset();
        }
    }
}
