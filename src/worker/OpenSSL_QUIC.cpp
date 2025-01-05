#include "worker/OpenSSL_QUIC.hpp"

#ifdef MANAPI_OPENSSL_QUIC_REALIZATION

#include "worker/tools/OpenSSLTools.hpp"

manapi::net::worker::openssl_quic::openssl_quic(net::site &site) : worker::udp(site) {
    tools::ssl_library_init();
}

manapi::net::worker::openssl_quic::~openssl_quic() {

}

void manapi::net::worker::openssl_quic::init() {
    udp::init();

    this->ctx = SSL_CTX_new(OSSL_QUIC_client_method());
}

std::shared_ptr<manapi::net::worker::openssl_quic> manapi::net::worker::openssl_quic::create(net::site &site,
    std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::openssl_quic>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

void manapi::net::worker::openssl_quic::onrecv(ev::io &watcher, int revents) {
    if (revents & EV_READ) {

    }

    if (revents & EV_WRITE) {

    }
}


#endif