#pragma once

#include "./ManapiInterfaceWorker.hpp"
#include "./ManapiBaseWorker.hpp"

#define MANAPIHTTP_WORKER_EVENT_LOOP(n__) n__->speed_min_delay = static_cast<int>(this->config()->speed_check_delay); \
auto const prev = std::exchange(n__->flags, ((n__->flags >> 2) << 2) | (flags & CONN_MASK_UPDATE)); \
if (n__->flags & CONN_EVENT_LOCKED) return prev; \
n__->flags |= CONN_EVENT_LOCKED; auto status = n__->flags; \
while (true)
#define MANAPIHTTP_WORKER_EVENT_BREAK(n__) if (status != n__->flags) { status = n__->flags; continue; }\
assert (n__->flags & CONN_EVENT_LOCKED); n__->flags ^= CONN_EVENT_LOCKED;  break;

namespace manapi::net::worker {
    struct connection_base_t {
        ssize_t transfered;
        ssize_t transfered_k;
    };

    struct connection_prepared_base_t : connection_base_t {
        std::unique_ptr<worker_watcher_cb> ev_callback;
        int flags;
        int speed_min_delay;
    };

    struct connection_prepared_t : connection_prepared_base_t {
        std::unique_ptr<struct connection_io> top;
    };

    struct tcp_connection_t : connection_prepared_t {
        manapi::timer t;
        worker::base *worker;
        std::shared_ptr<ev::tcp> watcher;
    };

    struct tls_connection_t : tcp_connection_t {
        void *ssl;
        manapi::timer accept_timer;
        void *rbio;
        void *wbio;
    };

    namespace prepared {
        int event_flags (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        int event_flags (const shared_conn &conn, connection_prepared_base_t *data) MANAPIHTTP_NOEXCEPT;

        manapi::bytebuffer recv_first_buffer (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        manapi::bytebuffer recv_first_buffer (const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT;

        inline ssize_t sync_write(interface_worker *w, const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
            auto const connection = conn->as<connection_prepared_base_t>();
            auto const config = w->config();
            ssize_t const limit_size = config->speed_limit_rate - connection->transfered;

            auto const size = base::buffs_cut_by_size (buff, nbuff, limit_size, finish);

            if (!size)
                return 0;

            return w->sync_write_ex(conn, buff, nbuff, size, finish, config->max_buffer_stack);
        }

        void waiting(const shared_conn &conn, connection_prepared_base_t *data, bool state) MANAPIHTTP_NOEXCEPT;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT;

        void flush_read_ (worker::base *w, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT;

        int flush_read2_ (http::config *config, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT;

        bool is_writable (http::config *config, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT;

        std::size_t recv_count (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        std::size_t recv_count (const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT;

        void top_buffer_clear (connection_prepared_t *s) MANAPIHTTP_NOEXCEPT;

        void event_callback_clear (const shared_conn &conn, connection_prepared_base_t *s) MANAPIHTTP_NOEXCEPT;

        void timer_clear (manapi::timer t) MANAPIHTTP_NOEXCEPT;

        void feed_event (worker::base *w, const shared_conn &conn, connection_prepared_t *data, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT;

        void feed_event (worker::base *w, const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT;

        void update_limit_rate_connection (const shared_conn &sconn, connection_prepared_base_t *data, worker::base *w, http::config *config, wrk_interface_global_t *global) MANAPIHTTP_NOEXCEPT;

        void update_limit_rate_connection (const shared_conn &sconn, worker::base *w, http::config *config, wrk_interface_global_t *global) MANAPIHTTP_NOEXCEPT;

        std::unique_ptr<manapi::net::worker::worker_watcher_cb> event_on (const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXCEPT;
    }
}