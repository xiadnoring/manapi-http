#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"

const std::map <manapi::net::http::versions::http , std::string> http_version_to_print = {
    {manapi::net::http::versions::HTTP_v0_9, "0.9"},
    {manapi::net::http::versions::HTTP_v1_0, "1.0"},
    {manapi::net::http::versions::HTTP_v1_1, "1.1"},
    {manapi::net::http::versions::HTTP_v2, "2"},
    {manapi::net::http::versions::HTTP_v3, "3"},
};

const std::map <std::string, manapi::net::http::versions::http> http_version_to_parse = {
    {"0.9", manapi::net::http::versions::HTTP_v0_9},
    {"1.0", manapi::net::http::versions::HTTP_v1_0},
    {"1.1", manapi::net::http::versions::HTTP_v1_1},
    {"2", manapi::net::http::versions::HTTP_v2},
    {"2.0", manapi::net::http::versions::HTTP_v2},
    {"3", manapi::net::http::versions::HTTP_v3},
    {"3.0", manapi::net::http::versions::HTTP_v3}
};

manapi::net::http::config::config(const json &config) {
    /* partial data min size */
    if (config.contains("partial_data_min_size"))
    {
        this->partial_data_min_size.store(config["partial_data_min_size"].as_integer());
    }

    /* socket block size */
    if (config.contains("socket_block_size"))
    {
        this->socket_block_size.store(config["socket_block_size"].as_integer());
    }

    /* http version */
    if (config.contains("http_version")) {
        this->http_version_str = config["http_version"].as_string();

        if (this->http_version_str       == "0.9")    this->http_version.store(versions::HTTP_v0_9);
        else if (this->http_version_str  == "1.0")    this->http_version.store(versions::HTTP_v1_0);
        else if (this->http_version_str  == "1.1")    this->http_version.store(versions::HTTP_v1_1);
        else if (this->http_version_str  == "2")      this->http_version.store(versions::HTTP_v2);
        else if (this->http_version_str  == "3")      this->http_version.store(versions::HTTP_v3);
        else {
            this->http_version_str = "1.1";
            this->http_version.store(versions::HTTP_v1_1);

            MANAPIHTTP_LOG("http version '{}' is invalid in the config", *http_version_str.get());
        }
    }

    /* port */
    if (config.contains("port")){
        this->port = config["port"].as_string();
    }

    /* address */
    if (config.contains("address")){
        this->address = config["address"].as_string();
    }

    /* speed limit rate */
    if (config.contains("speed_limit_rate")) {
        this->speed_limit_rate_ = config["speed_limit_rate"].as_integer();
    }

    /* ssl */
    if (config.contains("ssl")) {
        this->ssl_config = {
            .enabled  = config["ssl"]["enabled"].as_bool(),
            .key      = config["ssl"]["key"].as_string(),
            .cert     = config["ssl"]["cert"].as_string()
        };
    }

    /* max_header_block_size */
    if (config.contains("max_header_block_size"))
    {
        this->max_header_block_size.store(config["max_header_block_size"].as_integer());
    }

    /* buffer_size */
    if (config.contains("buffer_size")) {
        this->buffer_size_.store(config["buffer_size"].as_integer());
    }

    /* max_backlog */
    if (config.contains("max_backlog")) {
        this->max_backlog_.store(config["max_backlog"].as_integer());
    }

    /* keep_alive */
    if (config.contains("keep_alive"))
    {
        this->keep_alive.store(config["keep_alive"].as_integer());
    }

    /* recv_timeout */
    if (config.contains("recv_timeout"))
    {
        this->recv_timeout.store(config["recv_timeout"].as_integer());
    }

    /* send_timeout */
    if (config.contains("send_timeout"))
    {
        this->send_timeout.store(config["send_timeout"].as_integer());
    }

    /* implementation */
    if (config.contains("implementation"))
    {
        this->implementation = config["implementation"].as_string();
    }

    /* transport */
    if (config.contains("transport"))
    {
        this->transport = config["transport"].as_string();
    }

    /* tls version */
    if (config.contains("tls_version"))
    {
        const std::string &tls_version_string = config["tls_version"].as_string();
        if (tls_version_string == "1" || tls_version_string == "1.0")
        {
            this->tls_version.store(versions::TLS_v1);
        }
        else if (tls_version_string == "1.1")
        {
            this->tls_version.store(versions::TLS_v1_1);
        }
        else if (tls_version_string == "1.2")
        {
            this->tls_version.store(versions::TLS_v1_2);
        }
        else if (tls_version_string == "1.3")
        {
            this->tls_version.store(versions::TLS_v1_3);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid tls_version in config: {}", tls_version_string);
        }
    }

    /* quic cc algo */
    if (config.contains("quic_cc_algo"))
    {
        const std::string &quic_cc_algo_string = config["quic_cc_algo"].as_string();
        if (quic_cc_algo_string == "CUBIC")
        {
            this->quic_cc_algo.store(versions::QUIC_CC_CUBIC);
        }
        else if (quic_cc_algo_string == "RENO")
        {
            this->quic_cc_algo.store(versions::QUIC_CC_RENO);
        }
        else if (quic_cc_algo_string == "BBR")
        {
            this->quic_cc_algo.store(versions::QUIC_CC_BBR);
        }
        else if (quic_cc_algo_string == "BBR2")
        {
            this->quic_cc_algo.store(versions::QUIC_CC_BBR2);
        }
        else if (quic_cc_algo_string == "NONE")
        {
            this->quic_cc_algo.store(versions::QUIC_CC_NONE);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo param in the config: {}", quic_cc_algo_string);
        }
    }

    /* max connections */
    if (config.contains("max_connections")) {
        this->max_connections_.store(config["max_connections"].as_integer());
    }

    /* max_rst_cnt */
    if (config.contains("max_rst_cnt")) {
        this->max_rst_cnt_.store(config["max_rst_cnt"].as_integer());
    }

    /* quic debug */
    if (config.contains("quic_debug"))
    {
        this->quic_debug.store(config["quic_debug"].as_bool());
    }

    /* tcp no delay */
    if (config.contains("tcp_no_delay")) {
        this->tcp_no_delay.store(config["tcp_no_delay"].as_bool());
    }

    /* verify peer */
    if (config.contains("verify_peer")) {
        this->verify_peer.store(config["verify_peer"].as_bool());
    }

    /* speed_check_delay */
    if (config.contains("speed_check_delay")) {
        this->speed_check_delay_.store(config["speed_check_delay"].as_integer());
    }

    /* speed_check_bytes */
    if (config.contains("speed_check_bytes")) {
        this->speed_check_delay_.store(config["speed_check_bytes"].as_integer());
    }
}

