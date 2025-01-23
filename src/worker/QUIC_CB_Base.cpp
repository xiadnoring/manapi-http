#include "worker/QUIC_CB_Base.hpp"

manapi::net::worker::quic_cb_base::quic_cb_base(net::site &site) {

}

manapi::net::worker::quic_cb_base::~quic_cb_base() = default;

ssize_t manapi::net::worker::quic_cb_base::send(void *buf, ssize_t buflen) {
    return 0;
}

void manapi::net::worker::quic_cb_base::set_connection(std::weak_ptr <worker::connection> connection) {
    this->connection = std::move(connection);
}

void manapi::net::worker::quic_cb_base::global_init(net::site &site) {
}

void manapi::net::worker::quic_cb_base::global_deinit(net::site &site) {
}

manapi::future<void> manapi::net::worker::quic_cb_base::client_init(quic_frame_data_t frame, std::string &server_hello, std::string &server_handshake) {
    co_return;
}

manapi::future<void> manapi::net::worker::quic_cb_base::client_init_ack(quic_frame_data_t frame) {
    co_return;
}

manapi::future<void> manapi::net::worker::quic_cb_base::client_handshake(quic_frame_data_t frame) {
    co_return;
}

manapi::future<void> manapi::net::worker::quic_cb_base::client_handshake_finished(quic_frame_data_t frame, std::string &server_handshake_finished) {
    co_return;
}

manapi::future<void> manapi::net::worker::quic_cb_base::client_application(quic_frame_data_t frame) {
    co_return;
}
