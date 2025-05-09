#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"

const std::map <int , std::string> http_version_to_print = {
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
    this->max_buffer_stack_.store(5);
    this->max_plain_param_length_.store(16000UL);
    this->max_file_param_length_.store(2147483648UL);

    /* partial data min size */
    if (config.contains("partial_data_min_size"))
    {
        this->partial_data_min_size_.store(config["partial_data_min_size"].as_integer());
    }
    else {
        this->partial_data_min_size_.store(0UL);
    }

    /* http versions */
    if (config.contains("http_versions")) {
        auto s = std::set<int> ();
        for (const auto &version : config["http_versions"].as_array() ) {
            int num = -1;
            if (version == "0.9")           num = versions::HTTP_v0_9;
            else if (version == "1.0")      num = versions::HTTP_v1_0;
            else if (version == "1.1")      num = versions::HTTP_v1_1;
            else if (version == "2")        num = versions::HTTP_v2;
            else if (version == "3")        num = versions::HTTP_v3;
            else {
                MANAPIHTTP_LOG(manapi::async::current(), "http version '{}' is invalid in the config", version.as_string());
            }

            if (-1 != num) {
                s.insert(num);
            }
        }
        this->http_versions_ = std::move(s);
    }

    /* port */
    if (config.contains("port")){
        this->port_ = config["port"].as_string();
    }
    else {
        this->port_ = "8888";
    }

    /* address */
    if (config.contains("address")){
        this->address_ = config["address"].as_string();
    }
    else {
        this->address_ = "0.0.0.0";
    }

    /* speed limit rate */
    if (config.contains("speed_limit_rate")) {
        this->speed_limit_rate_ = config["speed_limit_rate"].as_integer();
    }
    else {
        this->speed_limit_rate_.store(2097152000);
    }

    /* ssl */
    if (config.contains("ssl")) {
        this->ssl_config_ = {
            .enabled  = config["ssl"]["enabled"].as_bool(),
            .key      = config["ssl"]["key"].as_string(),
            .cert     = config["ssl"]["cert"].as_string()
        };
    }

    /* max_header_block_size */
    if (config.contains("max_header_block_size"))
    {
        this->max_header_block_size_.store(config["max_header_block_size"].as_integer());
    }
    else {
        this->max_header_block_size_.store(4096UL);
    }

    /* buffer_size */
    if (config.contains("buffer_size")) {
        this->buffer_size_.store(config["buffer_size"].as_integer());
    }
    else {
        this->buffer_size_.store(65536);
    }

    /* max_backlog */
    if (config.contains("max_backlog")) {
        this->max_backlog_.store(config["max_backlog"].as_integer());
    }
    else {
        this->max_backlog().store(200);
    }

    /* keep_alive */
    if (config.contains("keep_alive"))
    {
        this->keep_alive_.store(config["keep_alive"].as_integer());
    }
    else {
        this->keep_alive_.store(2UL);
    }

    /* implementation */
    if (config.contains("implementation"))
    {
        this->implementation_ = config["implementation"].as_string();
    }
    else {
        this->implementation_ = "default";
    }

    /* transport */
    if (config.contains("transport"))
    {
        this->transport_ = config["transport"].as_string();
    }
    else {
        this->transport_ = "tcp";
    }

    /* tls version */
    if (config.contains("tls_version"))
    {
        const std::string &tls_version_string = config["tls_version"].as_string();
        if (tls_version_string == "1" || tls_version_string == "1.0")
        {
            this->tls_version_.store(versions::TLS_v1);
        }
        else if (tls_version_string == "1.1")
        {
            this->tls_version_.store(versions::TLS_v1_1);
        }
        else if (tls_version_string == "1.2")
        {
            this->tls_version_.store(versions::TLS_v1_2);
        }
        else if (tls_version_string == "1.3")
        {
            this->tls_version_.store(versions::TLS_v1_3);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid tls_version in config: {}", tls_version_string);
        }
    }
    else {
        this->tls_version_.store(versions::TLS_v1_3);
    }

    /* quic cc algo */
    if (config.contains("quic_cc_algo"))
    {
        const std::string &quic_cc_algo_string = config["quic_cc_algo"].as_string();
        if (quic_cc_algo_string == "CUBIC")
        {
            this->quic_cc_algo_.store(versions::QUIC_CC_CUBIC);
        }
        else if (quic_cc_algo_string == "RENO")
        {
            this->quic_cc_algo_.store(versions::QUIC_CC_RENO);
        }
        else if (quic_cc_algo_string == "BBR")
        {
            this->quic_cc_algo_.store(versions::QUIC_CC_BBR);
        }
        else if (quic_cc_algo_string == "BBR2")
        {
            this->quic_cc_algo_.store(versions::QUIC_CC_BBR2);
        }
        else if (quic_cc_algo_string == "NONE")
        {
            this->quic_cc_algo_.store(versions::QUIC_CC_NONE);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo param in the config: {}", quic_cc_algo_string);
        }
    }
    else {
        this->quic_cc_algo_ = versions::QUIC_CC_RENO;
    }

    /* max connections */
    if (config.contains("max_connections")) {
        this->max_connections_.store(config["max_connections"].as_integer());
    }
    else {
        this->max_connections_.store(1000);
    }

    /* max_rst_cnt */
    if (config.contains("max_rst_cnt")) {
        this->max_rst_cnt_.store(config["max_rst_cnt"].as_integer());
    }
    else {
        this->max_rst_cnt_.store(5);
    }

    /* quic debug */
    if (config.contains("quic_debug"))
    {
        this->quic_debug_.store(config["quic_debug"].as_bool());
    }
    else {
        this->quic_debug_.store(false);
    }

    /* tcp no delay */
    if (config.contains("tcp_no_delay")) {
        this->tcp_no_delay_.store(config["tcp_no_delay"].as_bool());
    }
    else {
        this->tcp_no_delay_.store(false);
    }

    /* verify peer */
    if (config.contains("verify_peer")) {
        this->verify_peer_.store(config["verify_peer"].as_bool());
    }
    else {
        this->verify_peer_.store(true);
    }

    /* speed_check_delay */
    if (config.contains("speed_check_delay")) {
        this->speed_check_delay_.store(config["speed_check_delay"].as_integer());
    }
    else {
        this->speed_check_delay_.store(5000);
    }

    /* speed_check_bytes */
    if (config.contains("speed_check_bytes")) {
        this->speed_check_bytes_.store(config["speed_check_bytes"].as_integer());
    }
    else {
        this->speed_check_bytes_.store(1048576);
    }

    /* cipher list */
    if (config.contains("cipher_list")) {
        this->cipher_list_ = config["cipher_list"].as_string();
    }
    else {
        this->cipher_list_ = "TLS_CHACHA20_POLY1305_SHA256:TLS_AES_256_GCM_SHA384";
    }
}

