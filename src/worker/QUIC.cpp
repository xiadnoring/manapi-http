#include <fcntl.h>
#include "worker/QUIC.hpp"

manapi::net::worker::QUIC::QUIC(net::site &site) : base(site) {
    this->buffer.resize(1350);
}

manapi::net::worker::QUIC::~QUIC() = default;

void manapi::net::worker::QUIC::init() {

    hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    if (getaddrinfo(config->get_address().data(), config->get_port().data(), &hints, &local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    config->set_server_address(*local->ai_addr);
    config->set_server_len(local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP QUIC PORT USED: {}. https://{}:{}", config->get_port(), config->get_address(), config->get_port());

    // for quic
    config->set_socket_fd(socket (local->ai_family, SOCK_DGRAM, 0));

    if (config->get_socket_fd() < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }

    setsockopt(config->get_socket_fd(), SOL_SOCKET, SO_REUSEADDR, &reuseaddr_param, sizeof(int));

    if (fcntl(config->get_socket_fd(), F_SETFL, O_NONBLOCK) != 0) {
       THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "Failed to make socket {} non-blocking", config->get_socket_fd());
    }

    if (bind(config->get_socket_fd(), local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", config->get_port());
    }
}

void manapi::net::worker::QUIC::onrecv(const std::shared_ptr<worker::base> & worker) {
    while (true) {
        auto rhs = recv(config->get_socket_fd(), buffer.data(), buffer.size(), 0);
        if (rhs < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }

            MANAPIHTTP_LOG("failed to read: recvfrom(...) = {}", rhs);
            return;
        }

        this->_parse_frame(rhs);
    }
}

std::shared_ptr<manapi::net::worker::QUIC> manapi::net::worker::QUIC::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::QUIC>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::string manapi::net::worker::QUIC::_parse_string(ssize_t &i, const ssize_t &len, const ssize_t &size) {
    if (len + i >= size) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "No enough left space");
    }
    i += len;
    return std::move(std::string(buffer.data() + i, len));
}

void manapi::net::worker::QUIC::_parse_frame(ssize_t &size) {
    ssize_t i = 0;
    auto hb = _parse_number<uint8_t>(i, size); // Header Byte
    auto quic_version = _parse_number<uint32_t>(i, size);

    auto des_id_len = _parse_number<uint8_t>(i, size);
    auto des_id = _parse_string(i, des_id_len, size);

    auto src_id_len = _parse_number<uint8_t>(i, size);
    auto src_id = _parse_string(i, src_id_len, size);

    auto token_id_len = _parse_number<uint8_t>(i, size);
    auto token_id = _parse_string(i, token_id_len, size);

    auto len = _parse_number<uint16_t>(i, size);

    auto pnum = _parse_number<uint8_t>(i, size); // Packet Number

    auto a = 0;
}
