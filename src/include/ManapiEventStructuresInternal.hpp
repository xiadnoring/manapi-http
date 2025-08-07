#pragma once

#include <uv.h>
#include "components/ManapiTimerObject.hpp"

namespace manapi {
    struct timer::timer_data_t {
        int flags{0};
        std::chrono::milliseconds delay;
        std::chrono::steady_clock::time_point point;

        union timer_data_cb_t {
            async_cb_t async_cb;
            sync_cb_t sync_cb;

            ~timer_data_cb_t ();
        } cb{};

        ~timer_data_t();
    };

    enum timer_tasks_flags {
        TIMER_TASK_ENABLED = 1,
        TIMER_TASK_INTERVAL = 2,
        TIMER_TASK_ACTIVE = 4,
        TIMER_TASK_IS_ASYNC = 8
    };
}

namespace manapi::ev {
    void callback_watcher_tcp_connection_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_udp_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_async (uv_async_t *s) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_timer (uv_timer_t *s) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_io (uv_poll_t *s, int status, int revents) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_idle (uv_idle_t *s) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_check (uv_check_t *s) MANAPIHTTP_NOEXCEPT ;

    void callback_watcher_prepare (uv_prepare_t *s) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_tcp_accept (uv_tcp_t *s, int status) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_tcp_read (uv_stream_t *s,  ssize_t nread, const uv_buf_t *buf) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_udp_send (uv_udp_send_t *s, int status) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_connect_tcp (uv_connect_t *s, int status) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_write (uv_write_t *s, int status) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_fs (uv_fs_t *req) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_random (uv_random_t *s, int status, void *buf, size_t buflen) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_getaddrinfo (uv_getaddrinfo_t *req, int status, struct addrinfo *res) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_getnameinfo (uv_getnameinfo_t *req, int status, const char *hostname, const char *service) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_work (uv_work_t *req) MANAPIHTTP_NOEXCEPT;

    void callback_watcher_after_work (uv_work_t *req, int status) MANAPIHTTP_NOEXCEPT;

    void callback_close_cb (uv_handle_t *s) MANAPIHTTP_NOEXCEPT;
}