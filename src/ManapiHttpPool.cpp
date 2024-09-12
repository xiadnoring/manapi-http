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
#include "ManapiHttpPool.hpp"
#include "ManapiTaskHttp.hpp"
#include "ManapiTaskFunction.hpp"

manapi::net::http_pool::http_pool(const utils::json &config, class site *site, const size_t &id) : config (config) {
    this->id = id;
    this->site = site;

    this->config.set_function_contains_compressor([site] (const std::string &name) -> bool { return site->contains_compressor(name); });
}

manapi::net::http_pool::~http_pool() = default;

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

ev::loop_ref manapi::net::http_pool::get_loop() {
    return loop;
}

void manapi::net::http_pool::stop() {
    std::lock_guard <std::mutex> lock (m_initing);

    ////////////////////////
    //////// Stopping //////
    ////////////////////////

    // if udp loop
    if (config.get_http_implement() == "quic")
    {
        quic_map_conns.lock();

        // clean connections (need to break loop)
        for (auto it = quic_map_conns.begin(); it != quic_map_conns.end();)
        {
            http_task::quic_delete_conn_io(it->second, site);
            it = quic_map_conns.erase(it);
        }


        quic_map_conns.unlock();
    }
    else if (config.get_http_implement() == "tls")
    {
        // nothing
    }

    MANAPI_LOG("{}", "shutdown socket");

    // close socket
    shutdown(config.get_socket_fd(), SHUT_RDWR);

    // stop watcher
    ev_io->stop();

    // break loop
    loop.break_loop();

    // wait while loop is running
    std::lock_guard<std::mutex> lk (m_running);
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

const int &manapi::net::http_pool::get_fd() {
    return config.get_socket_fd();
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
        if (config.get_http_implement() == "quic")
        {
            if (config.get_quic_implement() == "quiche")
            {
                if (config.get_http3_config() != nullptr) quiche_h3_config_free(config.get_http3_config());
                if (config.get_quic_config() != nullptr) quiche_config_free(config.get_quic_config());

                config.set_http3_config(nullptr);
                config.set_quic_config(nullptr);
            }
        }
        else if (config.get_http_implement() == "tls")
        {
            if (config.get_ssl_config().enabled)
            {
                SSL_CTX_free(config.get_openssl_ctx());
            }
        }

        freeaddrinfo(local);
        local = nullptr;

        close(config.get_socket_fd());

        delete ev_io;
    });

    if (config.get_http_implement() == "tls")
    {
        hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        if (getaddrinfo(config.get_address().data(), config.get_port().data(), &hints, &local) != 0) {
            MANAPI_LOG("{}", "failed to resolve host");
            return -1;
        }

        config.set_server_address(*local->ai_addr);
        config.set_server_len(local->ai_addrlen);

        MANAPI_LOG("HTTP TLS PORT USED: {}. {}:{}", config.get_port(), config.get_address(), config.get_port());

        config.set_socket_fd(socket(AF_INET, SOCK_STREAM, 0));
        if (config.get_socket_fd() < 0) {
            MANAPI_LOG("{}", "SOCKET ERROR");
            return 1;
        }
        // REUSE PARAM
        setsockopt(config.get_socket_fd(), SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

        // TIMEOUT RECV PARAM
        recv_timeout.tv_sec = config.get_keep_alive();
        recv_timeout.tv_usec = 0;
        setsockopt(config.get_socket_fd(), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));

        // TIMEOUT RECV PARAM
        send_timeout.tv_sec = config.get_keep_alive();
        send_timeout.tv_usec = 0;
        setsockopt(config.get_socket_fd(), SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));

        if (fcntl(config.get_socket_fd(), F_SETFL, O_NONBLOCK) != 0) {
            MANAPI_LOG("Failed to make socket {} non-blocking", config.get_socket_fd());
            return 1;
        }

        if (bind(config.get_socket_fd(), local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT {} IS ALREADY IN USE", config.get_port());
            return 1;
        }

        if (listen(config.get_socket_fd(), 10) < 0) {
            MANAPI_LOG("LISTEN ERROR. sock_fd: {}", config.get_socket_fd());
            return 1;
        }

        if (config.get_ssl_config().enabled) {
            // setup ssl certs

            // init
            config.set_openssl_ctx(ssl_create_context(config.get_tls_version()));
            // setup ctx (load certs)
            ssl_configure_context();
        }

        ev_io->set <http_pool, &http_pool::new_connection_tls> (this);
    }
    else if (config.get_http_implement() == "quic")
    {
        hints = {
                .ai_family      = PF_UNSPEC,
                .ai_socktype    = SOCK_DGRAM,
                .ai_protocol    = IPPROTO_UDP
        };

        if (getaddrinfo(config.get_address().data(), config.get_port().data(), &hints, &local) != 0) {
            MANAPI_LOG("{}", "failed to resolve host");
            return -1;
        }

        config.set_server_address(*local->ai_addr);
        config.set_server_len(local->ai_addrlen);

        MANAPI_LOG("HTTP QUIC PORT USED: {}. https://{}:{}", config.get_port(), config.get_address(), config.get_port());

        // for quic
        config.set_socket_fd(socket (local->ai_family, SOCK_DGRAM, 0));

        if (config.get_socket_fd() < 0) {
            MANAPI_LOG("{}", "SOCKET ERROR");
            return 1;
        }

        setsockopt(config.get_socket_fd(), SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

        if (fcntl(config.get_socket_fd(), F_SETFL, O_NONBLOCK) != 0) {
            MANAPI_LOG("Failed to make socket {} non-blocking", config.get_socket_fd());
            return 1;
        }

        if (bind(config.get_socket_fd(), local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT {} IS ALREADY IN USE", config.get_port());
            return 1;
        }

        if (config.get_quic_implement() == "quiche")
        {
            // THE 2 MB MEMORY WILL BE LOCK FOR EVER ONE TIME
            config.set_quic_config(quiche_config_new(QUICHE_PROTOCOL_VERSION));

            if (config.is_quic_debug())
            {
                quiche_enable_debug_logging([] (const char *line, void *arg) -> void { MANAPI_LOG("{}", line);}, nullptr);
            }
            // ssl
            quiche_config_load_cert_chain_from_pem_file (config.get_quic_config(), config.get_ssl_config().cert.data());
            quiche_config_load_priv_key_from_pem_file   (config.get_quic_config(), config.get_ssl_config().key.data());

            {
                std::string http_application_protocol;

                switch (config.get_http_version())
                {
                    case versions::HTTP_v0_9:   http_application_protocol = "\x08http/0.9"; break;
                    case versions::HTTP_v1_0:   http_application_protocol = "\x08http/1.0"; break;
                    case versions::HTTP_v1_1:   http_application_protocol = "\x08http/1.1"; break;
                    case versions::HTTP_v2:     http_application_protocol = "\x06http/2";   break;
                    case versions::HTTP_v3:     http_application_protocol = "\x02h3";       break;
                    default: THROW_MANAPI_EXCEPTION("invalid http_version, default -> {}", "HTTP_v3");
                }

                quiche_config_set_application_protos(config.get_quic_config(), reinterpret_cast <const uint8_t *> (http_application_protocol.data()), http_application_protocol.size());
            }

            quiche_config_set_max_idle_timeout                      (config.get_quic_config(), 50000);
            quiche_config_set_max_recv_udp_payload_size             (config.get_quic_config(), MANAPI_MAX_DATAGRAM_SIZE);
            quiche_config_set_max_send_udp_payload_size             (config.get_quic_config(),  MANAPI_MAX_DATAGRAM_SIZE);
            quiche_config_set_initial_max_data                      (config.get_quic_config(), 10000000);
            quiche_config_set_initial_max_stream_data_bidi_local    (config.get_quic_config(), 1000000);
            quiche_config_set_initial_max_stream_data_bidi_remote   (config.get_quic_config(), 1000000);
            quiche_config_set_initial_max_stream_data_uni           (config.get_quic_config(), 1000000);
            quiche_config_set_initial_max_streams_bidi              (config.get_quic_config(), 100);
            quiche_config_set_initial_max_streams_uni               (config.get_quic_config(), 100);
            quiche_config_set_disable_active_migration              (config.get_quic_config(), true);
            quiche_config_enable_early_data                         (config.get_quic_config());

            if (config.get_quic_cc_algo() != versions::QUIC_CC_NONE) {
                quiche_cc_algorithm algo = QUICHE_CC_RENO;

                switch (config.get_quic_cc_algo())
                {
                    case versions::QUIC_CC_CUBIC:   algo = QUICHE_CC_CUBIC;     break;
                    case versions::QUIC_CC_RENO:    algo = QUICHE_CC_RENO;      break;
                    case versions::QUIC_CC_BBR:     algo = QUICHE_CC_BBR;       break;
                    case versions::QUIC_CC_BBR2:    algo = QUICHE_CC_BBR2;      break;
                    default: THROW_MANAPI_EXCEPTION("invalid quic_cc_algo: {}", config.get_quic_cc_algo());
                }

                quiche_config_set_cc_algorithm                      (config.get_quic_config(), algo);
            }

            quiche_config_enable_early_data                         (config.get_quic_config());


            config.set_http3_config(quiche_h3_config_new());

            if (config.get_http3_config() == nullptr)
            {
                THROW_MANAPI_EXCEPTION("{}", "failed to create HTTP/3 config");
                return 1;
            }
        }
        else if (config.get_quic_implement() == "msquic")
        {

        }
        else
        {
            THROW_MANAPI_EXCEPTION("invalid quic_implement: {}", config.get_quic_implement());
        }

        ev_io->set <http_pool, &http_pool::new_connection_quic> (this);
    }
    else
    {
        THROW_MANAPI_EXCEPTION("invalid http_implement: {}", config.get_http_implement());
    }

    // create watcher
    ev_io->start(config.get_socket_fd(), ev::READ);

    // say, that it can be deleted
    lock.unlock();
    loop.run(ev::AUTO);

    return 0;
}

void manapi::net::http_pool::new_connection_quic(ev::io &watcher, int revents) {
    uint8_t buff[65535];
    uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];

    while (true) {
        ssize_t buff_size = sizeof (buff);
        sockaddr_storage client{};
        socklen_t client_len = sizeof(sockaddr_storage);
        memset(&client, 0, client_len);

        buff_size = recvfrom(config.get_socket_fd(), &buff, buff_size, 0, reinterpret_cast<struct sockaddr *> (&client), &client_len);
        utils::manapi_socket_information socket_information = {
            .ip = inet_ntoa(reinterpret_cast<struct sockaddr_in *> (&client)->sin_addr),
            .port = htons(reinterpret_cast<struct sockaddr_in *> (&client)->sin_port)
        };

        if (buff_size < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                break;
            }

            MANAPI_LOG("failed to read: recvfrom(...) = {}", buff_size);
            return;
        }
        {
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

            int read = quiche_header_info(buff, buff_size, MANAPI_QUIC_CONNECTION_ID_LEN, &version, &type, s_cid, &s_cid_len, d_cid, &d_cid_len, token, &token_len);

            if (read < 0)
            {
                MANAPI_LOG("{}", "failed to parse quic headers. quiche_header_info(...) < 0");
                continue;
            }

            const std::string dcid_str (reinterpret_cast <const char *> (d_cid), d_cid_len);

            // LOCK UNORDERED MAP OF THE CONNECTIONS
            quic_map_conns.lock();

            // UNLOCK UNORDERED MAP OF THE CONNECTIONS AFTER DELETING OR CALLING unwrap.call(...)
            utils::before_delete unlock_quic_map_conns ([this] () -> void { quic_map_conns.unlock(); });

            auto conn_io = quic_map_conns.contains(dcid_str) ? quic_map_conns.at(dcid_str) : nullptr;
            bool new_connection_pool = false;
            if (conn_io == nullptr)
            {
                MANAPI_LOG("connections: {} ({})", quic_map_conns.size() + 1, dcid_str);
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
                        MANAPI_LOG("failed to create vneg packet: {}",
                                written);
                        continue;
                    }

                    const ssize_t sent = sendto(config.get_socket_fd(), out, written, 0,
                                          (struct sockaddr *) &client,
                                          client_len);

                    if (sent != written)
                    {
                        MANAPI_LOG("{}", "failed to register new connection (sent != written)");

                        continue;
                    }

                    // fprintf(stderr, "sent %zd bytes\n", sent);
                    continue;
                }

                if (token_len == 0)
                {
                    http_task::mint_token(d_cid, d_cid_len, &client, client_len,
                               token, &token_len);

                    uint8_t new_cid[MANAPI_QUIC_CONNECTION_ID_LEN];

                    if (http_task::gen_cid(new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) == nullptr)
                    {
                        continue;
                    }

                    // std::cout << "new s_cid -> " << std::string((char *)new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) << "\n";

                    const ssize_t written = quiche_retry(s_cid, s_cid_len,
                                                   d_cid, d_cid_len,
                                                   new_cid, MANAPI_QUIC_CONNECTION_ID_LEN,
                                                   token, token_len,
                                                   version, out, sizeof(out));

                    if (written < 0)
                    {
                        MANAPI_LOG("failed to create retry packet: {}", written);

                        continue;
                    }

                    const ssize_t sent = sendto(config.get_socket_fd(), out, written, 0,
                                          (struct sockaddr *) &client,
                                          client_len);
                    if (sent != written)
                    {
                        MANAPI_LOG("{}", "failed to send: sendto(...) != written");
                        continue;
                    }

                    // -printf(" -> sent %zd bytes\n", sent);
                    continue;
                }

                if (!http_task::validate_token(token, token_len, &client, client_len,
                                    od_cid, &od_cid_len)) {
                    MANAPI_LOG ("{}", "invalid address validation token");
                    continue;
                }

                conn_io = http_task::quic_create_connection(d_cid, d_cid_len, od_cid, od_cid_len, config.get_socket_fd(), client, client_len, &config, site, &quic_map_conns);

                if (conn_io == nullptr)
                {
                    continue;
                }
                if (!conn_io->is_responsing) { new_connection_pool = true; conn_io->is_responsing = true; }
                MANAPI_LOG("new connection: {}", dcid_str);
            }
            else
            {
                if (!conn_io->is_responsing) { new_connection_pool = true; conn_io->is_responsing = true; }
                unlock_quic_map_conns.call();
            }

            {
                std::lock_guard<std::mutex> lk (conn_io->buffers_mutex);
                conn_io->buffers.push_back(std::string ((char *) buff, buff_size));
                if (new_connection_pool) {
                    get_site().append_task(
                        new function_task ([this, dcid_str, client, client_len] () -> void { http_task::udp_loop_event (&quic_map_conns, site, &config, dcid_str, reinterpret_cast<const sockaddr &>(client), client_len); }),
                        2);
                }
            }

            //get_http()->append_task(task, 1);
        }
    }

        get_site().append_task(new function_task ([this] () -> void { http_task::quic_generate_output_packages (&quic_map_conns, site); }), 2);
}

void manapi::net::http_pool::new_connection_tls(ev::io &watcher, int revents) {
    struct sockaddr_storage client{};
    socklen_t len = sizeof(client);
    int conn_fd = accept(config.get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);

    if (conn_fd < 0)
    {
        return;
    }

    auto *ta = new http_task(conn_fd, reinterpret_cast<const sockaddr &>(client), len, site, &config, CONN_TCP);

    if (config.get_ssl_config().enabled)
    {
        ta->ssl = SSL_new(config.get_openssl_ctx());
    }

    get_site().append_task(ta, 1);
}

manapi::net::site & manapi::net::http_pool::get_site() const {
    return *site;
}

void manapi::net::http_pool::ssl_configure_context() {
    if (SSL_CTX_use_certificate_file(config.get_openssl_ctx(), config.get_ssl_config().cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION("{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(config.get_openssl_ctx(), config.get_ssl_config().key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION("{}", "cannot use private key file openssl");
    }
}
