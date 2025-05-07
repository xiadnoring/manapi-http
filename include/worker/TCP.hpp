#pragma once

#include "../ManapiUtils.hpp"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./ManapiAsync.hpp"
#include "../http/HTTPv1_1.hpp"
#include "../async/ManapiCancellation.hpp"

namespace manapi::net::worker {
    class TCP : public worker::base {
    public:
        struct connection_interface {
            manapi::timer t;
            std::shared_ptr<worker::base> worker;
            connection_stat_interface stats;
            int status = 0;
            std::shared_ptr<ev::tcp> watcher;
            std::unique_ptr<struct connection_io> top;
            std::unique_ptr<worker_watcher_cb> ev_callback;
        };

        TCP (net::site &site);

        ~TCP () override;

        bool is_valid_connection(worker::connection *connection) override;

        void init () override;

        void configure_connection (const shared_conn & connection, oncont_cb cb) override;

        void onaccept(std::shared_ptr<ev::tcp> &watcher, int status);

        virtual void onrecv (std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer);

        static std::shared_ptr<worker::TCP> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);

        virtual shared_conn accept (ev::shared_tcp &w, std::move_only_function<shared_conn()> init);

        virtual shared_conn accept (ev::shared_tcp &w);

        future<ssize_t> response(const shared_conn &connection, http::response *resp, bool finish) override;

        void close_connection(connection* conn, bool clean_disconnect) override;

        void stop() override;

        ssize_t sync_write(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish) override;

        std::unique_ptr<worker_watcher_cb> event_on(worker::connection *conn, std::unique_ptr<worker_watcher_cb> callback) override;

        int event_flags(worker::connection *conn, int flags) override;

        int event_flags(worker::connection *conn) override;

    protected:
        virtual void flush_write_ (const shared_conn &connection, bool flush = false);

        virtual bool recv_setup_connection (manapi::net::worker::connection *storage);

        void update_limit_rate ();

        void timeout_ (const worker::shared_conn& storage);

        void ev_watcher_stop_ (connection_interface & conn);

        virtual void update_limit_rate_connection (connection *conn);

        static void _connection_interface_eraser (connection_interface *connection);

        virtual void http_work_ (http::http_v1_1_t *http_v1_1_ctx, const worker::shared_conn &conn, int flags, ibuffpool_t buffer);

        virtual void onaccept_event_ (const worker::shared_conn &conn);

        std::map <std::uintptr_t, shared_conn> connections;
        ev::shared_tcp watcher_accept_;
    private:
        static std::string stringify_http_info (manapi::net::http::response *res, const int &version, const std::string &delimiter) ;
        static std::string stringify_headers (manapi::net::http::response *res, const std::string &delimiter) ;
        static void connection_interface_eraser (void *ptr);


        std::weak_ptr<base> self_;
        sockaddr_storage sockaddrin{};
        addrinfo *local;
        timer limit_rate_timer{};
        timeval recv_timeout{}, send_timeout{};
        int count;
    };
}
