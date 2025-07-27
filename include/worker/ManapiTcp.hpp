#pragma once

#include "../ManapiUtils.hpp"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./ManapiBaseWorker.hpp"
#include "./ManapiInterfaceWorker.hpp"
#include "./ManapiAsync.hpp"
#include "../http/ManapiHttp2.hpp"
#include "../http/ManapiHttp1.hpp"
#include "../async/ManapiCancellation.hpp"

namespace manapi::net::worker {
    struct tcp_connection_t;

    class TCP : public worker::interface_worker {
    public:

        enum connection_status {
            CONN_TCP_RESERVED     = 256,
            CONN_LIMIT_RATE     = 512,
        };

        TCP (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~TCP () override;

        manapi::future<error::status> init (std::size_t deep) override;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXPECT override;

        void onaccept(const std::shared_ptr<ev::tcp> &watcher, int status) MANAPIHTTP_NOEXPECT;

        virtual void onrecv (const std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer) MANAPIHTTP_NOEXPECT;

        static std::shared_ptr<worker::TCP> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        virtual shared_conn accept (const ev::shared_tcp &w, std::move_only_function<shared_conn()> init) MANAPIHTTP_NOEXPECT;

        virtual shared_conn accept (const ev::shared_tcp &w) MANAPIHTTP_NOEXPECT;

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXPECT override;

        void stop(std::function<void()> cb) override;

        void feed_event (const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXPECT override;

        ssize_t sync_write_ex(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXPECT override;

        ssize_t sync_write(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXPECT override;

        int event_flags(const shared_conn & conn, int flags) MANAPIHTTP_NOEXPECT override;

        int event_flags(const shared_conn & conn) MANAPIHTTP_NOEXPECT override;

        [[nodiscard]] std::size_t recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXPECT override;

        bytebuffer recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXPECT override ;
    protected:
        virtual void read_start_ (tcp_connection_t *data) MANAPIHTTP_NOEXPECT;

        virtual void read_stop_ (tcp_connection_t *data) MANAPIHTTP_NOEXPECT;

        virtual int flush_write_ (const shared_conn &connection, bool flush = false) MANAPIHTTP_NOEXPECT;

        void flush_read_ (const shared_conn &conn, tcp_connection_t *data) MANAPIHTTP_NOEXPECT;

        void update_limit_rate () MANAPIHTTP_NOEXPECT;

        void timeout_ (shared_conn conn) MANAPIHTTP_NOEXPECT;

        virtual void update_limit_rate_connection (const shared_conn &sconn) MANAPIHTTP_NOEXPECT;

        bool is_writable(const shared_conn &conn) MANAPIHTTP_NOEXPECT override;

        virtual int onaccept_event_ (const worker::shared_conn &conn) MANAPIHTTP_NOEXPECT;

        ev::shared_tcp watcher_accept_;
    protected:
        std::size_t count;

        int flags;

        conns_by_ip ips;

        std::function<void()> finish;
    private:

        static void connection_interface_eraser (worker::connection *ptr) MANAPIHTTP_NOEXPECT;

        sockaddr_storage sockaddrin{};

        addrinfo *local;

        timer limit_rate_timer{};

        timeval recv_timeout{}, send_timeout{};
    };
}