manapi::net::http::config::~config() = default;

// ======================[ configs funcs]==========================

void manapi::net::http::config::set_socket_block_size(const size_t &s) {
    this->socket_block_size.store(s);
}

std::atomic<size_t> &manapi::net::http::config::get_socket_block_size() {
    return this->socket_block_size;
}

void manapi::net::http::config::set_max_header_block_size(const size_t &s) {
    this->max_header_block_size.store(s);
}

std::atomic<size_t> &manapi::net::http::config::get_max_header_block_size() {
    return this->max_header_block_size;
}

std::atomic<size_t> &manapi::net::http::config::get_partial_data_min_size() {
    return this->partial_data_min_size;
}

void manapi::net::http::config::set_http_version(const size_t &new_http_version) {
    this->http_version.store(new_http_version);
}

std::atomic<size_t> &manapi::net::http::config::get_http_version() {
    return this->http_version;
}

void manapi::net::http::config::set_http_version_str(const std::string &new_http_version) {
    this->http_version_str = new_http_version;
}

manapi::AtomicReference<std::string> manapi::net::http::config::get_http_version_str() {
    return *this->http_version_str;
}

/**
 * keep alive in seconds
 * @param seconds
 */
void manapi::net::http::config::set_keep_alive(const long int &seconds) {
    this->keep_alive.store(seconds);
}

std::atomic<size_t> &manapi::net::http::config::get_keep_alive() {
    return this->keep_alive;
}

std::atomic<ssize_t> &manapi::net::http::config::get_recv_timeout() {
    return this->recv_timeout;
}

