#pragma once

#include "../ManapiUtils.hpp"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./ManapiAsync.hpp"
#include "../http/HTTPv2.hpp"
#include "../http/HTTPv1_1.hpp"
#include "../async/ManapiCancellation.hpp"

namespace manapi::net::worker {
    class TCP : public worker::base {
    public:
        struct connection_interface : base::connection_base_t {
            manapi::timer t;
            std::shared_ptr<worker::base> worker;
            int status = 0;
            std::shared_ptr<ev::tcp> watcher;
            std::unique_ptr<struct connection_io> top;
            std::unique_ptr<worker_watcher_cb> ev_callback;
            int speed_min_delay;
            void *data;
        };

        TCP (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config);

        ~TCP () override;

        bool is_valid_connection(worker::connection *connection) override;

        void init () override;

        void configure_connection (const shared_conn & connection, oncont_cb cb) override;

        void onaccept(std::shared_ptr<ev::tcp> &watcher, int status);

        virtual void onrecv (std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer);

        static std::shared_ptr<worker::TCP> create (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        virtual shared_conn accept (ev::shared_tcp &w, std::move_only_function<shared_conn()> init);

        virtual shared_conn accept (ev::shared_tcp &w);

        future<ssize_t> response(const shared_conn &connection, http::response *resp, bool finish) override;

        void close_connection(shared_conn conn, bool clean_disconnect) override;

        void stop() override;

        void feed_event (const shared_conn &conn, int flags, const char *buff, ssize_t size) override;

        ssize_t sync_write_ex(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt);

        ssize_t sync_write(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) override;

        int event_flags(const shared_conn & conn, int flags) override;

        int event_flags(const shared_conn & conn) override;

    protected:
        virtual void flush_write_ (const shared_conn &connection, bool flush = false);

        void update_limit_rate ();

        void timeout_ (shared_conn conn);

        void ev_watcher_stop_ (connection_interface & conn);

        virtual bool update_limit_rate_connection (const shared_conn &sconn);

        virtual void http2_work_ (http::http_v2_t *http_v2_ctx, const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize);

        bool is_writable(const shared_conn &conn) override;

        virtual void http_work_ (http::http_v1_1_t *http_v1_1_ctx, const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize);

        virtual void onaccept_event_ (const worker::shared_conn &conn);

        std::unordered_map <std::uintptr_t, shared_conn> connections;
        ev::shared_tcp watcher_accept_;
    protected:
        void conn_work_finish_ (worker::shared_conn conn, bool ok, ibuffpool_t buffer = {});

        std::weak_ptr<base> self_;
        std::shared_ptr<net::worker::http_v2> http_v2_worker;
    private:
        static std::string stringify_http_info (manapi::net::http::response *res, const int &version, const std::string &delimiter) ;
        static std::string stringify_headers (manapi::net::http::response *res, const std::string &delimiter) ;
        static void connection_interface_eraser (void *ptr);

        sockaddr_storage sockaddrin{};
        addrinfo *local;
        timer limit_rate_timer{};
        timeval recv_timeout{}, send_timeout{};
    };
}
