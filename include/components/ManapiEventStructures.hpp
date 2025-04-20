#pragma once

#include "uv.h"

#ifdef __linux__
#   include "uv/linux.h"
#endif

#include "uv/errno.h"

#include "ManapiDebug.hpp"

#define MANAPI_EV_NODISCARD [[nodiscard]]
#define MANAPI_EV_NOEXPECT noexcept(true)
#define MANAPI_EV_CAST_STREAM(x) reinterpret_cast<uv_stream_t *> (x)
#define MANAPI_EV_CAST_HANDLE(x) reinterpret_cast <uv_handle_t *> (x)
#define MANAPI_EV_DEFAULT_PRIVATE_VAR(name_class, name_struct)
#define MANAPI_EV_DEFAULT(name_class, name_struct) \
        void unbind (uv_close_cb cb) MANAPI_EV_NOEXPECT;\
        void unbind () MANAPI_EV_NOEXPECT;\
        void data (void *data) MANAPI_EV_NOEXPECT;\
        void *data () MANAPI_EV_NOEXPECT; \
        loop_ref loop () MANAPI_EV_NOEXPECT; \
        name_struct* custom () MANAPI_EV_NOEXPECT; \
        bool is_active () MANAPI_EV_NOEXPECT; \
        ~name_class ();
#define MANAPI_EV_STREAM(name_class, name_struct) \
        int listen (int backlog, uv_connection_cb cb) MANAPI_EV_NOEXPECT; \
        static int ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPI_EV_NOEXPECT; \
        static int ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPI_EV_NOEXPECT;
#define MANAPI_EV_CHECK(expr) if (expr) { THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_EXTERNAL_LIB_CRASH, #expr); }

namespace manapi {
    typedef uv_os_fd_t fd_t;
    typedef uv_os_sock_t socket_t;
}

namespace manapi::ev {
    enum types {
        EV_TIMER = 0,
        EV_WRITE,
        EV_IO,
        EV_TCP,
        EV_UDP,
        EV_PREPARE,
        EV_CHECK,
        EV_IDLE,
        EV_ASYNC
    };
    enum flags {
        READ = UV_READABLE,
        WRITE = UV_WRITABLE,
        DISCONNECT = UV_DISCONNECT,
        PRIORITIZED = UV_PRIORITIZED
    };

    typedef uv_loop_t *loop_ref;

