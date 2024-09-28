#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"

manapi::net::config::config(const utils::json &config) {
    // =================[partial data min size  ]================= //
    if (config.contains("partial_data_min_size"))
    {
        partial_data_min_size = config["partial_data_min_size"].get <size_t> ();
    }

    // =================[socket block size      ]================= //
    if (config.contains("socket_block_size"))
    {
        socket_block_size = config["socket_block_size"].get <size_t> ();
    }

    // =================[http version           ]================= //
    if (config.contains("http_version")) {
        http_version_str = config["http_version"].get <std::string> ();

        if (http_version_str       == "0.9")    http_version = versions::HTTP_v0_9;
        else if (http_version_str  == "1.0")    http_version = versions::HTTP_v1_0;
        else if (http_version_str  == "1.1")    http_version = versions::HTTP_v1_1;
        else if (http_version_str  == "2")      http_version = versions::HTTP_v2;
        else if (http_version_str  == "3")      http_version = versions::HTTP_v3;
        else {
            http_version_str    = "1.1";
            http_version        = versions::HTTP_v1_1;

            MANAPI_LOG("http version '{}' is invalid in the config", http_version_str);
        }
    }

    // =================[port                   ]================= //
    if (config.contains("port"))
    {
        port = config["port"].get <std::string> ();
    }

    // =================[address                ]================= //
    if (config.contains("address"))
    {
        address = config["address"].get <std::string> ();
    }

    // =================[ssl                    ]================= //
    if (config.contains("ssl")) {
        ssl_config.enabled  = config["ssl"]["enabled"].get<bool>();
        ssl_config.key      = config["ssl"]["key"].get<std::string>();
        ssl_config.cert     = config["ssl"]["cert"].get<std::string>();
    }

    // =================[max_header_block_size  ]================= //
    if (config.contains("max_header_block_size"))
    {
        max_header_block_size = config["max_header_block_size"].get<size_t>();
    }

    // =================[keep_alive             ]================= //
    if (config.contains("keep_alive"))
    {
        keep_alive = config["keep_alive"].get<size_t>();
    }

    // =================[recv_timeout           ]================= //
    if (config.contains("recv_timeout"))
    {
        recv_timeout = config["recv_timeout"].get<size_t>();
    }

    // =================[send_timeout           ]================= //
    if (config.contains("send_timeout"))
    {
        send_timeout = config["send_timeout"].get<size_t>();
    }

    // =================[http_implement         ]================= //
    if (config.contains("http_implement"))
    {
        http_implement = config["http_implement"].get<std::string>();
    }

    // =================[tls_version            ]================= //
    if (config.contains("tls_version"))
    {
        const std::string &tls_version_string = config["tls_version"].get<std::string>();
        if (tls_version_string == "1" || tls_version_string == "1.0")
        {
            tls_version = versions::TLS_v1;
        }
        else if (tls_version_string == "1.1")
        {
            tls_version = versions::TLS_v1_1;
        }
        else if (tls_version_string == "1.2")
        {
            tls_version = versions::TLS_v1_2;
        }
        else if (tls_version_string == "1.3")
        {
            tls_version = versions::TLS_v1_3;
        }
        else {
            THROW_MANAPI_EXCEPTION("invalid tls_version in config: {}", tls_version_string);
        }
    }

    // =================[quic_cc_algo           ]================= //
    if (config.contains("quic_cc_algo"))
    {
        const std::string &quic_cc_algo_string = config["quic_cc_algo"].get<std::string>();
        if (quic_cc_algo_string == "CUBIC")
        {
            quic_cc_algo = versions::QUIC_CC_CUBIC;
        }
        else if (quic_cc_algo_string == "RENO")
        {
            quic_cc_algo = versions::QUIC_CC_RENO;
        }
        else if (quic_cc_algo_string == "BBR")
        {
            quic_cc_algo = versions::QUIC_CC_BBR;
        }
        else if (quic_cc_algo_string == "BBR2")
        {
            quic_cc_algo = versions::QUIC_CC_BBR2;
        }
        else if (quic_cc_algo_string == "NONE")
        {
            quic_cc_algo = versions::QUIC_CC_NONE;
        }
        else {
            THROW_MANAPI_EXCEPTION("invalid quic_cc_algo in config: {}", quic_cc_algo_string);
        }
    }

    // =================[quic_cc_algo           ]================= //
    if (config.contains("quic_debug"))
    {
        quic_debug = config["quic_debug"].get<bool>();
    }

    // =================[quic_implement         ]================= //
    if (config.contains("quic_implement"))
    {
        quic_implement = config["quic_implement"].get<std::string>();
    }
}