std::atomic<ssize_t> &manapi::net::http::config::get_send_timeout() {
    return this->send_timeout;
}

std::atomic<size_t> & manapi::net::http::config::max_connections() {
    return this->max_connections_;
}

std::atomic<int> & manapi::net::http::config::max_backlog() {
    return this->max_backlog_;
}

void manapi::net::http::config::set_port(const std::string &_port) {
    this->port = _port;
}

manapi::AtomicReference<std::string> manapi::net::http::config::get_port() {
    return *this->port;
}

manapi::AtomicReference<std::string> manapi::net::http::config::get_implementation() {
    return *this->implementation;
}

manapi::AtomicReference<std::string> manapi::net::http::config::get_transport() {
    return *this->transport;
}


manapi::AtomicReference<std::string> manapi::net::http::config::get_address() {
    return *this->address;
}

std::atomic<size_t> &manapi::net::http::config::get_tls_version() {
    return this->tls_version;
}

std::atomic<bool> &manapi::net::http::config::is_quic_debug() {
    return this->quic_debug;
}

std::atomic <size_t> &manapi::net::http::config::get_quic_cc_algo() {
    return this->quic_cc_algo;
}

std::atomic<ssize_t> & manapi::net::http::config::max_rst_cnt() {
    return this->max_rst_cnt_;
}

std::atomic<ssize_t> & manapi::net::http::config::speed_check_delay() {
    return this->speed_check_delay_;
}

std::atomic<ssize_t> & manapi::net::http::config::speed_check_bytes() {
    return this->speed_check_bytes_;
}

std::atomic<ssize_t> & manapi::net::http::config::speed_limit_rate() {
    return this->speed_limit_rate_;
}

manapi::AtomicReference<manapi::net::http::ssl_config_t> manapi::net::http::config::get_ssl_config() {
    return *this->ssl_config;
}

void manapi::net::http::config::set_server_address(const sockaddr &addr) {
    this->server_addr = addr;
}

manapi::AtomicReference<sockaddr> manapi::net::http::config::get_server_address() {
    return *this->server_addr;
}

void manapi::net::http::config::set_server_len(const size_t &len) {
    this->server_len.store(len);
}

std::atomic<socklen_t>  &manapi::net::http::config::get_server_len() {
    return this->server_len;
}

// void manapi::net::http::config::set_http3_config(quiche_h3_config *config) {
//     http3_config = config;
// }
//
// quiche_h3_config * manapi::net::http::config::get_http3_config() {
//     return http3_config;
// }
//
// void manapi::net::http::config::set_quic_config(quiche_config *config) {
//     quic_config = config;
// }
//
// quiche_config * manapi::net::http::config::get_quic_config() {
//     return quic_config;
// }

#ifdef _WIN32
void manapi::net::http::config::set_socket_fd(const SOCKET &fd) {
    this->sock_fd.store(fd);
}

std::atomic<SOCKET> &manapi::net::http::config::get_socket_fd() {
    return this->sock_fd;
}
#else
void manapi::net::http::config::set_socket_fd(const int &fd) {
    this->sock_fd.store(fd);
}

std::atomic<int> &manapi::net::http::config::get_socket_fd() {
    return this->sock_fd;
}
#endif
bool manapi::net::http::config::contains_compressor(const std::string &name) {
    if (this->function_contains_compressor == nullptr) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "function_contains_compressor = {}. We need to set function before call", "nullptr");
    }
    return this->function_contains_compressor (name);
}

void manapi::net::http::config::set_function_contains_compressor(const std::function<bool(const std::string &name)> &func) {
    this->function_contains_compressor = func;
}

const std::string &manapi::net::http::config::stringify_http_version(const versions::http &version) {
    return http_version_to_print.at(version);
}

manapi::net::http::versions::http manapi::net::http::config::parse_http_version(const std::string &version) {
    return http_version_to_parse.at(version);
}

std::atomic<bool> & manapi::net::http::config::get_tcp_no_delay() {
    return this->tcp_no_delay;
}

std::atomic<bool> & manapi::net::http::config::get_verify_peer() {
    return this->verify_peer;
}

std::atomic<ssize_t> & manapi::net::http::config::buffer_size() {
    return this->buffer_size_;
}