manapi::net::http::config::~config() = default;

// ======================[ configs funcs]==========================


void manapi::net::http::config::max_header_block_size(const size_t &s) {
    this->max_header_block_size_.store(s);
}

std::atomic<size_t> &manapi::net::http::config::max_header_block_size() {
    return this->max_header_block_size_;
}

std::atomic<size_t> &manapi::net::http::config::partial_data_min_size() {
    return this->partial_data_min_size_;
}


bool manapi::net::http::config::contains_http_version(int version) {
    auto n = this->http_versions().get();
    return n->contains(version);
}

int manapi::net::http::config::recommended_http_version() {
    auto n = this->http_versions().get();
    if (n->empty()) {
        return -1;
    }
    return *n->rbegin();
}

manapi::Atomic<std::set<int>> & manapi::net::http::config::http_versions() {
    return this->http_versions_;
}

/**
 * keep alive in seconds
 * @param seconds
 */
void manapi::net::http::config::keep_alive(const long int &seconds) {
    this->keep_alive_.store(seconds);
}

std::atomic<size_t> &manapi::net::http::config::keep_alive() {
    return this->keep_alive_;
}

std::atomic<size_t> & manapi::net::http::config::max_connections() {
    return this->max_connections_;
}

std::atomic<int> & manapi::net::http::config::max_backlog() {
    return this->max_backlog_;
}

void manapi::net::http::config::port(const std::string &_port) {
    this->port_ = _port;
}

manapi::AtomicReference<std::string> manapi::net::http::config::port() {
    return *this->port_;
}

manapi::AtomicReference<std::string> manapi::net::http::config::implementation() {
    return *this->implementation_;
}

manapi::AtomicReference<std::string> manapi::net::http::config::transport() {
    return *this->transport_;
}


manapi::AtomicReference<std::string> manapi::net::http::config::address() {
    return *this->address_;
}

manapi::AtomicReference<std::string> manapi::net::http::config::cipher_list() {
    return *this->cipher_list_;
}

std::atomic<size_t> &manapi::net::http::config::tls_version() {
    return this->tls_version_;
}

std::atomic<bool> &manapi::net::http::config::is_quic_debug() {
    return this->quic_debug_;
}

std::atomic <size_t> &manapi::net::http::config::quic_cc_algo() {
    return this->quic_cc_algo_;
}

std::atomic<size_t> & manapi::net::http::config::max_buffer_stack() {
    return this->max_buffer_stack_;
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

manapi::AtomicReference<manapi::net::http::ssl_config_t> manapi::net::http::config::ssl_config() {
    return *this->ssl_config_;
}

void manapi::net::http::config::server_address(const sockaddr &addr) {
    this->server_addr_ = addr;
}

manapi::AtomicReference<sockaddr> manapi::net::http::config::server_address() {
    return *this->server_addr_;
}

void manapi::net::http::config::server_len(const size_t &len) {
    this->server_len_.store(len);
}

std::atomic<socklen_t>  &manapi::net::http::config::server_len() {
    return this->server_len_;
}

bool manapi::net::http::config::contains_compressor(const std::string &name) {
    if (!this->function_contains_compressor_) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "function_contains_compressor = {}. We need to set function before call", "nullptr");
    }
    return this->function_contains_compressor_ (name);
}

void manapi::net::http::config::function_contains_compressor(std::move_only_function<bool(const std::string &name)> func) {
    this->function_contains_compressor_ = std::move(func);
}

const std::string &manapi::net::http::config::stringify_http_version(const int &version) {
    return http_version_to_print.at(version);
}

manapi::net::http::versions::http manapi::net::http::config::parse_http_version(const std::string &version) {
    return http_version_to_parse.at(version);
}

std::atomic<bool> & manapi::net::http::config::tcp_no_delay() {
    return this->tcp_no_delay_;
}

std::atomic<bool> & manapi::net::http::config::verify_peer() {
    return this->verify_peer_;
}

std::atomic<ssize_t> & manapi::net::http::config::buffer_size() {
    return this->buffer_size_;
}
