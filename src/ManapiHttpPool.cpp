#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>
#include "ManapiHttpPool.h"
#include "ManapiTaskHttp.h"
#include "ManapiTaskFunction.h"

manapi::net::http_pool::http_pool(const utils::json &config, http *server, const size_t &id) {
    this->id = id;
    this->server = server;

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

}

manapi::net::http_pool::~http_pool() = default;

// ======================[ configs funcs]==========================

void manapi::net::http_pool::set_socket_block_size(const size_t &s) {
    socket_block_size = s;
}

const size_t &manapi::net::http_pool::get_socket_block_size() const {
    return socket_block_size;
}

void manapi::net::http_pool::set_max_header_block_size(const size_t &s) {
    max_header_block_size = s;
}

const size_t &manapi::net::http_pool::get_max_header_block_size() const {
    return max_header_block_size;
}

const size_t &manapi::net::http_pool::get_partial_data_min_size() const {
    return partial_data_min_size;
}

void manapi::net::http_pool::set_http_version(const size_t &new_http_version) {
    http_version = new_http_version;
}

const size_t &manapi::net::http_pool::get_http_version() const {
    return http_version;
}

void manapi::net::http_pool::set_http_version_str(const std::string &new_http_version) {
    http_version_str = new_http_version;
}

const std::string &manapi::net::http_pool::get_http_version_str() const {
    return http_version_str;
}

/**
 * keep alive in seconds
 * @param seconds
 */
void manapi::net::http_pool::set_keep_alive(const long int &seconds) {
    keep_alive = seconds;
}

const size_t &manapi::net::http_pool::get_keep_alive() const {
    return keep_alive;
}

void manapi::net::http_pool::set_port(const std::string &_port) {
    port = _port;
}

const std::string &manapi::net::http_pool::get_port() const {
    return port;
}

SSL_CTX *manapi::net::http_pool::ssl_create_context(const size_t &version) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    switch (version)
    {
        case versions::TLS_v1:      method = TLSv1_server_method();     break;
        case versions::TLS_v1_1:    method = TLSv1_1_server_method();   break;
        case versions::TLS_v1_2:    method = TLSv1_2_server_method();   break;
        case versions::TLS_v1_3:    method = TLS_server_method();       break;
        case versions::DTLS_v1:     method = DTLSv1_server_method();    break;
        case versions::DTLS_v1_2:   method = DTLSv1_2_server_method();  break;
        case versions::DTLS_v1_3:   method = DTLS_server_method();      break;
        default: THROW_MANAPI_EXCEPTION("can not find the initialization method openssl (tls_version): {}", version);
    }


    ctx = SSL_CTX_new(method);

    if (!ctx)
    {
        THROW_MANAPI_EXCEPTION("{}", "cannot create openssl context for tcp connections");
    }

    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

    return ctx;
}

const manapi::net::ssl_config_t &manapi::net::http_pool::get_ssl_config() {
    return ssl_config;
}

ev::loop_ref manapi::net::http_pool::get_loop() {
    return loop;
}

void manapi::net::http_pool::stop() {
    std::lock_guard <std::mutex> lock (m_initing);

    ////////////////////////
    //////// Stopping //////
    ////////////////////////

    // if udp loop
    if (http_implement == "quic")
    {
        quic_map_conns.lock();

        // clean connections (need to break loop)
        for (auto it = quic_map_conns.begin(); it != quic_map_conns.end();)
        {
            http_task::quic_delete_conn_io(it->second);
            it = quic_map_conns.erase(it);
        }


        quic_map_conns.unlock();
    }
    else if (http_implement == "tls")
    {
        // nothing
    }

    MANAPI_LOG("{}", "shutdown socket");
    // shutdown socket
    shutdown(sock_fd, SHUT_RDWR);

    // stop watcher
    ev_io->stop();

    // break loop
    loop.break_loop();

    // wait while loop is running
    std::lock_guard<std::mutex> lk (m_running);


    ///////////////////////////
    ////////  Clean Up ////////
    ///////////////////////////

    delete ev_io;

    if (http_implement == "quic")
    {
        quiche_h3_config_free(http3_config);
        quiche_config_free(quic_config);

        http3_config    = nullptr;
        quic_config     = nullptr;
    }
    else if (http_implement == "tls")
    {
        if (ssl_config.enabled)
        {
            SSL_CTX_free(ctx);
        }
    }
}

