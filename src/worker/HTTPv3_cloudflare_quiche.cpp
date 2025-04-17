#include <http/HTTPv2.hpp>

#include "crypto/ManapiAEAD.hpp"
#include "worker/HTTPv3_clouflare_quiche.hpp"
#include "ManapiVersions.hpp"
#include "ManapiParams.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

# define MANAPI_MAX_DATAGRAM_SIZE 1350
# define MANAPI_QUICHE_CONNECTION_ID_LEN 16

constexpr static size_t _quiche_token_max_len = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;

template<typename T>
requires(version_greater_or_equals(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
    return quiche_h3_event_headers_has_more_frames(static_cast<T>(event));
}

template<typename T>
requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
    return quiche_h3_event_headers_has_body(static_cast<T>(event));
}

template<typename ...Args>
requires(version_greater_or_equals(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args...args) {
    return quiche_h3_send_additional_headers(args...);
}

template<typename ...Args>
requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args...args) { /* skip */ return 0; }

manapi::net::worker::http_v3_cloudflare_quiche::http_v3_cloudflare_quiche(net::site &site) : udp(site) {

}

manapi::net::worker::http_v3_cloudflare_quiche::~http_v3_cloudflare_quiche() {
    quiche_config_free(this->_quiche_config);
    quiche_h3_config_free(this->_quiche_h3_config);
}

