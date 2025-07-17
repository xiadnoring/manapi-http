#pragma once

#include <uv.h>

namespace manapi::ev {
    void callback_watcher_tcp_connection_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

    void callback_watcher_udp_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

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

    void callback_watcher_connect_tcp (uv_connect_t *s, int status);

    void callback_watcher_write (uv_write_t *s, int status);

    void callback_watcher_fs (uv_fs_t *req);

    void callback_watcher_random (uv_random_t *s, int status, void *buf, size_t buflen);

    void callback_watcher_getaddrinfo (uv_getaddrinfo_t *req, int status, struct addrinfo *res);

    void callback_watcher_getnameinfo (uv_getnameinfo_t *req, int status, const char *hostname, const char *service);

    void callback_watcher_work (uv_work_t *req);

    void callback_watcher_after_work (uv_work_t *req, int status);

    void callback_close_cb (uv_handle_t *s);
}