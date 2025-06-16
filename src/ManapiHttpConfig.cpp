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
    this->max_plain_param_length=(16000UL);
    this->max_file_param_length=(2147483648UL);

    auto &obj = config.as_object();

    auto it = obj.find("max_concurrent_streams");
    if (it != obj.end())
        this->max_concurrent_streams = it->second.as_integer_cast();
    else
        this->max_concurrent_streams = -1;

    it = obj.find("max_frame_size");
    if (it != obj.end())
        this->max_frame_size = it->second.as_integer_cast();
    else
        this->max_frame_size = -1;

    it = obj.find("max_hpack_table_size");
    if (it != obj.end())
        this->max_hpack_table_size = it->second.as_integer_cast();
    else
        this->max_hpack_table_size = -1;

    it = obj.find("max_hpack_list_size");
    if (it != obj.end())
        this->max_hpack_list_size = it->second.as_integer_cast();
    else
        this->max_hpack_list_size = -1;

    it = obj.find("initial_window_size");
    if (it != obj.end())
        this->initial_window_size = it->second.as_integer_cast();
    else
        this->initial_window_size = -1;

    it = obj.find("http1_implementation");
    if (it != obj.end())
        this->http1_implementation = it->second.as_string_cast();
    else
        this->http1_implementation = "default";

    it = obj.find("http2_implementation");
    if (it != obj.end())
        this->http2_implementation = it->second.as_string_cast();
    else
        this->http2_implementation = "default";

    it = obj.find("http3_implementation");
    if (it != obj.end())
        this->http3_implementation = it->second.as_string_cast();
    else
        this->http3_implementation = "default";

    it = obj.find("max_merge_buffer_stack");
    if (it != obj.end())
        this->max_merge_buffer_stack = it->second.as_integer();
    else
        this->max_merge_buffer_stack = 16;

    /* partial data min size */
    if (config.contains("partial_data_min_size"))
    {
        this->partial_data_min_size=(config["partial_data_min_size"].as_integer());
    }
    else {
        this->partial_data_min_size=(0UL);
    }

    if (config.contains("max_buffer_stack")) {
        this->max_buffer_stack = config["max_buffer_stack"].as_integer_cast();
    }
    else {
        this->max_buffer_stack=(5);
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
                MANAPIHTTP_LOG("http version '{}' is invalid in the config", version.as_string());
            }

            if (-1 != num) {
                s.insert(num);
            }
        }
        this->http_versions = std::move(s);
    }

    /* port */
    if (config.contains("port")){
        this->port = config["port"].as_string();
    }
    else {
        this->port = "8888";
    }

    /* address */
    if (config.contains("address")){
        this->address = config["address"].as_string();
    }
    else {
        this->address = "0.0.0.0";
    }

    /* speed limit rate */
    if (config.contains("speed_limit_rate")) {
        this->speed_limit_rate = config["speed_limit_rate"].as_integer();
    }
    else {
        this->speed_limit_rate=(2097152000);
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
        this->max_header_block_size=(config["max_header_block_size"].as_integer());
    }
    else {
        this->max_header_block_size=(4096UL);
    }

    /* buffer_size */
    if (config.contains("buffer_size")) {
        this->buffer_size=(config["buffer_size"].as_integer());
    }
    else {
        this->buffer_size=(65536);
    }

    /* max_backlog */
    if (config.contains("max_backlog")) {
        this->max_backlog=(config["max_backlog"].as_integer());
    }
    else {
        this->max_backlog=(200);
    }

    /* keep_alive */
    if (config.contains("keep_alive"))
    {
        this->keep_alive=(config["keep_alive"].as_integer());
    }
    else {
        this->keep_alive=(2UL);
    }

    /* implementation */
    if (config.contains("implementation"))
    {
        this->implementation = config["implementation"].as_string();
    }
    else {
        this->implementation = "default";
    }

    /* transport */
    if (config.contains("transport"))
    {
        this->transport = config["transport"].as_string();
    }
    else {
        this->transport = "tcp";
    }

    /* tls version */
    if (config.contains("tls_version"))
    {
        const std::string &tls_version_string = config["tls_version"].as_string();
        if (tls_version_string == "1" || tls_version_string == "1.0")
        {
            this->tls_version=(versions::TLS_v1);
        }
        else if (tls_version_string == "1.1")
        {
            this->tls_version=(versions::TLS_v1_1);
        }
        else if (tls_version_string == "1.2")
        {
            this->tls_version=(versions::TLS_v1_2);
        }
        else if (tls_version_string == "1.3")
        {
            this->tls_version=(versions::TLS_v1_3);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "invalid tls_version in config: {}", tls_version_string);
        }
    }
    else {
        this->tls_version=(versions::TLS_v1_3);
    }

    /* quic cc algo */
    if (config.contains("quic_cc_algo"))
    {
        const std::string &quic_cc_algo_string = config["quic_cc_algo"].as_string();
        if (quic_cc_algo_string == "CUBIC")
        {
            this->quic_cc_algo=(versions::QUIC_CC_CUBIC);
        }
        else if (quic_cc_algo_string == "RENO")
        {
            this->quic_cc_algo=(versions::QUIC_CC_RENO);
        }
        else if (quic_cc_algo_string == "BBR")
        {
            this->quic_cc_algo=(versions::QUIC_CC_BBR);
        }
        else if (quic_cc_algo_string == "BBR2")
        {
            this->quic_cc_algo=(versions::QUIC_CC_BBR2);
        }
        else if (quic_cc_algo_string == "NONE")
        {
            this->quic_cc_algo=(versions::QUIC_CC_NONE);
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "invalid quic_cc_algo param in the config: {}", quic_cc_algo_string);
        }
    }
    else {
        this->quic_cc_algo = versions::QUIC_CC_RENO;
    }

    /* max connections */
    if (config.contains("max_connections")) {
        this->max_connections=(config["max_connections"].as_integer());
    }
    else {
        this->max_connections=(1000);
    }

    /* max_rst_cnt */
    if (config.contains("max_rst_cnt")) {
        this->max_rst_cnt=(config["max_rst_cnt"].as_integer());
    }
    else {
        this->max_rst_cnt=(5);
    }

    /* quic debug */
    if (config.contains("quic_debug"))
    {
        this->quic_debug=(config["quic_debug"].as_bool());
    }
    else {
        this->quic_debug=(false);
    }

    /* tcp no delay */
    if (config.contains("tcp_no_delay")) {
        this->tcp_no_delay=(config["tcp_no_delay"].as_bool());
    }
    else {
        this->tcp_no_delay=(false);
    }

    /* verify peer */
    if (config.contains("verify_peer")) {
        this->verify_peer=(config["verify_peer"].as_bool());
    }
    else {
        this->verify_peer=(true);
    }

    /* speed_check_delay */
    if (config.contains("speed_check_delay")) {
        this->speed_check_delay=(config["speed_check_delay"].as_integer());
    }
    else {
        this->speed_check_delay=(5);
    }

    /* speed_check_bytes */
    if (config.contains("speed_check_bytes")) {
        this->speed_check_bytes=(config["speed_check_bytes"].as_integer());
    }
    else {
        this->speed_check_bytes=(1048576);
    }

    /* cipher list */
    if (config.contains("cipher_list")) {
        this->cipher_list = config["cipher_list"].as_string();
    }

    if (config.contains("simultaneous_accepts")) {
        this->simultaneous_accepts=(config["simultaneous_accepts"].as_bool());
    }
    else {
        this->simultaneous_accepts=(false);
    }
}

manapi::net::http::config::~config() = default;

// ======================[ configs funcs]==========================


bool manapi::net::http::config::contains_http_version(int version) {
    return this->http_versions.contains(version);
}

int manapi::net::http::config::recommended_http_version() {
    auto &n = this->http_versions;
    if (n.empty()) {
        return -1;
    }
    return *n.rbegin();
}

bool manapi::net::http::config::contains_compressor(const std::string &name) {
    if (!this->function_contains_compressor_) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "function_contains_compressor = {}. We need to set function before call", "nullptr");
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