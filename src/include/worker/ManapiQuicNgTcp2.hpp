#pragma once

#include "../ManapiUtils.hpp"
#include "worker/ManapiUdp.hpp"

#if MANAPIHTTP_NGHTTP3_DEPENDENCY

// namespace manapi::net::worker {
//     class quic_ngtcp2 : public worker::udp {
//     public:
//         explicit quic_ngtcp2(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata,manapi::net::http::config * config);
//
//         ~quic_ngtcp2() override;
//
//         static std::shared_ptr<worker::quic_ngtcp2> create (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config);
//
//         void init() override;
//
//         void stop(std::function<void()> cb) override;
//
//         void onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) override;
//
//         void close_connection(shared_conn conn, bool clean_disconnect) override;
//
//         void configure_connection(const shared_conn &conn, oncont_cb cb) override;
//
//         int event_flags(const shared_conn &conn, int flags) override;
//
//         int event_flags(const shared_conn &conn) override;
//
//         std::unique_ptr<worker_watcher_cb> event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) override;
//
//         void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;
/
//
//         bool is_writable(const shared_conn &conn) override;
//
//         ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) override;
//
//         ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) override;
//
//         void waiting(const shared_conn &conn, bool state) override;
//
//     private:
//     };
// }

#endif