manapi::net::config::~config() = default;

// ======================[ configs funcs]==========================

void manapi::net::config::set_socket_block_size(const size_t &s) {
    socket_block_size = s;
}

const size_t &manapi::net::config::get_socket_block_size() const {
    return socket_block_size;
}

void manapi::net::config::set_max_header_block_size(const size_t &s) {
    max_header_block_size = s;
}

const size_t &manapi::net::config::get_max_header_block_size() const {
    return max_header_block_size;
}

const size_t &manapi::net::config::get_partial_data_min_size() const {
    return partial_data_min_size;
}

void manapi::net::config::set_http_version(const size_t &new_http_version) {
    http_version = new_http_version;
}

const size_t &manapi::net::config::get_http_version() const {
    return http_version;
}

void manapi::net::config::set_http_version_str(const std::string &new_http_version) {
    http_version_str = new_http_version;
}

const std::string &manapi::net::config::get_http_version_str() const {
    return http_version_str;
}

/**
 * keep alive in seconds
 * @param seconds
 */
void manapi::net::config::set_keep_alive(const long int &seconds) {
    keep_alive = seconds;
}

const size_t &manapi::net::config::get_keep_alive() const {
    return keep_alive;
}

const size_t & manapi::net::config::get_recv_timeout() const {
    return recv_timeout;
}

const size_t & manapi::net::config::get_send_timeout() const {
    return send_timeout;
}

void manapi::net::config::set_port(const std::string &_port) {
    port = _port;
}

const std::string &manapi::net::config::get_port() const {
    return port;
}

const std::string & manapi::net::config::get_http_implement() const {
    return http_implement;
}

const std::string & manapi::net::config::get_address() const {
    return address;
}

const std::string & manapi::net::config::get_quic_implement() const {
    return quic_implement;
}

const size_t & manapi::net::config::get_tls_version() const {
    return tls_version;
}

SSL_CTX * manapi::net::config::get_openssl_ctx() {
    return ctx;
}

void manapi::net::config::set_openssl_ctx(SSL_CTX *ctx) {
    this->ctx = ctx;
}

const bool & manapi::net::config::is_quic_debug() const {
    return quic_debug;
}

const size_t & manapi::net::config::get_quic_cc_algo() const {
    return quic_cc_algo;
}

const manapi::net::ssl_config_t &manapi::net::config::get_ssl_config() {
    return ssl_config;
}

void manapi::net::config::set_server_address(const sockaddr &addr) {
    server_addr = addr;
}

sockaddr & manapi::net::config::get_server_address() {
    return server_addr;
}

void manapi::net::config::set_server_len(const size_t &len) {
    server_len = len;
}

const socklen_t & manapi::net::config::get_server_len() const {
    return server_len;
}

void manapi::net::config::set_http3_config(quiche_h3_config *config) {
    http3_config = config;
}

quiche_h3_config * manapi::net::config::get_http3_config() {
    return http3_config;
}

void manapi::net::config::set_quic_config(quiche_config *config) {
    quic_config = config;
}

quiche_config * manapi::net::config::get_quic_config() {
    return quic_config;
}

void manapi::net::config::set_socket_fd(const int &fd) {
    sock_fd = fd;
}

const int & manapi::net::config::get_socket_fd() const {
    return sock_fd;
}

bool manapi::net::config::contains_compressor(const std::string &name) const {
    if (function_contains_compressor == nullptr) { THROW_MANAPI_EXCEPTION("function_contains_compressor = {}. We need to set function before call", "nullptr"); }
    return function_contains_compressor (name);
}

void manapi::net::config::set_function_contains_compressor(const std::function<bool(const std::string &name)> &func) {
    function_contains_compressor = func;
}
