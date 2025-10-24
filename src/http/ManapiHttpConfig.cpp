#include "http/ManapiHttpConfig.hpp"
#include "../include/ManapiUtils.hpp"

const std::map <std::string_view, manapi::net::http::versions::http> http_version_to_parse = {
    {"0.9", manapi::net::http::versions::HTTP_v0_9},
    {"1.0", manapi::net::http::versions::HTTP_v1_0},
    {"1.1", manapi::net::http::versions::HTTP_v1_1},
    {"2", manapi::net::http::versions::HTTP_v2},
    {"2.0", manapi::net::http::versions::HTTP_v2},
    {"3", manapi::net::http::versions::HTTP_v3},
    {"3.0", manapi::net::http::versions::HTTP_v3}
};

enum http_version_bits {
    HTTP_VER_BIT_0_9 = 1,
    HTTP_VER_BIT_1_0 = 2,
    HTTP_VER_BIT_1_1 = 4,
    HTTP_VER_BIT_2 = 8,
    HTTP_VER_BIT_3 = 16
};


manapi::net::http::config::config(const json &config) {
    this->http_versions = 0;
    this->server_len = 0;
    this->init_proto_timeout = get_config_param<std::size_t> (config, "init_proto_timeout", 8000);
    this->window_stream_size = get_config_param<ssize_t> (config, "window_stream_size", 2000000);
    this->window_connection_size = get_config_param<ssize_t> (config, "window_connection_size", 4000000);
    this->max_concurrent_streams = get_config_param<ssize_t> (config, "max_concurrent_streams", -1);
    this->max_frame_size = get_config_param<ssize_t>(config, "max_frame_size", -1);
    this->max_hpack_table_size = get_config_param<ssize_t>(config, "max_hpack_table_size", -1);
    this->max_hpack_list_size = get_config_param<ssize_t>(config, "max_hpack_list_size", -1);
    this->initial_window_size = get_config_param<ssize_t>(config, "initial_window_size", -1);
    this->http1_implementation = get_config_param<std::string>(config, "http1_implementation", "default");
    this->http2_implementation = get_config_param<std::string>(config, "http2_implementation", "default");
    this->http3_implementation = get_config_param<std::string>(config, "http3_implementation", "default");
    this->max_merge_buffer_stack = get_config_param<ssize_t>(config, "max_merge_buffer_stack", 2);
    this->partial_data_min_size = get_config_param<ssize_t>(config, "partial_data_min_size", 0);
    this->max_buffer_stack = get_config_param<ssize_t>(config, "max_buffer_stack", 5);
    this->port = get_config_param<std::string>(config, "port", "8888");
    this->address = get_config_param<std::string>(config, "address", "0.0.0.0");
    this->speed_limit_rate = get_config_param<ssize_t>(config, "speed_limit_rate", 2097152000);
    this->max_connections = get_config_param<ssize_t>(config, "max_connections", 1000);
    this->max_connections_by_ip = get_config_param<ssize_t>(config, "max_connections_by_ip", 6);
    this->max_rst_cnt = get_config_param<ssize_t>(config, "max_rst_cnt", 5);
    this->tcp_no_delay = get_config_param<bool>(config, "tcp_no_delay", false);
    this->speed_check_delay = get_config_param<int>(config, "speed_check_delay", 5);
    this->speed_check_bytes = get_config_param<ssize_t>(config, "speed_check_bytes", 1048576);
    this->speed_stream_check_delay = get_config_param<ssize_t>(config, "speed_stream_check_delay", 5);
    this->speed_stream_check_bytes = get_config_param<ssize_t>(config, "speed_stream_check_bytes", 1048576);
    this->simultaneous_accepts = get_config_param<bool>(config, "simultaneous_accepts", false);
    this->max_headers_size = get_config_param<ssize_t>(config, "max_headers_size", 16384);
    this->max_header_key_size = get_config_param<ssize_t>(config, "max_header_key_size", 64);
    this->max_header_value_size = get_config_param<ssize_t>(config, "max_header_value_size", 4096);
    this->buffer_size = get_config_param<uint32_t>(config, "buffer_size", 4096);
    this->tcp_backlog = get_config_param<ssize_t>(config, "tcp_backlog", 200);
    this->tls_accept_timeout = get_config_param<std::size_t>(config, "tls_accept_timeout", 8000);
    this->tls_shutdown_timeout = get_config_param<std::size_t>(config, "tls_shutdown_timeout", 5000);
    this->force_conn_shutdown = get_config_param<bool>(config, "force_conn_shutdown", false);
    this->keep_alive = get_config_param<uint32_t>(config, "keep_alive", 2);
    this->implementation = get_config_param<std::string>(config, "implementation", "default");
    this->transport = get_config_param<std::string>(config, "transport", "tcp");

    if (config.contains("ssl") && config["ssl"].is_object())
        this->ssl = config["ssl"];
    else
        this->ssl = manapi::json::object();

    if (config.contains("quic") && config["quic"].is_object())
        this->quic = config["quic"];
    else
        this->quic = manapi::json::object();

    /* http versions */
    if (config.contains("http")) {
        for (const auto &version : config["http"].as_array() ) {
            int num = 0;
            if (version == "0.9")           num = HTTP_VER_BIT_0_9;
            else if (version == "1.0")      num = HTTP_VER_BIT_1_0;
            else if (version == "1.1")      num = HTTP_VER_BIT_1_1;
            else if (version == "2"
                || version == "2.0")        num = HTTP_VER_BIT_2;
            else if (version == "3"
                || version == "3.0")        num = HTTP_VER_BIT_3;
            else {
                MANAPIHTTP_LOG("http version ('{}') incorrect in the config", version.as_string());
            }

            if (num)
                this->http_versions |= num;
        }
    }
}