std::future <int> manapi::net::http_pool::run() {
    // wait until another loop is stopping
    m_running.lock();

    pool_promise = new std::promise <int> ();

    std::thread t ([this] () {
        utils::before_delete unwrap_pool_promise ([this] () {
            delete pool_promise;
            pool_promise = nullptr;

            m_running.unlock();
        });

        pool_promise->set_value(_pool());
    });

    t.detach();

    return pool_promise->get_future();
}

int manapi::net::http_pool::_pool() {
    MANAPI_LOG("pool init #{}", id);

    std::unique_lock <std::mutex> lock (m_initing);

    MANAPI_LOG("pool start #{}", id);

    addrinfo hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = IPPROTO_TCP
    };

    int so_reuseaddr_param = 1;
    timeval recv_timeout{}, send_timeout{};

    ev_io  = new ev::io (loop);

    utils::before_delete bd_clean_up ([this] () -> void {
        if (local != nullptr)
        {
            freeaddrinfo(local);
            local = nullptr;
        }
    });

    if (http_implement == "tls")
    {
        hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
            MANAPI_LOG("{}", "failed to resolve host");
            return -1;
        }

        server_addr = local->ai_addr;
        server_len  = local->ai_addrlen;

        MANAPI_LOG("HTTP TLS PORT USED: {}. {}:{}", port, address, port);

        sock_fd = socket(AF_INET, SOCK_STREAM, 0);

        // REUSE PARAM
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

        // TIMEOUT RECV PARAM
        recv_timeout.tv_sec = get_keep_alive();
        recv_timeout.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));

        // TIMEOUT RECV PARAM
        send_timeout.tv_sec = get_keep_alive();
        send_timeout.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));

        if (sock_fd < 0) {
            MANAPI_LOG("{}", "SOCKET ERROR");
            return 1;
        }

        fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK);

        if (bind(sock_fd, local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT {} IS ALREADY IN USE", port);
            return 1;
        }

        if (listen(sock_fd, 10) < 0) {
            MANAPI_LOG("LISTEN ERROR. sock_fd: {}", sock_fd);
            return 1;
        }

        if (ssl_config.enabled) {
            // setup ssl certs

            // init
            ctx = ssl_create_context(tls_version);
            // setup ctx (load certs)
            ssl_configure_context();
        }

        ev_io->set <http_pool, &http_pool::new_connection_tls> (this);
    }
    else if (http_implement == "quic")
    {
        hints = {
                .ai_family      = PF_UNSPEC,
                .ai_socktype    = SOCK_DGRAM,
                .ai_protocol    = IPPROTO_UDP
        };

        if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
            MANAPI_LOG("{}", "failed to resolve host");
            return -1;
        }

        server_addr = local->ai_addr;
        server_len  = local->ai_addrlen;

        MANAPI_LOG("HTTP QUIC PORT USED: {}. https://{}:{}", port, address, port);

        // for quic
        sock_fd = socket (local->ai_family, SOCK_DGRAM, 0);

        if (sock_fd < 0) {
            MANAPI_LOG("{}", "SOCKET ERROR");
            return 1;
        }

        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

        fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK);

        if (bind(sock_fd, local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT {} IS ALREADY IN USE", port);
            return 1;
        }


        //quiche_enable_debug_logging(debug_log, NULL);

        quic_config = quiche_config_new(QUICHE_PROTOCOL_VERSION);

        if (quic_debug)
        {
            quiche_enable_debug_logging([] (const char *line, void *arg) -> void { MANAPI_LOG("{}", line);}, nullptr);
        }
        // ssl
        quiche_config_load_cert_chain_from_pem_file (quic_config, ssl_config.cert.data());
        quiche_config_load_priv_key_from_pem_file   (quic_config, ssl_config.key.data());

        {
            std::string http_application_protocol;

            switch (http_version)
            {
                case versions::HTTP_v0_9:   http_application_protocol = "\x08http/0.9"; break;
                case versions::HTTP_v1_0:   http_application_protocol = "\x08http/1.0"; break;
                case versions::HTTP_v1_1:   http_application_protocol = "\x08http/1.1"; break;
                case versions::HTTP_v2:     http_application_protocol = "\x06http/2";   break;
                case versions::HTTP_v3:     http_application_protocol = "\x02h3";       break;
                default: THROW_MANAPI_EXCEPTION("invalid http_version, default -> {}", "HTTP_v3");
            }

            quiche_config_set_application_protos(quic_config, reinterpret_cast <const uint8_t *> (http_application_protocol.data()), http_application_protocol.size());
        }

        quiche_config_set_max_idle_timeout                      (quic_config, 2000);
        quiche_config_set_max_recv_udp_payload_size             (quic_config, MANAPI_MAX_DATAGRAM_SIZE);
        quiche_config_set_max_send_udp_payload_size             (quic_config,  MANAPI_MAX_DATAGRAM_SIZE);
        quiche_config_set_initial_max_data                      (quic_config, 10000000);
        quiche_config_set_initial_max_stream_data_bidi_local    (quic_config, 1000000);
        quiche_config_set_initial_max_stream_data_bidi_remote   (quic_config, 1000000);
        quiche_config_set_initial_max_stream_data_uni           (quic_config, 1000000);
        quiche_config_set_initial_max_streams_bidi              (quic_config, 100);
        quiche_config_set_initial_max_streams_uni               (quic_config, 100);
        quiche_config_set_disable_active_migration              (quic_config, true);
        quiche_config_enable_early_data                         (quic_config);

        if (quic_cc_algo != versions::QUIC_CC_NONE) {
            quiche_cc_algorithm algo = QUICHE_CC_RENO;

            switch (quic_cc_algo)
            {
                case versions::QUIC_CC_CUBIC:   algo = QUICHE_CC_CUBIC;     break;
                case versions::QUIC_CC_RENO:    algo = QUICHE_CC_RENO;      break;
                case versions::QUIC_CC_BBR:     algo = QUICHE_CC_BBR;       break;
                case versions::QUIC_CC_BBR2:    algo = QUICHE_CC_BBR2;      break;
                default: THROW_MANAPI_EXCEPTION("invalid quic_cc_algo: {}", quic_cc_algo);
            }

            quiche_config_set_cc_algorithm                      (quic_config, algo);
        }

        // quiche_config_enable_early_data                         (quic_config);


        http3_config = quiche_h3_config_new();

        if (http3_config == nullptr)
        {
            THROW_MANAPI_EXCEPTION("{}", "failed to create HTTP/3 config");
            return 1;
        }

        ev_io->set <http_pool, &http_pool::new_connection_quic> (this);
    }
    else
    {
        THROW_MANAPI_EXCEPTION("invalid http_implement: {}", http_implement);
    }

    // create watcher
    ev_io->start(sock_fd, ev::READ);

    // say, that it can be deleted
    lock.unlock();

    loop.run(ev::AUTO);

    return 0;
}