void manapi::net::worker::http_v3_cloudflare_quiche::onrecv(ev::io &watcher, int revents) {
    sockaddr_storage sockaddr_src{};
    socklen_t sockaddr_len = sizeof (sockaddr_src);
    memset(&sockaddr_src, '\0', sockaddr_len);

    uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];

    while (true) {
        std::shared_ptr<worker::connection> connection;

        auto rhs = ::recvfrom(this->fd, this->gbuffer.data(), this->gbuffer_size, 0x00, reinterpret_cast<sockaddr *> (&sockaddr_src), &sockaddr_len);

        if (rhs <= 0) {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
                break;
            }
            /* socker error */

            return;
        }

        uint8_t type;
        uint32_t version;

        std::string scid;
        scid.resize(QUICHE_MAX_CONN_ID_LEN);
        uint64_t scid_len = scid.size();

        std::string dcid;
        dcid.resize(QUICHE_MAX_CONN_ID_LEN);
        uint64_t dcid_len = dcid.size();

        std::string odcid;

        std::string token;
        token.resize(_quiche_token_max_len);
        uint64_t token_len = token.size();

        if(0 != quiche_header_info (reinterpret_cast<uint8_t *> (this->gbuffer.data()), rhs, MANAPI_QUICHE_CONNECTION_ID_LEN,
            &version, &type, reinterpret_cast<uint8_t *> (scid.data()), &scid_len, reinterpret_cast<uint8_t *> (dcid.data()), &dcid_len,
            reinterpret_cast<uint8_t *> (token.data()), &token_len)) {

            /* broken packet */
            return;
            }

        scid.resize(scid_len);
        dcid.resize(dcid_len);
        token.resize(token_len);

        auto it = this->connections.find(dcid);
        if (it == this->connections.end()) {
            if (this->connections.size() >= this->config->max_connections()) {
                return;
            }

            if (!quiche_version_is_supported(version)) {
                const int64_t written = quiche_negotiate_version(reinterpret_cast<uint8_t *> (scid.data()),
                    scid.size(), reinterpret_cast<uint8_t *> (dcid.data()), dcid.size(), out, sizeof (out));

                if (written < 0) {
                    return;
                }

                rhs = ::sendto(this->fd, out, written, 0x00, reinterpret_cast<sockaddr*> (&sockaddr_src), sockaddr_len);

                if (rhs != written) {
                    /* failed to send */
                }

                return;
            }

            if (token.empty()) {
                token = http_v3_cloudflare_quiche::_gen_mint_token(dcid, sockaddr_src, sockaddr_len);
                std::string new_scid = crypto::random_string(MANAPI_QUICHE_CONNECTION_ID_LEN);
                const int64_t written = quiche_retry(reinterpret_cast<uint8_t *> (scid.data()), scid.size(), reinterpret_cast<uint8_t *>(dcid.data()), dcid.size(),
                    reinterpret_cast<uint8_t *>(new_scid.data()), new_scid.size(), reinterpret_cast<uint8_t *>(token.data()), token.size(), version, out, sizeof (out));

                if (written < 0) {
                    return;
                }

                rhs = ::sendto(this->fd, out, written, 0x00, reinterpret_cast<sockaddr*>(&sockaddr_src), sockaddr_len);

                if (rhs != written) {
                    /* failed to send */
                }

                return;
            }

            if (!http_v3_cloudflare_quiche::_validate_token(token, odcid, sockaddr_src, sockaddr_len)) {
                return;
            }

            auto _quiche_conn = quiche_accept(reinterpret_cast<uint8_t *> (dcid.data()), dcid.size(),
                reinterpret_cast<uint8_t *>(odcid.data()), odcid.size(), &*config->get_server_address(), config->get_server_len(),
                reinterpret_cast<sockaddr *>(&sockaddr_src), sockaddr_len, this->_quiche_config);

            if (!_quiche_conn) {
                return;
            }

            connection = std::make_shared<worker::connection>(new connection_t{
                .cid = dcid, .conn = _quiche_conn, .quiche_timer = this->le->get_loop(), .io_timer = {}, .write_watcher = this->le->get_loop(), .http3_conn = {nullptr},
                .streams = {}, .worker = std::shared_ptr (this->worker), .status = 0, .write_total = 0, .read_total = 0, .write_total_prev = 0, .read_total_prev = 0,
                .transfared_last_second = 0}, http_v3_cloudflare_quiche::_clean_connection
            );

            auto &conn_data = connection->as<connection_t>();

            auto ev_conn_ptr = new decltype(connection)(connection);

            conn_data.quiche_timer.data = ev_conn_ptr;
            //conn_data.timer.data = ev_conn_ptr;
            conn_data.write_watcher.data = ev_conn_ptr;

            ev_async_init(&conn_data.write_watcher, http_v3_cloudflare_quiche::_write_watcher_cb);
            ev_init(&conn_data.quiche_timer, http_v3_cloudflare_quiche::_quiche_timeout);
            conn_data.quiche_timer.priority = priority::timeout_timer;
            conn_data.io_timer = this->site.async_context()->timerpool()->append_interval_sync(this->config->speed_check_delay(),
                [this, ev_conn_ptr] (manapi::timer t) -> void { this->_io_timeout(ev_conn_ptr->get()->as<connection_t>()); });

            //conn_data.timer.start();
            conn_data.write_watcher.start();

            this->connections.insert({
                dcid,
                connection
            });
        }
        else {
            connection = it->second;
        }

        auto &conn_data = connection->as<connection_t>();

        quiche_recv_info recv_info {
            reinterpret_cast <sockaddr *>(&sockaddr_src),
            sockaddr_len,
            (sockaddr *)&*this->config->get_server_address(),
            this->config->get_server_len()
        };

        ssize_t done = quiche_conn_recv(conn_data.conn, reinterpret_cast<uint8_t*>(this->gbuffer.data()), rhs, &recv_info);

        if (done < 0) {
            return;
        }

        if ((quiche_conn_is_in_early_data(conn_data.conn) || quiche_conn_is_established(conn_data.conn)) && !conn_data.http3_conn) {
            conn_data.http3_conn = quiche_h3_conn_new_with_transport(conn_data.conn, this->_quiche_h3_config);

            if (!conn_data.http3_conn) {
                MANAPIHTTP_LOG2(this->site.async_context(), "QUICHE: assert(!conn_data.http3_conn) failed");
                return;
            }
        }

        if (conn_data.http3_conn && !(conn_data.status & CONN_CLOSED)) {
            http_v3_cloudflare_quiche::_flush_write(conn_data, false);

            quiche_h3_event *event{nullptr};

            while (true) {
                int64_t stream_id = quiche_h3_conn_poll(conn_data.http3_conn, conn_data.conn, &event);

                if (stream_id < 0) {
                    break;
                }

                switch (quiche_h3_event_type(event)) {
                    case QUICHE_H3_EVENT_HEADERS: {
                        auto stream_it = conn_data.streams.find(stream_id);
                        std::shared_ptr<http::http_v2> client;
                        if (stream_it == conn_data.streams.end()) {
                            auto worker = this->new_dependency();
                            client = std::make_shared<http::http_v2>(std::move(worker), this->config, this->site);
                            client->connection = std::make_shared<worker::connection>(new connection_stream_t (
                                stream_id, connection, this->site.async_context(), 0x00,
                                0, 0, {nullptr}),
                                +[] (void *ptr) -> void { delete static_cast<connection_stream_t *> (ptr); });
                            client->connection->version = http::versions::HTTP_v3;
                            conn_data.streams[stream_id] = client->connection;
                        }
                        else {
                            MANAPIHTTP_LOG2 (this->site.async_context(), "Several QUICHE_H3_EVENT_HEADERS was received");
                            break;
                        }

                        quiche_h3_event_for_each_header(event, http_v3_cloudflare_quiche::_grab_headers, client.get());

                        client->request_data.body_index = 0;
                        client->request_data.body_index = 0;
                        client->request_data.uri = client->request_data.headers[":path"];
                        client->request_data.headers_size = 0;
                        client->request_data.divided = -1;
                        client->request_data.http = http::versions::HTTP_v3;

                        client->request_data.has_body = manapi_quiche_h3_event_headers_has_more_frames_(event);

                        if (client->request_data.has_body) {
                            auto contentlength = client->request_data.headers.find(http::HEADER.CONTENT_LENGTH);
                            client->request_data.headers_part = 0;
                            client->request_data.body_part = 0;
                            client->request_data.body_size = contentlength != client->request_data.headers.end()
                                ? std::stoll(contentlength->second) : -1 /* The size isn't fixed */;
                        }
                        else {
                            client->request_data.body_size = 0;
                            client->request_data.body_part = 0;
                        }
                        client->request_data.body_left = client->request_data.body_size;
                        client->request_data.method = client->request_data.headers[":method"];

                        async::run(connection->as<connection_t>().worker->site.async_context(), [stream_id, connection, client = std::move(client)] () mutable -> future<> {
                            auto &ctx = connection->as<connection_t>().worker->site.async_context();

                            try {
                                if (co_await client->parse_request(0, 0)) {
                                    co_await client->execute_handler();
                                }
                            }
                            catch (std::exception const &e) {
                                /* error */
                                MANAPIHTTP_LOG(ctx, "Unexpected exception: {}", e.what());
                            }

                            co_await ctx->eventloop()->custom_callback(
                                [connection, stream_id] (manapi::event_loop *ev) -> void {
                                    auto &conn = connection->as<connection_t>();
                                    auto &parent = connection->as<connection_t>();
                                    parent.flags |= CONN_REVIEW_STREAMS;
                                    parent.closed_streams.push_back(stream_id);
                                    parent.write_watcher.send();
                            });

                            // auto &conn_data = client->connection->as<connection_t>();
                            // conn_data.write_watcher.send()
                            // conn_data.status.fetch_or(CONN_HALF_CLOSED);
                        });

                        break;
                    }
                    case QUICHE_H3_EVENT_DATA: {
                        auto stream_it = conn_data.streams.find(stream_id);
                        if (stream_it == conn_data.streams.end()) {
                            break;
                        }

                        auto &stream_connection = stream_it->second;
                        auto &stream = stream_connection->as<connection_stream_t>();

                        _flush_read_stream(stream.connection->as<connection_t>(), stream_it);

                        // skip

                        break;
                    }
                    case QUICHE_H3_EVENT_FINISHED: {
                        auto stream_it = conn_data.streams.find(stream_id);
                        if (stream_it == conn_data.streams.end()) {
                            break;
                        }

                        auto &stream_connection = stream_it->second;
                        auto &stream = stream_connection->as<connection_stream_t>();

                        stream.status2 |= STREAM_CONN_RECV_END;

                        _flush_read_stream(stream.connection->as<connection_t>(), stream_it);
                        break;
                    }
                    case QUICHE_H3_EVENT_RESET: {
                        auto stream_it = conn_data.streams.find(stream_id);
                        if (stream_it == conn_data.streams.end()) {
                            break;
                        }
                        auto &stream = stream_it->second->as<connection_stream_t>();

                        this->_stream_close(stream);

                        break;
                    }
                    case QUICHE_H3_EVENT_PRIORITY_UPDATE: {
                        break;
                    }
                    case QUICHE_H3_EVENT_GOAWAY: {
                        http_v3_cloudflare_quiche::_reset_all_streams(conn_data);

                        break;
                    }
                }

                quiche_h3_event_free(event);
            }
        }

        /* setup timeout */
        auto connection_it = this->connections.find(dcid);
        if (connection_it != this->connections.end()) {
            this->_quiche_flush_egress(conn_data);
            http_v3_cloudflare_quiche::_quiche_timeout_again(conn_data);
            http_v3_cloudflare_quiche::_flush_connection_closed(conn_data);
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::init() {
    udp::init();

    this->_quiche_config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    this->_quiche_h3_config = quiche_h3_config_new();

    auto ssl_config = this->config->get_ssl_config();

    if (!ssl_config->enabled) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_CONFIG_ERROR, "QUICHE: QUIC requires SSL be enabled");
    }

    if (0 != quiche_config_load_cert_chain_from_pem_file(this->_quiche_config, ssl_config->cert.data())) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load cert chain from pem file: {}", ssl_config->cert);
    }

    if (0 != quiche_config_load_priv_key_from_pem_file(this->_quiche_config, ssl_config->key.data())) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load priv key from pem file: {}", ssl_config->key);
    }

    quiche_config_set_application_protos(this->_quiche_config, reinterpret_cast <const uint8_t *> (QUICHE_H3_APPLICATION_PROTOCOL), sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1);
    quiche_config_set_max_idle_timeout(this->_quiche_config, 5000);
    quiche_config_set_max_recv_udp_payload_size(this->_quiche_config, MANAPI_MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(this->_quiche_config, MANAPI_MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(this->_quiche_config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(this->_quiche_config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(this->_quiche_config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(this->_quiche_config, 1000000);
    quiche_config_set_initial_max_streams_bidi (this->_quiche_config, 100);
    quiche_config_set_initial_max_streams_uni (this->_quiche_config, 100);
    //quiche_config_set_disable_active_migration (this->_quiche_config, true);
    quiche_config_verify_peer(this->_quiche_config, this->config->get_verify_peer());
    if (this->config->is_quic_debug()) {
        quiche_enable_debug_logging([] (const char *line, void *argp)
            -> void {
            MANAPIHTTP_LOG2((static_cast<http_v3_cloudflare_quiche*>(argp))->site.async_context(), line);
        }, this);
    }

    if (this->config->get_quic_cc_algo().load() != http::versions::QUIC_CC_NONE) {
        quiche_cc_algorithm algo = QUICHE_CC_RENO;

        switch (this->config->get_quic_cc_algo().load())
        {
            case http::versions::QUIC_CC_CUBIC:   algo = QUICHE_CC_CUBIC;     break;
            case http::versions::QUIC_CC_RENO:    algo = QUICHE_CC_RENO;      break;
            case http::versions::QUIC_CC_BBR:     algo = QUICHE_CC_BBR;       break;
            case http::versions::QUIC_CC_BBR2:    algo = QUICHE_CC_BBR2;      break;
            default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo: {}",
                    static_cast<int>(this->config->get_quic_cc_algo().load()));
        }

        quiche_config_set_cc_algorithm (this->_quiche_config, algo);
    }

    this->gbuffer_size = MANAPI_MAX_DATAGRAM_SIZE;
    this->gbuffer.reserve(this->gbuffer_size);

    this->write = http_v3_cloudflare_quiche::default_write;
    this->read = http_v3_cloudflare_quiche::default_read;

    /* every 1 second */
    this->limit_rate_timer = this->site.async_context()->timerpool()->append_interval_sync(1000,
        [this] (manapi::timer t) -> void { this->update_limit_rate(); });
}

std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche> manapi::net::worker::http_v3_cloudflare_quiche::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::http_v3_cloudflare_quiche>(site);
    worker->set_config(std::move(config));
    worker->new_dependency = [worker = std::weak_ptr<worker::http_v3_cloudflare_quiche> (worker)] () -> std::shared_ptr<worker::http_v3_cloudflare_quiche> {
          return std::shared_ptr<worker::http_v3_cloudflare_quiche> (worker);
    };
    return std::move(worker);
}

manapi::future<ssize_t> manapi::net::worker::http_v3_cloudflare_quiche::response(worker::connection &connection, http::response &resp, bool finish) {
    auto &stream_data = connection.as<connection_stream_t>();
    auto &conn_data = stream_data.connection->as<connection_t>();

    stream_data.headers = std::make_unique<std::map<std::string, std::string>>(std::move(resp.headers()));
    stream_data.quiche_headers.reset(new quiche_h3_header[stream_data.headers->size() + 1]);
    stream_data.headers_size = stream_data.headers->size() + 1;
    size_t i = 1;

    const size_t max_header_value_len = 200;
    for (auto header = stream_data.headers->begin(); header != stream_data.headers->end(); ++header) {
        size_t header_value_cursor = 0;
        while (true) {
            if (header->first.size() > max_header_value_len) {
                THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "header key is too long. Size: {}", header->first.size());
            }
            auto len = std::min(max_header_value_len - header->first.size(), header->second.size() - header_value_cursor);
            http_v3_cloudflare_quiche::_quiche_set_header(stream_data.quiche_headers.get()[i++], header->first, std::string_view{header->second.data() + header_value_cursor, len});
            header_value_cursor += len;
            if (header_value_cursor == header->second.size()) {
                break;
            }
            ++stream_data.headers_size;
            auto nheaders = static_cast<quiche_h3_header *>(realloc(stream_data.quiche_headers.release(), sizeof (quiche_h3_header) * stream_data.headers_size));
            if (nheaders) {
                stream_data.quiche_headers.reset(nheaders);
            }
            else {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "failed to realloc(...) headers buffer");
            }
        }
    }

    i = 0;

    auto &status_header = stream_data.headers->operator[](":status");
    status_header = std::to_string(resp.status_code());
    http_v3_cloudflare_quiche::_quiche_set_header(stream_data.quiche_headers.get()[i++], ":status", status_header);

    if (stream_data.atomic_status & STREAM_ATOMIC_CONN_CLOSED) {
        co_return -1;
    }

    co_await stream_data.mx.lock();

    stream_data.atomic_status.fetch_or(STREAM_ATOMIC_CONN_HEADERS);
    stream_data.connection->as<connection_t>().write_watcher.send();

    co_return static_cast<ssize_t>(1);
}