manapi::net::http::config::~config() = default;

// ======================[ configs funcs]==========================


bool manapi::net::http::config::contains_http_version(int version) {
    int num = 0;
    switch (version) {
        case http::versions::HTTP_v0_9: num = HTTP_VER_BIT_0_9; break;
        case http::versions::HTTP_v1_0: num = HTTP_VER_BIT_1_0; break;
        case http::versions::HTTP_v1_1: num = HTTP_VER_BIT_1_1; break;
        case http::versions::HTTP_v2: num = HTTP_VER_BIT_2; break;
        case http::versions::HTTP_v3: num = HTTP_VER_BIT_3; break;
        default: return false;
    }
    return this->http_versions & num;
}

std::vector<std::string_view> manapi::net::http::config::alpns() {
    std::vector<std::string_view> tests;
    if (this->http_versions & HTTP_VER_BIT_0_9)
        tests.push_back("http/0.9");
    if (this->http_versions & HTTP_VER_BIT_1_0)
        tests.push_back("http/1.0");
    if (this->http_versions & HTTP_VER_BIT_1_1)
        tests.push_back("http/1.1");
    if (this->http_versions & HTTP_VER_BIT_2)
        tests.push_back("h2");
    if (this->http_versions & HTTP_VER_BIT_3)
        tests.push_back("h3");

    return std::move(tests);
}

bool manapi::net::http::config::contains_compressor(std::string_view name) {
    if (!this->function_contains_compressor_) {
        manapi_log_trace("http config: function_contains_compressor_ wasn't set");
        return false;
    }

    return this->function_contains_compressor_ (name);
}

void manapi::net::http::config::function_contains_compressor(std::move_only_function<bool(std::string_view name)> func) {
    this->function_contains_compressor_ = std::move(func);
}

std::string_view manapi::net::http::config::stringify_http_version(int version) {
    switch (version) {
        case versions::HTTP_v0_9: return "0.9";
        case versions::HTTP_v1_0: return "1.0";
        case versions::HTTP_v1_1: return "1.1";
        case versions::HTTP_v2: return "2";
        case versions::HTTP_v3: return "3";
        default: return "1.1";
    }
}

manapi::error::status_or<manapi::net::http::versions::http> manapi::net::http::config::parse_http_version(std::string_view version) MANAPIHTTP_NOEXCEPT {
    auto it = http_version_to_parse.find(version);
    if (it != http_version_to_parse.end())
        return it->second;

    return error::status_not_found("http version invalid");
}