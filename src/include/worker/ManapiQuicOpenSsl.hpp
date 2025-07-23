#pragma once

#include "ManapiUtils.hpp"
#include "worker/ManapiUdp.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#   include <openssl/ssl.h>
#   include <openssl/quic.h>

#   if OPENSSL_VERSION_NUMBER >= ((3<<28)|(2<<20)|(0<<4)|0x0L)

#       define MANAPIHTTP_OPENSSL_QUIC_SUPPORT

namespace manapi::net::worker {
    class openssl_quic : public udp {
    public:
        openssl_quic (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~openssl_quic() override;

        static std::shared_ptr<worker::openssl_quic> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        void onrecv(const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) override;

        error::status init(std::size_t deep) override;

        void stop(std::function<void()> cb) override;

        void close_connection(shared_conn conn, int flags) override;

        int event_flags(const shared_conn &conn) override;

        int event_flags(const shared_conn &conn, int flags) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;

        bool is_valid_connection(worker::connection *connection) override;

        bool is_writable(const shared_conn &conn) override;

        MANAPIHTTP_NODISCARD std::size_t recv_count(const shared_conn &conn) const override;

        bytebuffer recv_first_buffer(const shared_conn &conn) override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) override;

        void waiting(const shared_conn &conn, bool state) override;

        std::string_view alpn_ossltest ();

        void alpn_ossltest (std::string test);
    private:
        std::string alpn_ossltest_;
        void *ctx;
        void *listener;
    };
}
#   endif
#endif