void manapi::net::worker::http_v3_cloudflare_quiche::stop() {
    this->limit_rate_timer.sync_stop(this->site.async_context());
}

manapi::net::worker::http_v3_cloudflare_quiche * manapi::net::worker::http_v3_cloudflare_quiche::_get_dynamic_worker(
    const std::shared_ptr<worker::base> &w) {
    return dynamic_cast<http_v3_cloudflare_quiche *> (w.get());
}

void manapi::net::worker::http_v3_cloudflare_quiche::_flush_write_stream(connection_t &conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it, bool app) {
    auto &s = stream_it->second->as<connection_stream_t>();

    while (true) {
        bool finish;

        if (app) {
            if (s.atomic_status & STREAM_ATOMIC_CONN_HEADERS) {
                finish = s.atomic_status & STREAM_ATOMIC_CONN_SEND_END;

                if (finish) {
                    s.status2 |= STREAM_CONN_SEND_END;
                }

                size_t current_headers_size = 0;
                size_t max_headers_size = 500;

                for (size_t i = s.header_cursor; i <= s.headers_size; ) {
                    if (i < s.headers_size) {
                        auto header = s.quiche_headers.get()[i];

                        if (current_headers_size + header.name_len + header.value_len < max_headers_size) {
                            current_headers_size += header.name_len + header.value_len;
                            ++i;

                            continue;
                        }
                    }

                    int rhs;
                    auto headers = s.quiche_headers.get() + s.header_cursor;
                    if (s.header_cursor != 0) {
                        if constexpr (version_greater_or_equals(MANAPIHTTP_QUICHE_VERSION, "0.23.0")) {
                            rhs = manapi_quiche_h3_send_additional_headers_(conn_data.http3_conn, conn_data.conn, s.stream_id, headers, i - s.header_cursor, false, finish && i == s.headers_size);
                        }
                        else {
                            rhs = 0;
                        }
                    }
                    else {
                        rhs = quiche_h3_send_response(conn_data.http3_conn, conn_data.conn, s.stream_id, headers, i - s.header_cursor, finish && i == s.headers_size);
                    }

                    if (rhs == 0) {
                        conn_data.write_total += std::exchange(current_headers_size, 0);
                        s.header_cursor = i;
                    }
                    else if (rhs == QUICHE_H3_ERR_STREAM_BLOCKED) {
                        break;
                    }
                    else {
                        s.atomic_status.store(STREAM_ATOMIC_CONN_CLOSED);
                        s.mx.unlock();

                        return;
                    }

                    http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->_quiche_flush_egress(conn_data);

                    if (i == s.headers_size) {

                        break;
                    }
                }

                if (s.header_cursor == s.headers_size) {
                    s.header_cursor = 0;
                    s.headers_size = 0;

                    s.quiche_headers = nullptr;
                    s.headers = nullptr;
                    s.headers_size = 0;

                    //std::cout << "HEADERS " << (finish ? "YES\n" : "NO\n");
                    s.atomic_status.fetch_xor(STREAM_ATOMIC_CONN_HEADERS);
                    s.mx.unlock();
                    //quiche_conn_stream_shutdown(conn_data.conn, s.stream_id, QUICHE_SHUTDOWN_READ, 0);
                }
            }

            if (s.atomic_status & STREAM_ATOMIC_CONN_WRITE) {
                    //std::cout << "WRITE\n";
                finish = s.atomic_status & STREAM_ATOMIC_CONN_SEND_END;

                const ssize_t max_buffer_size = std::max(100000L, static_cast<ssize_t>(s.write_buffer2->size()));

                if (s.flush_write_total_size + s.write_buffer2->size() <= max_buffer_size) {
                    auto worker = http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker);

                    if (finish) {
                        s.status2 |= STREAM_CONN_SEND_END;
                    }

                    ssize_t rhs;

                    if (s.flush_write_buffer) {
                        rhs = 0;
                    }
                    else {
                        auto copy = std::min(static_cast<ssize_t>(s.write_buffer2->size()), static_cast<ssize_t>(worker->config->speed_limit_rate().load() - conn_data.transfared_last_second));
                        if (finish) { finish = copy == s.write_buffer2->size(); }
                        if (copy > 0 || (finish)) {
                            rhs = quiche_h3_send_body(conn_data.http3_conn, conn_data.conn, s.stream_id,
                                reinterpret_cast <const uint8_t *>(s.write_buffer2->data()), copy, finish);
                        }
                        else {
                            rhs = 0;
                        }

                        if (rhs < 0) {
                            if (rhs == QUICHE_H3_ERR_DONE) {
                                /* no space left */
                                rhs = 0;
                            }
                            else {
                                /* error */
                                s.atomic_status.store(STREAM_ATOMIC_CONN_CLOSED);
                                s.mx.unlock();

                                return;
                            }
                        }
                        else {
                            conn_data.transfared_last_second += rhs;
                        }

                        worker->_quiche_flush_egress(conn_data);
                    }

                    while (rhs != s.write_buffer2->size()) {
                        http_v3_cloudflare_quiche::init_write_buffer(s);
                        const auto copy = std::min(static_cast<ssize_t>(s.flush_write_last->buffer->size() - s.flush_write_cursor), static_cast<ssize_t>(s.write_buffer2->size()) - rhs);
                        memcpy(s.flush_write_last->buffer->data() + s.flush_write_cursor, s.write_buffer2->data() + rhs, copy);
                        s.flush_write_cursor += copy;
                        s.flush_write_total_size += copy;
                        rhs += copy;
                    }

                    s.atomic_status.fetch_xor(STREAM_ATOMIC_CONN_WRITE);
                    s.mx.unlock();
                }
            }
        }



        if (s.flush_write_last) {
            auto worker = http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker);
            finish = s.status2 & STREAM_CONN_SEND_END;

            int repeat = 0;

            while (s.flush_write_last) {
                auto size = static_cast<int>(s.flush_write_buffer->buffer->size());
                if (!s.flush_write_buffer->next) { size = std::min(size, s.flush_write_cursor); }
                else {
                    if (finish) { finish = false; }
                }

                auto copy = static_cast<ssize_t>(size - s.flush_write_current);
                if (copy) {
                    copy = std::min(copy, static_cast<ssize_t>(worker->config->speed_limit_rate().load() - conn_data.transfared_last_second));
                    ssize_t rhs = 0;
                    if (copy > 0) {
                        rhs = quiche_h3_send_body(conn_data.http3_conn, conn_data.conn, s.stream_id,
                            reinterpret_cast <const uint8_t *>(s.flush_write_buffer->buffer->data()) + s.flush_write_current, copy, finish && (s.flush_write_current + copy == size));
                    }
                    else {
                        rhs = 0;
                    }

                    if (rhs < 0) {
                        if (rhs == QUICHE_H3_ERR_DONE) {
                            /* no space left */
                            rhs = 0;
                            if (repeat++ <= 0) {
                                worker->_quiche_flush_egress(conn_data);
                                continue;
                            }
                        }
                        else {
                            /* error */
                            s.atomic_status.store(STREAM_ATOMIC_CONN_CLOSED);
                            s.mx.unlock();
                            return;
                        }
                    }

                    if (rhs == 0) {
                        break;
                    }

                    repeat = 0;

                    /* great ! */
                    conn_data.transfared_last_second += rhs;
                    s.flush_write_current += rhs;
                    s.flush_write_total_size -= rhs;
                }

                if (s.flush_write_current == size) {
                    s.flush_write_current = 0;
                    /* end of buffer */
                    if (!s.flush_write_buffer->next) {
                        s.flush_write_buffer = nullptr;
                        s.flush_write_last = nullptr;
                        s.flush_write_cursor = 0;
                        break;
                    }

                    auto b = std::move(s.flush_write_buffer->next);
                    s.flush_write_buffer = std::move(b);
                }
            }

            if (!app && !s.flush_write_last) {
                /* force grab user buffers */
                app = true;
                continue;
            }
        }

        break;
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::_flush_read_stream(connection_t &conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it) {
    auto &s = stream_it->second->as<connection_stream_t>();

    if (s.atomic_status & STREAM_ATOMIC_CONN_READ) {
        auto &conn = s.connection->as<connection_t>();
        auto worker = http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker);

        if (!s.read_buffer2) {
            s.read_buffer2 = worker->site.bufferpool()->get();
            s.read_buffer2->resize(worker->buffer_size());
            s.flush_read_cursor = 0;
        }

       // std::cout << "READ\n";

        bool done = false;
        while(s.read_buffer2->size() != s.flush_read_cursor) {
            auto copy = std::min(static_cast<ssize_t>(s.read_buffer2->size() - s.flush_read_cursor), static_cast<ssize_t>(worker->config->speed_limit_rate().load() - conn.transfared_last_second));
            ssize_t rhs;

            if (copy > 0) {
                rhs = ::quiche_h3_recv_body(conn_data.http3_conn, conn_data.conn, s.stream_id, reinterpret_cast <uint8_t *>(s.read_buffer2->data()) + s.flush_read_cursor, copy);
            }
            else {
                rhs = 0;
            }

            if (rhs < 0) {
                if (rhs == QUICHE_ERR_DONE) {
                    done = true;
                    break;
                }
                else {
                    /* error */
                    s.atomic_status.store(STREAM_ATOMIC_CONN_CLOSED);
                    s.mx.unlock();
                    return;
                }
            }

            if (rhs == 0) {
                break;
            }


            conn.transfared_last_second += rhs;
            s.flush_read_cursor += rhs;
            conn_data.read_total += rhs;

            //printf("DATA %zi\n", rhs);

            worker->_quiche_flush_egress(conn_data);
        }

        if ((done && (s.status2 & STREAM_CONN_RECV_END))) {
            s.atomic_status.fetch_or(STREAM_ATOMIC_CONN_RECV_END);

            s.atomic_status.fetch_xor(STREAM_ATOMIC_CONN_READ);
            s.mx.unlock();
        }
        else if (s.flush_read_cursor == s.read_buffer2->size()) {
            s.atomic_status.fetch_xor(STREAM_ATOMIC_CONN_READ);
            s.mx.unlock();
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::_flush_write(connection_t &conn_data, bool app) {
    http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->_quiche_flush_egress(conn_data);
    /* send data */
    int64_t stream_id;
    quiche_stream_iter *stream = quiche_conn_writable(conn_data.conn);
    while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
        auto stream_it = conn_data.streams.find(stream_id);
        //MANAPIHTTP_LOG("WRITE STREAM:{}", stream_id);
        if (stream_it != conn_data.streams.end()) {
            http_v3_cloudflare_quiche::_flush_write_stream(conn_data, stream_it, app);
        }
    }
    quiche_stream_iter_free(stream);
}

void manapi::net::worker::http_v3_cloudflare_quiche::_flush_read(connection_t &conn_data) {
    http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->_quiche_flush_egress(conn_data);

    /* recv data */
    int64_t stream_id;
    quiche_stream_iter *stream = quiche_conn_readable(conn_data.conn);
    while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
        auto stream_it = conn_data.streams.find(stream_id);
        //MANAPIHTTP_LOG("READ STREAM:{}", stream_id);
        if (stream_it != conn_data.streams.end()) {
            _flush_read_stream(conn_data, stream_it);
        }
    }
    quiche_stream_iter_free(stream);
}

