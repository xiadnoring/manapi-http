// #include "../include/worker/ManapiQuicNgTcp2.hpp"
//
//
// #if MANAPIHTTP_NGHTTP3_DEPENDENCY
//
// #include <nghttp3/nghttp3.h>
// #include <nghttp3/version.h>
//
// manapi::net::worker::quic_ngtcp2::quic_ngtcp2(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config) : udp(std::move(site), std::move(wdata), config) {
// }
//
// manapi::net::worker::quic_ngtcp2::~quic_ngtcp2() {
//
// }
//
// std::shared_ptr<manapi::net::worker::quic_ngtcp2> manapi::net::worker::quic_ngtcp2::create(net::http::site site,
//     std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
//     auto worker = std::make_shared<worker::quic_ngtcp2>(std::move(site), std::move(wdata), config.get());
//     worker->self_ = std::weak_ptr (worker);
//     return std::move(worker);
// }
//
// void manapi::net::worker::quic_ngtcp2::init() {
//     udp::init();
//
// }
//
// void manapi::net::worker::quic_ngtcp2::stop(std::function<void()> cb) {
//     udp::stop(cb);
// }
//
// void manapi::net::worker::quic_ngtcp2::onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) {
//
// }
//
// void manapi::net::worker::quic_ngtcp2::close_connection(shared_conn conn, bool clean_disconnect) {
// }
//
// void manapi::net::worker::quic_ngtcp2::configure_connection(const shared_conn &conn, oncont_cb cb) {
// }
//
// int manapi::net::worker::quic_ngtcp2::event_flags(const shared_conn &conn, int flags) {
// }
//
// int manapi::net::worker::quic_ngtcp2::event_flags(const shared_conn &conn) {
// }
//
// std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::quic_ngtcp2::event_on(
//     const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) {
// }
//
// void manapi::net::worker::quic_ngtcp2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size,
//     ibuffpool_t *p) {
// }
//
//
// #endif