void manapi::net::http_pool::new_connection_quic(ev::io &watcher, int revents) {
    ssize_t buff_size = MANAPI_MAX_DATAGRAM_SIZE;
    char buff[buff_size];

    sockaddr_storage client{};
    socklen_t client_len = sizeof(sockaddr_storage);
    memset(&client, 0, client_len);

    buff_size = recvfrom(sock_fd, &buff, buff_size, 0, reinterpret_cast<struct sockaddr *> (&client), &client_len);

    utils::manapi_socket_information socket_information = {
        .ip = inet_ntoa(reinterpret_cast<struct sockaddr_in *> (&client)->sin_addr),
        .port = htons(reinterpret_cast<struct sockaddr_in *> (&client)->sin_port)
    };

    if (buff_size < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            MANAPI_LOG("{}", "recv would block");
            return;
        }

        MANAPI_LOG("failed to read: recvfrom(...) = {}", buff_size);
        return;
    }

    {
        uint8_t out[4096 * 2];

        uint8_t     type;
        uint32_t    version;

        uint8_t     s_cid[QUICHE_MAX_CONN_ID_LEN];
        size_t      s_cid_len = sizeof (s_cid);

        uint8_t     d_cid[QUICHE_MAX_CONN_ID_LEN];
        size_t      d_cid_len = sizeof (s_cid);

        uint8_t     od_cid[QUICHE_MAX_CONN_ID_LEN];
        size_t      od_cid_len = sizeof (s_cid);

        uint8_t     token[quic_token_max_len];
        size_t      token_len = sizeof (token);

        int read = quiche_header_info(reinterpret_cast<uint8_t *> (buff), buff_size, MANAPI_QUIC_CONNECTION_ID_LEN, &version, &type, s_cid, &s_cid_len, d_cid, &d_cid_len, token, &token_len);

        if (read < 0)
        {
            THROW_MANAPI_EXCEPTION("{}", "failed to parse quic headers. quiche_header_info(...) < 0");
        }

        const std::string dcid_str (reinterpret_cast <const char *> (d_cid), d_cid_len);

        // LOCK UNORDERED MAP OF THE CONNECTIONS
        quic_map_conns.lock();

        // UNLOCK UNORDERED MAP OF THE CONNECTIONS AFTER DELETING OR CALLING unwrap.call(...)
        utils::before_delete unlock_quic_map_conns ([this] () -> void { quic_map_conns.unlock(); });

        auto conn_io = quic_map_conns.contains(dcid_str) ? quic_map_conns.at(dcid_str) : nullptr;

        if (conn_io == nullptr)
        {
            // UNLOCK UNORDERED MAP
            unlock_quic_map_conns.call();

            // no connections in the history
            if (!quiche_version_is_supported(version)) {
                MANAPI_LOG("version negotiation: {}", version);

                const ssize_t written = quiche_negotiate_version(s_cid, s_cid_len,
                                                           d_cid, d_cid_len,
                                                           out, sizeof(out));

                if (written < 0)
                {
                    THROW_MANAPI_EXCEPTION("failed to create vneg packet: {}",
                            written);
                    return;
                }

                const ssize_t sent = sendto(sock_fd, out, written, 0,
                                      (struct sockaddr *) &client,
                                      client_len);

                if (sent != written)
                {
                    THROW_MANAPI_EXCEPTION("{}", "failed to register new connection (sent != written)");

                    return;
                }

                // fprintf(stderr, "sent %zd bytes\n", sent);
                return;
            }

            if (token_len == 0)
            {
                http_task::mint_token(d_cid, d_cid_len, &client, client_len,
                           token, &token_len);

                uint8_t new_cid[MANAPI_QUIC_CONNECTION_ID_LEN];

                if (http_task::gen_cid(new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) == nullptr)
                {
                    return;
                }

                // std::cout << "new s_cid -> " << std::string((char *)new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) << "\n";

                const ssize_t written = quiche_retry(s_cid, s_cid_len,
                                               d_cid, d_cid_len,
                                               new_cid, MANAPI_QUIC_CONNECTION_ID_LEN,
                                               token, token_len,
                                               version, out, sizeof(out));

                if (written < 0)
                {
                    THROW_MANAPI_EXCEPTION("failed to create retry packet: {}", written);

                    return;
                }

                const ssize_t sent = sendto(sock_fd, out, written, 0,
                                      (struct sockaddr *) &client,
                                      client_len);
                if (sent != written)
                {
                    THROW_MANAPI_EXCEPTION("{}", "failed to send: sendto(...) != written");
                    return;
                }

                // -printf(" -> sent %zd bytes\n", sent);
                return;
            }

            if (!http_task::validate_token(token, token_len, &client, client_len,
                                od_cid, &od_cid_len)) {
                THROW_MANAPI_EXCEPTION ("{}", "invalid address validation token");
                return;
                                }

            conn_io = http_task::quic_create_connection(d_cid, d_cid_len, od_cid, od_cid_len, sock_fd, client, client_len, this);

            if (conn_io == nullptr)
            {
                return;
            }
        }
        else
        {
            unlock_quic_map_conns.call();
        }

        auto ta = new http_task(sock_fd, buff, buff_size, client, client_len, ev_io, this);
        ta->set_quic_config(quic_config);
        ta->set_quic_map_conns(&quic_map_conns);
        ta->conn_io = conn_io;
        ta->quic_conn_io_key = conn_io->key;

        server->append_task(ta);
    }

    //server->append_task(ft);
}

void manapi::net::http_pool::new_connection_tls(ev::io &watcher, int revents) {
    struct sockaddr_storage client{};
    socklen_t len = sizeof(client);
    conn_fd = accept(sock_fd, reinterpret_cast<struct sockaddr *>(&client), &len);

    if (conn_fd < 0)
    {
        return;
    }

    auto *ta = new http_task(conn_fd, client, len, this);

    if (ssl_config.enabled)
    {
        ta->ssl = SSL_new(ctx);
    }

    server->append_task(ta);
}

manapi::net::http * manapi::net::http_pool::get_http() const {
    return server;
}

void manapi::net::http_pool::ssl_configure_context() {
    if (SSL_CTX_use_certificate_file(ctx, ssl_config.cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION("{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, ssl_config.key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION("{}", "cannot use private key file openssl");
    }
}