void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_set_header(quiche_h3_header &header, std::string_view key, std::string_view value) {
    header = {
        .name = reinterpret_cast<const uint8_t *> (key.data()),
        .name_len = key.size(),
        .value = reinterpret_cast<const uint8_t *> (value.data()),
        .value_len = value.size()
    };
}

void manapi::net::worker::http_v3_cloudflare_quiche::_stream_close(connection_stream_t &stream) {
    stream.atomic_status.store(STREAM_ATOMIC_CONN_CLOSED);
    stream.mx.unlock();
}

void manapi::net::worker::http_v3_cloudflare_quiche::_write_watcher_cb(struct ev_loop *loop, ev_async *w, int revents) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (w->data);
    auto &conn_data = connection->as<connection_t>();

    http_v3_cloudflare_quiche::_flush_read(conn_data);
    http_v3_cloudflare_quiche::_flush_write(conn_data, true);


    if (conn_data.flags & CONN_REVIEW_STREAMS) {
        for (auto id = conn_data.closed_streams.begin(); id != conn_data.closed_streams.end(); ) {
            auto stream = conn_data.streams.find(*id);
            if (stream != conn_data.streams.end()) {
                if (false == (stream->second->as<connection_stream_t>().atomic_status
                    & (STREAM_ATOMIC_CONN_WRITE | STREAM_ATOMIC_CONN_READ | STREAM_ATOMIC_CONN_HEADERS))) {
                    stream = conn_data.streams.erase(stream);

                    if (conn_data.streams.empty() && conn_data.status & CONN_CLOSED) {
                        http_v3_cloudflare_quiche::_force_close(conn_data);
                        return;
                    }
                }
                else {
                    ++id;
                    continue;
                }
            }

            *id = conn_data.closed_streams.back();
            conn_data.closed_streams.pop_back();
        }
    }

    /* flush I/O */

    http_v3_cloudflare_quiche::_quiche_timeout_again(conn_data);
    http_v3_cloudflare_quiche::_flush_connection_closed(conn_data);
}