    void callback_watcher_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    void callback_watcher_async (uv_async_t *s);
    void callback_watcher_timer (uv_timer_t *s);
    void callback_watcher_io (uv_poll_t *s, int status, int revents);
    void callback_watcher_idle (uv_idle_t *s);
    void callback_watcher_check (uv_check_t *s);
    void callback_watcher_prepare (uv_prepare_t *s);
    void callback_watcher_tcp_accept (uv_tcp_t *s, int status);
    void callback_watcher_tcp_read (uv_stream_t *s,  ssize_t nread, const uv_buf_t *buf);
    void callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags);
    void callback_watcher_udp_send (uv_udp_send_t *s, int status);
    void callback_watcher_write (uv_write_t *s, int status);

    class async {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(async, uv_async_t)
    public:
        MANAPI_EV_DEFAULT(async, uv_async_t)

        async (loop_ref loop);
        async (loop_ref loop, uv_async_cb cb);

        int send () MANAPI_EV_NOEXPECT;

        void set (uv_async_cb cb = callback_watcher_async) MANAPI_EV_NOEXPECT;
    private:

        uv_async_t s_;
    };

    class idle {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(idle, uv_idle_t)
    public:
        MANAPI_EV_DEFAULT(idle, uv_idle_t)
        idle (loop_ref loop);

        int init (loop_ref loop) MANAPI_EV_NOEXPECT;

        int start () MANAPI_EV_NOEXPECT;
        int start (uv_idle_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_idle_t s_;
    };

    class check {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(check, uv_check_t)
    public:
        MANAPI_EV_DEFAULT(check, uv_check_t)
        check (loop_ref loop);

        int init (loop_ref loop) MANAPI_EV_NOEXPECT;
        int start () MANAPI_EV_NOEXPECT;
        int start (uv_check_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_check_t s_;
    };

    class io {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(io, uv_poll_t)
    public:
        MANAPI_EV_DEFAULT(io, uv_poll_t)

        template<typename T1 = manapi::fd_t>
        requires(sizeof (manapi::fd_t) != sizeof (manapi::socket_t))
        io (loop_ref loop, manapi::socket_t fd) : s_() {
            MANAPI_EV_CHECK(uv_poll_init_socket(loop, &this->s_, fd));
        }

        template<typename T1 = manapi::fd_t>
        requires(sizeof (manapi::fd_t) != sizeof (manapi::socket_t))
        io (loop_ref loop, T1 fd) : s_() {
            MANAPI_EV_CHECK(uv_poll_init(loop, &this->s_, fd));
        }

        template<typename T1 = manapi::fd_t>
        requires(sizeof (manapi::fd_t) == sizeof (manapi::socket_t))
        io (loop_ref loop, T1 fd) : s_() {
            MANAPI_EV_CHECK(uv_poll_init_socket(loop, &this->s_, fd));
        }

        int start (int revents, uv_poll_cb cb) MANAPI_EV_NOEXPECT;
        int start (int revents) MANAPI_EV_NOEXPECT;
        int start () MANAPI_EV_NOEXPECT;
        int restart (int revents) MANAPI_EV_NOEXPECT;
        int stop () MANAPI_EV_NOEXPECT;
        int events () MANAPI_EV_NOEXPECT;
    private:
        uv_poll_t s_;
    };

    class write {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(write, uv_write_t)
    public:
        MANAPI_EV_DEFAULT(write, uv_write_t)

        write (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb);
        write (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs);
    private:
        uv_write_t s_{};
    };

    class tcp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(tcp, uv_tcp_t)
    public:
        MANAPI_EV_DEFAULT(tcp, uv_tcp_t)
        MANAPI_EV_STREAM(tcp, uv_tcp_t)

        tcp (loop_ref loop);

        int accept (tcp *parent) MANAPI_EV_NOEXPECT;

        int read_start () MANAPI_EV_NOEXPECT;
        int read_start (uv_alloc_cb alloc, uv_read_cb cb) MANAPI_EV_NOEXPECT;

        int read_stop () MANAPI_EV_NOEXPECT;

        int bind (sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;
    private:
        uv_tcp_t s_;
    };

    class udp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp, uv_udp_t)
    public:
        MANAPI_EV_DEFAULT(udp, uv_udp_t)
        MANAPI_EV_STREAM(udp, uv_udp_t)

        udp (loop_ref loop);

        int bind (sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;

        int recv_start () MANAPI_EV_NOEXPECT;
        int recv_start (uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPI_EV_NOEXPECT;

        int recv_stop () MANAPI_EV_NOEXPECT;

        int try_send (const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPI_EV_NOEXPECT;

        int try_send (int count, uv_buf_t **buf, uint32_t *nbuf, sockaddr **addr, int flags) MANAPI_EV_NOEXPECT;
    private:
        uv_udp_t s_;
    };

    class udp_send {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp_send, uv_udp_send_t)
    public:
        MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)

        udp_send (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb, const sockaddr *addr);
        udp_send (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr);
    private:
        uv_udp_send_t s_;
    };

    class prepare {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(prepare, uv_prepare_t)
    public:
        MANAPI_EV_DEFAULT(prepare, uv_prepare_t)

        prepare (loop_ref loop);

        int start () MANAPI_EV_NOEXPECT;
        int start (uv_prepare_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_prepare_t s_;
    };

    class timer {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(timer, uv_timer_t)
    public:
        MANAPI_EV_DEFAULT(timer, uv_timer_t)

        timer (loop_ref loop);

        int start (uint64_t timeout, uint64_t repeat) MANAPI_EV_NOEXPECT;
        int start (uint64_t timeout, uint64_t repeat, uv_timer_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;

        int again () MANAPI_EV_NOEXPECT;

        void repeat (uint64_t repeat)  MANAPI_EV_NOEXPECT;

        MANAPI_EV_NODISCARD uint64_t repeat () const MANAPI_EV_NOEXPECT;

        MANAPI_EV_NODISCARD uint64_t due_in () const MANAPI_EV_NOEXPECT;
    private:
        uv_timer_t s_;
    };
}

#undef MANAPI_EV_CAST_HANDLE
#undef MANAPI_EV_DEFAULT_PRIVATE_VAR
#undef MANAPI_EV_STREAM
#undef MANAPI_EV_CHECK
#undef MANAPI_EV_DEFAULT
#undef MANAPI_EV_CAST_STREAM