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
    class TCP : public worker::interface_worker {
    public:
        struct connection_interface : base::connection_base_t {
            manapi::timer t;
            worker::base *worker;
            int status = 0;
            std::shared_ptr<ev::tcp> watcher;
            std::unique_ptr<struct connection_io> top;
            std::unique_ptr<worker_watcher_cb> ev_callback;
            int speed_min_delay;
        };

        enum connection_status {
            CONN_KEEP_ALIVE     = 256,
            CONN_LIMIT_RATE     = 512,
        };

        TCP (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~TCP () override;

        bool is_valid_connection(worker::connection *connection) override;

        void init () override;

        void waiting(const shared_conn &conn, bool state) override;

        void configure_connection (const shared_conn & connection, oncont_cb cb) override;

        void onaccept(const std::shared_ptr<ev::tcp> &watcher, int status);

        virtual void onrecv (const std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer);

        static std::shared_ptr<worker::TCP> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        virtual shared_conn accept (const ev::shared_tcp &w, std::move_only_function<shared_conn()> init);

        virtual shared_conn accept (const ev::shared_tcp &w);

        void close_connection(shared_conn conn, int flags) override;

        void stop(std::function<void()> cb) override;

        void feed_event (const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;

        ssize_t sync_write_ex(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) override;

        ssize_t sync_write(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) override;

        int event_flags(const shared_conn & conn, int flags) override;

        int event_flags(const shared_conn & conn) override;

        std::size_t recv_count(const shared_conn &conn) const override;

        bytebuffer recv_first_buffer(const shared_conn &conn) override;
    protected:
        virtual int flush_write_ (const shared_conn &connection, bool flush = false);

        void flush_read_ (const shared_conn &conn, connection_interface *data);

        void update_limit_rate ();

        void timeout_ (shared_conn conn);

        virtual void update_limit_rate_connection (const shared_conn &sconn);

        bool is_writable(const shared_conn &conn) override;

        virtual int onaccept_event_ (const worker::shared_conn &conn);

        ev::shared_tcp watcher_accept_;
    protected:
        std::size_t count;

        int flags;

        conns_by_ip ips;

        std::function<void()> finish;
    private:

        static void connection_interface_eraser (worker::connection *ptr);

        sockaddr_storage sockaddrin{};

        addrinfo *local;

        timer limit_rate_timer{};

        timeval recv_timeout{}, send_timeout{};
    };
}