void manapi::net::worker::http_v3_cloudflare_quiche::_force_close(connection_t &conn_data) {
    auto connection = *static_cast<std::shared_ptr<worker::connection> *> (conn_data.quiche_timer.data);

    if (quiche_conn_is_closed(conn_data.conn)) {
        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(conn_data.conn, &stats);
        quiche_conn_path_stats(conn_data.conn, 0, &path_stats);

        fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu\n",
                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->connections.erase(conn_data.cid);
        conn_data.quiche_timer.stop();
        //conn_data.timer.stop();
        conn_data.write_watcher.stop();
        conn_data.io_timer.sync_stop(conn_data.worker->site.async_context());

        delete static_cast<std::shared_ptr<worker::connection> *> (std::exchange(conn_data.quiche_timer.data, nullptr));

        if (conn_data.http3_conn) {
            quiche_h3_conn_free(std::exchange(conn_data.http3_conn, nullptr));
        }
        quiche_conn_free(std::exchange(conn_data.conn, nullptr));
    }
    else {
        if ((conn_data.status & CONN_HALF_CLOSED) == 0) {
            conn_data.status|=CONN_HALF_CLOSED;
            /** refuse connection */
            quiche_conn_close(conn_data.conn, true, 0x02, reinterpret_cast <const uint8_t *> ("i/o timeout"), sizeof ("i/o timeout") - 1);
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::_flush_connection_closed(connection_t &conn_data) {
    // if (!(conn_data.status & CONN_CLOSED) && (conn_data.status & CONN_HALF_CLOSED) && conn_data.streams.empty()) {
    //     conn_data.status.fetch_xor(CONN_HALF_CLOSED);
    // }

    if (quiche_conn_is_closed(conn_data.conn)) {
        if (!(conn_data.status & CONN_CLOSED)) {
            conn_data.status|=(CONN_CLOSED);
            if (conn_data.streams.size()) {
                http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->_reset_all_streams(conn_data);
            }
            else {
                _force_close(conn_data);
            }
        }
        else {
            if (conn_data.streams.empty()) {
                /* retry */
                _force_close(conn_data);
            }
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::_clean_connection(void *conn_data) {
    std::cout << "CONN CLOSE\n";
    delete static_cast<connection_t*>(conn_data);
}

void manapi::net::worker::http_v3_cloudflare_quiche::init_write_buffer(connection_stream_t &s) {
    if (!s.flush_write_last || s.flush_write_last->buffer->size() == s.flush_write_cursor) {
        /* buffer size must be greater than the max frame size! */
        auto bwrite = s.connection->as<connection_t>().worker->site.bufferpool()->get();
        bwrite->resize(dynamic_cast<http_v3_cloudflare_quiche*>(s.connection->as<connection_t>().worker.get())->buffer_size());
        if (s.flush_write_last) {
            s.flush_write_last->next = std::make_unique<buffer_stack_t>(std::move(bwrite), nullptr);
            s.flush_write_last = s.flush_write_last->next.get();
        }
        else {
            if (!s.flush_write_buffer) {
                s.flush_write_buffer = std::make_unique<buffer_stack_t>(std::move(bwrite), nullptr);
            }
            s.flush_write_last = s.flush_write_buffer.get();
        }
        s.flush_write_cursor = 0;
    }
}

ssize_t manapi::net::worker::http_v3_cloudflare_quiche::buffer_size() {
    return this->config->buffer_size();
}

void manapi::net::worker::http_v3_cloudflare_quiche::_reset_all_streams(connection_t &conn_data) {
    for (auto &stream: conn_data.streams) {
        this->_stream_close(stream.second->as<connection_stream_t>());
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::_io_timeout(connection_t &conn_data) {
    // if (conn_data.stream_read_cnt > 0) {
    //     if (conn_data.read_total - conn_data.read_total_prev < conn_data.worker->config->speed_check_bytes()) {
    //         //conn_data.status.fetch_or(CONN_CLOSED);
    //         _force_close(conn_data);
    //         return;
    //     }
    // }
    //
    // if (conn_data.stream_write_cnt > 0) {
    //     if (conn_data.write_total - conn_data.write_total_prev < conn_data.worker->config->speed_check_bytes()) {
    //         //conn_data.status.fetch_or(CONN_CLOSED);
    //         _force_close(conn_data);
    //         return;
    //     }
    // }

    conn_data.read_total_prev = conn_data.read_total;
    conn_data.write_total_prev = conn_data.write_total;
}

void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_timeout(struct ev_loop *loop, ev_timer *w, int revents) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (w->data);
    auto &conn_data = connection->as<connection_t>();

    quiche_conn_on_timeout(conn_data.conn);

    http_v3_cloudflare_quiche::_get_dynamic_worker(conn_data.worker)->_quiche_flush_egress(conn_data);
    http_v3_cloudflare_quiche::_quiche_timeout_again(conn_data);
    http_v3_cloudflare_quiche::_flush_connection_closed(conn_data);
}

int manapi::net::worker::http_v3_cloudflare_quiche::_grab_headers(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) {
    auto client = static_cast<http::http_v2 *> (argp);
    client->request_data.headers[std::string{reinterpret_cast<const char *>(name), name_len}] += std::string_view{reinterpret_cast<const char *>(value), value_len};
    return 0;
}

bool manapi::net::worker::http_v3_cloudflare_quiche::_validate_token(std::string_view token, std::string &odcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len) {
    if (!token.starts_with("quiche")
        || token.substr(sizeof("quiche") - 1, sockaddr_len) != std::string_view{reinterpret_cast<const char *> (&sockaddr_src), sockaddr_len}){
        return false;
    }

    odcid = token.substr(sizeof("quiche") - 1 + sockaddr_len);
    return true;
}

std::string manapi::net::worker::http_v3_cloudflare_quiche::_gen_mint_token(std::string_view dcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len) {
    std::string token;
    token.reserve(sizeof("quiche")-1 + sockaddr_len + dcid.size());
    token += "quiche";
    token += std::string_view{reinterpret_cast<const char *>(&sockaddr_src), sockaddr_len};
    token += dcid;
    return std::move(token);
}

void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_flush_egress(connection_t &connection) {
    if (connection.status & CONN_CLOSED) {
        return;
    }

    uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(connection.conn, out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            /* done writing */
            break;
        }

        if (written < 0) {
            /* failed to create packet */
            return;
        }

        ssize_t sent = ::sendto(this->fd, out, written, 0x00, reinterpret_cast<sockaddr *>(&send_info.to), send_info.to_len);

        if (sent != written) {
            /* failed to send */
            return;
        }
    }

}

void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_timeout_again(connection_t &connection) {
    connection.quiche_timer.repeat = static_cast<double>(quiche_conn_timeout_as_nanos(connection.conn)) / 1e9f;
    connection.quiche_timer.again();
}

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate() {
    /* in the event loop */
    for (auto &conn: this->connections) {
        this->update_limit_rate_connection(*conn.second);
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate_connection(connection &conn) {
    auto &conn_data = conn.as<connection_t>();
    conn_data.transfared_last_second = 0;

    _flush_write(conn_data, false);
    _flush_read(conn_data);
}

manapi::future<ssize_t> manapi::net::worker::http_v3_cloudflare_quiche::default_write(connection &conn, const void *buf, ssize_t size, bool flag) {
    auto &stream_data = conn.as<connection_stream_t>();
    ssize_t rhs = 0;
    while (size != rhs) {
        if (stream_data.atomic_status & STREAM_ATOMIC_CONN_CLOSED) {
            co_return -1;
        }

        if (!stream_data.write_buffer) {
            auto &connection = stream_data.connection->as<connection_t>();
            stream_data.write_buffer = connection.worker->site.bufferpool()->get();
            stream_data.write_buffer->resize(dynamic_cast<http_v3_cloudflare_quiche *>(connection.worker.get())->buffer_size());
        }

        auto copy = std::min(size - rhs, static_cast<ssize_t>(stream_data.write_buffer->size()) - stream_data.write_cursor);
        memcpy (stream_data.write_buffer->data() + stream_data.write_cursor, static_cast<const char *>(buf) + rhs, copy);

        stream_data.write_cursor += copy;
        rhs += copy;

        const bool t_flag = (flag && rhs == size);
        if (stream_data.write_cursor == stream_data.write_buffer->size() || t_flag) {
            //std::cout << "WANT WRITE\n";
            co_await stream_data.mx.lock();
            //std::cout << "READY WRITE\n";

            if (t_flag) {
                stream_data.atomic_status.fetch_or(STREAM_ATOMIC_CONN_SEND_END);
            }

            stream_data.write_buffer->resize(stream_data.write_cursor);
            stream_data.write_buffer2 = std::move(stream_data.write_buffer);
            stream_data.write_cursor = 0;

            stream_data.atomic_status.fetch_or(STREAM_ATOMIC_CONN_WRITE);
            stream_data.connection->as<connection_t>().write_watcher.send();

            continue;
        }

        break;
    }
    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::http_v3_cloudflare_quiche::default_read(connection &conn, void *buf, ssize_t size) {
    auto &stream_data = conn.as<connection_stream_t>();

    ssize_t rhs = 0;

    while (true) {
        if (stream_data.atomic_status & STREAM_ATOMIC_CONN_CLOSED) {
            co_return -1;
        }

        if ((!stream_data.read_buffer || stream_data.read_buffer->size() == stream_data.read_cursor)) {
            if (stream_data.status & STREAM_CONN_RECV_END) { /* eos was reached */ break; }

            co_await stream_data.mx.lock();

            stream_data.read_buffer = std::move(stream_data.read_buffer2);
            if (stream_data.read_buffer) {
                stream_data.read_buffer->resize(stream_data.flush_read_cursor);
            }
            stream_data.read_cursor = 0;
            stream_data.flush_read_cursor = 0;

            //std::cout << "WANT READ\n";

            if (stream_data.atomic_status & STREAM_ATOMIC_CONN_RECV_END) {
                stream_data.status |= STREAM_CONN_RECV_END;
                stream_data.mx.unlock();
            }
            else {
                stream_data.atomic_status.fetch_or(STREAM_ATOMIC_CONN_READ);
                stream_data.connection->as<connection_t>().write_watcher.send();
            }

            continue;
        }

        auto copy = std::min(size, static_cast<ssize_t>(stream_data.read_buffer->size() - stream_data.read_cursor));
        memcpy (buf, stream_data.read_buffer->data(), copy);

        stream_data.read_cursor += copy;
        rhs += copy;

        break;
    }

    co_return rhs;
}

#endif
