#include "worker/HTTPv3_tquic.hpp"

#include <memory>

#include "http/HTTPv2.hpp"

#include "crypto/ManapiAEAD.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

# define MANAPI_MAX_DATAGRAM_SIZE 1350
# define MANAPI_QUICHE_CONNECTION_ID_LEN 16

http3_methods_t manapi::net::worker::http_v3_tquic::http3_methods = {
    .on_stream_headers = tquic_http3_on_stream_headers,
    .on_stream_data = tquic_http3_on_stream_data,
    .on_stream_finished = tquic_http3_on_stream_finished,
    .on_stream_reset = tquic_http3_on_stream_reset,
    .on_stream_priority_update = tquic_http3_on_stream_priority_update,
    .on_conn_goaway = tquic_http3_on_conn_goaway,
};

manapi::net::worker::http_v3_tquic::http_v3_tquic(net::site &site) : udp(site) {

}

manapi::net::worker::http_v3_tquic::~http_v3_tquic() {
    this->timeout->stop();

    quic_endpoint_free(this->_quic_server);
    quic_config_free(this->_quic_config);
    quic_tls_config_free(this->_quic_tls_config);
    http3_config_free(this->_quic_h3_config);
}

void manapi::net::worker::http_v3_tquic::onrecv(ev::io &watcher, int revents) {
    sockaddr_storage sockaddr_src{};
    socklen_t sockaddr_len = sizeof (sockaddr_src);
    uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];

    std::shared_ptr<worker::connection> connection;

    auto rhs = ::recvfrom(this->fd, this->gbuffer.data(), this->gbuffer.size(), 0x00, reinterpret_cast<sockaddr *> (&sockaddr_src), &sockaddr_len);

    if (rhs <= 0) {
        if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
            return;;
        }
        /* socker error */

        return;
    }

    quic_packet_info_t packet_info {
        .src = reinterpret_cast<const sockaddr *> (&sockaddr_src),
        .src_len = sockaddr_len,
        .dst = reinterpret_cast<const sockaddr *>(&*this->config->get_server_address()),
        .dst_len = this->config->get_server_len()
    };

    auto done = quic_endpoint_recv(this->_quic_server, reinterpret_cast<uint8_t *>(this->gbuffer.data()), this->gbuffer.size(), &packet_info);

    if (done < 0) {
        /* error */

    }

    this->_quic_process_connections();
}

void manapi::net::worker::http_v3_tquic::init() {
    udp::init();

    this->_quic_config = quic_config_new();
    this->_quic_h3_config = http3_config_new();
    this->_quic_tls_config = quic_tls_config_new();

    auto ssl_config = this->config->get_ssl_config();

    if (!ssl_config->enabled) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_CONFIG_ERROR, "QUICHE: QUIC requires SSL be enabled");
    }

    if (0 != quic_tls_config_set_certificate_file(this->_quic_tls_config, ssl_config->cert.data())) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load cert chain from pem file: {}", ssl_config->cert);
    }

    if (0 != quic_tls_config_set_private_key_file(this->_quic_tls_config, ssl_config->key.data())) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load priv key from pem file: {}", ssl_config->key);
    }
    char *application_protos[] = {application_h3_proto.data()};
    quic_tls_config_set_application_protos(this->_quic_tls_config, application_protos, sizeof(application_protos));

    quic_config_set_max_idle_timeout(this->_quic_config, 5000);
    quic_config_set_recv_udp_payload_size(this->_quic_config, MANAPI_MAX_DATAGRAM_SIZE);
    quic_config_set_send_udp_payload_size(this->_quic_config, MANAPI_MAX_DATAGRAM_SIZE);
    quic_config_set_initial_max_data (this->_quic_config, 1102410240);
    quic_config_set_initial_max_stream_data_bidi_local (this->_quic_config, 1102410240);
    quic_config_set_initial_max_stream_data_bidi_remote (this->_quic_config, 1102410240);
    quic_config_set_initial_max_stream_data_uni (this->_quic_config, 1102410240);
    quic_config_set_initial_max_streams_bidi (this->_quic_config, 100);
    quic_config_set_initial_max_streams_uni (this->_quic_config, 100);
    quic_tls_config_set_early_data_enabled (this->_quic_tls_config, true);
    quic_config_enable_retry(this->_quic_config, true);
    quic_config_set_max_handshake_timeout(this->_quic_config, 10000);
    quic_config_set_pto_linear_factor(this->_quic_config, 10);
    quic_config_set_send_batch_size(this->_quic_config, 16);
    quic_config_set_min_congestion_window(this->_quic_config, 4);
    quic_config_set_initial_rtt(this->_quic_config, 333);
    quic_config_set_max_pto(this->_quic_config, 10000);
    quic_config_set_cid_len(this->_quic_config, 12);
    quic_config_set_anti_amplification_factor(this->_quic_config, 3);
    quic_config_set_zerortt_buffer_size(this->_quic_config, 1000);
    quic_config_set_initial_congestion_window(this->_quic_config, 1000);
    quic_config_set_active_connection_id_limit(this->_quic_config, 2);
    quic_config_enable_encryption(this->_quic_config, true);

    // quiche_enable_debug_logging([] (const char *line, void *argp) -> void {
    //     MANAPIHTTP_LOG("quiche: {}", line);
    // }, nullptr);

    if (this->config->get_quic_cc_algo().load() != http::versions::QUIC_CC_NONE) {
        quic_congestion_control_algorithm algo;

        switch (this->config->get_quic_cc_algo().load())
        {
            case http::versions::QUIC_CC_CUBIC:   algo = QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC;     break;
            case http::versions::QUIC_CC_BBR:     algo = QUIC_CONGESTION_CONTROL_ALGORITHM_BBR;       break;
            default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo: {}",
                    static_cast<int>(this->config->get_quic_cc_algo().load()));
        }

        quic_config_set_congestion_control_algorithm (this->_quic_config, algo);
    }


    quic_config_set_tls_config(this->_quic_config, this->_quic_tls_config);
    this->gbuffer.resize(MANAPI_MAX_DATAGRAM_SIZE);

    this->write = http_v3_tquic::default_write;
    this->read = http_v3_tquic::default_read;

    this->handler_methods = {
        .on_conn_created = tquic_on_conn_created,
        .on_conn_established = tquic_on_conn_established,
        .on_conn_closed = tquic_on_conn_closed,
        .on_stream_created = tquic_on_stream_created,
        .on_stream_readable = tquic_on_stream_readable,
        .on_stream_writable = tquic_on_stream_writable,
        .on_stream_closed = tquic_on_stream_closed,
        .on_new_token = tquic_on_new_token,
    };

    this->sender_methods = {
        .on_packets_send = tquic_on_packets_send
    };

    this->_quic_server = quic_endpoint_new(this->_quic_config, true,
        &this->handler_methods, this, &this->sender_methods, this);

    this->timeout = std::make_shared<ev::timer> (this->le->get_loop());
    this->timeout->repeat = 0.2;
    this->timeout->set<http_v3_tquic, &http_v3_tquic::_quic_timeout>(this);
    this->timeout->start();

    // this->process_connections = std::make_unique<ev::idle>(this->loop);
    // this->process_connections->set<http_v3_tquic, &http_v3_tquic::_quic_process_connections>(this);
    // this->process_connections->start();
}

std::shared_ptr<manapi::net::worker::http_v3_tquic> manapi::net::worker::http_v3_tquic::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::http_v3_tquic>(site);
    worker->set_config(std::move(config));
    worker->new_dependency = [worker = std::weak_ptr<worker::http_v3_tquic> (worker)] () -> std::shared_ptr<worker::http_v3_tquic> {
          return std::shared_ptr<worker::http_v3_tquic> (worker);
    };
    return std::move(worker);
}

manapi::future<ssize_t> manapi::net::worker::http_v3_tquic::response(worker::connection &connection, http_response &resp, bool finish) {
    auto &stream_data = connection.as<connection_stream_t>();
    auto &conn_data = stream_data.connection->as<connection_t>();

    auto &headers = resp.ref_headers();
    stream_data.headers = new http3_header_t[1 + headers.size()];
    stream_data.headers_size = 1 + headers.size();

    auto _quiche_headers_clean = before_delete{
        [&stream_data] () -> void { stream_data.headers_size = 0; delete std::exchange(stream_data.headers, nullptr); }};

    size_t i = 0;

    std::string status_key {":status"};
    std::string status_value {std::to_string(resp.get_status_code())};
    http_v3_tquic::_quic_set_header(stream_data.headers[i++], status_key, status_value);

    for (auto &header: headers) {
        http_v3_tquic::_quic_set_header(stream_data.headers[i++], header.first, header.second);
    }

    if (finish) {
        /* stream is finished */
        stream_data.finished = finish;
    }

    co_await connection_io_await {stream_data.handle_io, stream_data.status, CONN_IDLE, &conn_data.write_watcher};

    if (stream_data.status & CONN_CLOSED) {
        co_return -1;
    }

    if (stream_data.status & CONN_IDLE) {
        MANAPIHTTP_LOG2("BUG: stream status still CONN_IDLE after send a HTTP Response");
    }

    co_return static_cast<ssize_t>(1);
}

void manapi::net::worker::http_v3_tquic::tquic_on_conn_closed(void *tctx, quic_conn_t *conn) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    MANAPIHTTP_LOG2("conn closed");
    auto conn_it = vtctx->connections.find(quic_conn_index(conn));
    if (conn_it == vtctx->connections.end()) {
        /* wasn't setup */
        return;
    }

    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();
    vtctx->connections.erase(conn_it);
    http_v3_tquic::_flush_connection_closed(conn_data);
}

void manapi::net::worker::http_v3_tquic::tquic_on_conn_created(void *tctx, quic_conn_t *conn) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    /* pass */
    MANAPIHTTP_LOG2("conn created");
}

void manapi::net::worker::http_v3_tquic::tquic_on_conn_established(void *tctx, quic_conn_t *conn) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    vtctx->_quic_try_new_connection(conn);

    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();

    http3_conn_process_streams(conn_data.http3_conn, conn_data.conn);
}

void manapi::net::worker::http_v3_tquic::tquic_on_new_token(void *tctx, quic_conn_t *conn, const uint8_t *token, size_t token_len) {
}

void manapi::net::worker::http_v3_tquic::tquic_on_stream_closed(void *tctx, quic_conn_t *conn, uint64_t stream_id) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();
    http3_conn_process_streams(conn_data.http3_conn, conn_data.conn);

}

void manapi::net::worker::http_v3_tquic::tquic_on_stream_created(void *tctx, quic_conn_t *conn, uint64_t stream_id) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);

    // Stream may be created before connection is established because the arriving of early data.
    vtctx->_quic_try_new_connection(conn);

    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();

    const uint8_t *app_proto;
    size_t app_proto_len;
    quic_conn_application_proto(conn, &app_proto, &app_proto_len);

    MANAPIHTTP_LOG("APP PROTO: {} {}", std::string_view{reinterpret_cast<const char *>(app_proto), app_proto_len}, stream_id);

    http3_conn_process_streams(conn_data.http3_conn, conn_data.conn);
}

void manapi::net::worker::http_v3_tquic::tquic_on_stream_writable(void *tctx, quic_conn_t *conn, uint64_t stream_id) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();
    http3_conn_process_streams(conn_data.http3_conn, conn_data.conn);
    auto it = conn_data.streams.find(stream_id);
    if (it != conn_data.streams.end()) {
        http_v3_tquic::_flush_write_stream(conn_data, it);
    }

}

void manapi::net::worker::http_v3_tquic::tquic_on_stream_readable(void *tctx, quic_conn_t *conn, uint64_t stream_id) {
    auto vtctx = static_cast<http_v3_tquic *> (tctx);
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (quic_conn_context(conn));
    auto &conn_data = connection->as<connection_t>();
    http3_conn_process_streams(conn_data.http3_conn, conn_data.conn);
}

int manapi::net::worker::http_v3_tquic::tquic_on_packets_send(void *psctx, quic_packet_out_spec_t *pkts, unsigned int count) {
    auto vpsctx = static_cast<http_v3_tquic *> (psctx);
    int j = 0;
    for (; j < count; ++j) {
        for (int i = 0; i < pkts[j].iovlen; ++i) {
            ::sendto(vpsctx->fd, pkts[j].iov[i].iov_base, pkts[j].iov[i].iov_len, 0x0, static_cast<const sockaddr *> (pkts[j].dst_addr), pkts[j].dst_addr_len);
        }
    }
    return j;
}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_conn_goaway(void *ctx, uint64_t stream_id) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (ctx);
    auto &conn_data = connection->as<connection_t>();

    conn_data.worker->_reset_all_streams(conn_data);
}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_stream_data(void *ctx, uint64_t stream_id) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (ctx);
    auto &conn_data = connection->as<connection_t>();

    auto stream_it = conn_data.streams.find(stream_id);
    if (stream_it == conn_data.streams.end()) {
        return;
    }

    auto &stream_connection = stream_it->second;
    auto &stream = stream_connection->as<connection_stream_t>();

    if (stream.status & CONN_READ) {
        stream.status.fetch_xor(CONN_READ);

        do {
            auto rhs = http3_recv_body(conn_data.http3_conn, conn_data.conn, stream_id, stream.rbuff + stream.rbuff_caret, sizeof(stream.rbuff) - stream.rbuff_caret);

            if (rhs <= 0) {
                break;
            }

            stream.rbuff_caret += static_cast<int>(rhs);
            conn_data.read_total += rhs;
        }
        while (stream.rbuff_caret != sizeof (stream.rbuff));

        conn_data.worker->site.taskpool->append_task([handle = std::move(stream.handle_io)] () mutable
            -> void { handle(); });
    }
}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_stream_finished(void *ctx, uint64_t stream_id) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (ctx);
    auto &conn_data = connection->as<connection_t>();
    conn_data.status.fetch_or(CONN_HALF_CLOSED);
}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_stream_headers(void *ctx, uint64_t stream_id, const http3_headers_t *headers, bool fin) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (ctx);
    auto &conn_data = connection->as<connection_t>();

    auto worker = conn_data.worker->new_dependency();
    auto client = std::make_shared<http::http_v2>(worker, worker->config, worker->site);
    http3_for_each_header(headers, http_v3_tquic::_grab_headers, client.get());
    client->connection = std::make_shared<worker::connection>(new connection_stream_t {
        .stream_id = stream_id, .connection = connection, .status = 0x00, .handle_io = {nullptr},
        .rbuff_pos = 0, .wbuff_pos = 0, .rbuff_caret = 0, .wbuff_caret = 0, .finished = false, .headers = {nullptr}},
        [] (void *ptr) -> void { delete static_cast<connection_stream_t *> (ptr); });

    conn_data.streams[stream_id] = client->connection;

    client->request_data.body_index = 0;
    client->request_data.body_index = 0;
    client->request_data.uri = client->request_data.headers[":path"];
    client->request_data.headers_size = 0;
    client->request_data.divided = -1;
    client->request_data.http = "HTTP/3";
    client->request_data.has_body = !fin;
    client->request_data.body_left = 0;
    client->request_data.buffer.resize(worker->config->get_socket_block_size());
    if (client->request_data.has_body) {
        auto contentlength = client->request_data.headers.find(HTTP_HEADER.CONTENT_LENGTH);
        client->request_data.headers_part = 0;
        client->request_data.body_part = 0;
        client->request_data.body_size = contentlength != client->request_data.headers.end() ? std::stoll(contentlength->second) : 0;
        client->request_data.body_ptr = client->request_data.buffer.data();
    }
    else {
        client->request_data.body_size = 0;
        client->request_data.body_part = 0;
        client->request_data.body_ptr = nullptr;
    }
    client->request_data.body_left = client->request_data.body_size;
    client->request_data.method = client->request_data.headers[":method"];

    worker->site.taskpool->append_task([client, taskpool = worker->site.taskpool] () mutable  -> void {
        async::run(std::move(taskpool), [client] () mutable -> future<> {
            co_await client->parse_request(0, 0);
            co_await client->execute_handler();
            std::cout << "FINISHED\n";
            // auto &conn_data = client->connection->as<connection_t>();
            // conn_data.write_watcher.send()
            // conn_data.status.fetch_or(CONN_HALF_CLOSED);
        });
    });
}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_stream_priority_update(void *ctx, uint64_t stream_id) {

}

void manapi::net::worker::http_v3_tquic::tquic_http3_on_stream_reset(void *ctx, uint64_t stream_id, uint64_t error_code) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (ctx);
    auto &conn_data = connection->as<connection_t>();

    auto stream_it = conn_data.streams.find(stream_id);
    if (stream_it == conn_data.streams.end()) {
        return;
    }
    auto &stream = stream_it->second->as<connection_stream_t>();

    conn_data.worker->_stream_close(stream);
    conn_data.streams.erase(stream_it);
}

bool manapi::net::worker::http_v3_tquic::_flush_write_stream(connection_t &conn_data, std::map<uint64_t, std::shared_ptr<connection>>::iterator &stream_it) {
    auto &s = stream_it->second->as<connection_stream_t>();
    bool rlt = false;

    if (s.status & CONN_WRITE) {

        ssize_t written_total = 0;

        auto body_len = s.wbuff_caret - s.wbuff_pos;
        const bool finish = s.finished;

        while (s.wbuff_caret != s.wbuff_pos) {
            auto written = http3_send_body(conn_data.http3_conn, conn_data.conn, s.stream_id, s.wbuff + s.wbuff_pos, s.wbuff_caret - s.wbuff_pos, finish);
            if (written <= 0) {
                return rlt;
            }
            s.wbuff_pos += static_cast<int>(written);
            written_total += written;
        }

        if (finish && body_len == written_total) {
            /* connection finished */
            stream_it = conn_data.streams.erase(stream_it);
            rlt = true;
        }


        MANAPIHTTP_LOG("write do: {} {}.", s.wbuff_pos, s.wbuff_caret);
        if (s.wbuff_caret == s.wbuff_pos) {
            s.status.fetch_xor(CONN_WRITE);
            conn_data.write_total += written_total;
            conn_data.worker->site.taskpool->append_task(
                [handle = std::move(s.handle_io)] () -> void { handle(); });
        }
    }
    else if (s.status & CONN_IDLE) {
        auto rhs = http3_send_headers(conn_data.http3_conn, conn_data.conn, s.stream_id, s.headers, s.headers_size, s.finished);
        if (rhs == 0) {
            s.status.fetch_xor(CONN_IDLE);

            quic_stream_shutdown(conn_data.conn, s.stream_id, QUIC_SHUTDOWN_READ, 0);

            if (s.finished) {
                stream_it = conn_data.streams.erase(stream_it);
                rlt = true;
            }

            conn_data.worker->site.taskpool->append_task(
                [handle = std::move(s.handle_io)] () mutable -> void { handle(); });
        }
        else {
            printf("hello\n");
        }
    }

    return rlt;
}

void manapi::net::worker::http_v3_tquic::_quic_set_header(http3_header_t &header, const std::string &key, const std::string &value) {
    header = http3_header_t {
        .name = (uint8_t *) (key.data()),
        .name_len = key.size(),
        .value = (uint8_t *) (value.data()),
        .value_len = value.size()
    };
}

void manapi::net::worker::http_v3_tquic::_stream_close(connection_stream_t &stream) {
    stream.status.fetch_or(CONN_CLOSED);
    bool flag = false;

    if (stream.status & CONN_WRITE) {
        stream.status.fetch_xor(CONN_WRITE);
        flag = true;
    }

    else if (stream.status & CONN_READ) {
        stream.status.fetch_xor(CONN_READ);
        flag = true;
    }

    else if (stream.status & CONN_IDLE) {
        stream.status.fetch_xor(CONN_IDLE);
        flag = true;
    }

    if (flag) {
        this->site.taskpool->append_task(
            [handle = std::move(stream.handle_io)] () mutable -> void { handle(); });
    }
}

void manapi::net::worker::http_v3_tquic::_write_watcher_cb(struct ev_loop *loop, ev_async *w, int revents) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (w->data);
    auto &conn_data = connection->as<connection_t>();

    if (conn_data.status & CONN_CLOSED) {
        return;
    }

    for (auto it = conn_data.streams.begin(); it != conn_data.streams.end();) {
        if (!_flush_write_stream (conn_data, it)) {
            ++it;
        }
    }
}

void manapi::net::worker::http_v3_tquic::_flush_connection_closed(connection_t &conn_data) {
    // if (!(conn_data.status & CONN_CLOSED) && (conn_data.status & CONN_HALF_CLOSED) && conn_data.streams.empty()) {
    //     conn_data.status.fetch_xor(CONN_HALF_CLOSED);
    // }

    if (quic_conn_is_closed(conn_data.conn)) {
        conn_data.timer.stop();
        conn_data.write_watcher.stop();
        conn_data.worker->_reset_all_streams(conn_data);

        delete static_cast<std::shared_ptr<worker::connection> *> (conn_data.timer.data);
    }
}

void manapi::net::worker::http_v3_tquic::_reset_all_streams(connection_t &conn_data) {
    for (auto &stream: conn_data.streams) {
        this->_stream_close(stream.second->as<connection_stream_t>());
    }

    conn_data.streams.clear();
}

void manapi::net::worker::http_v3_tquic::_connection_timer_check(struct ev_loop *loop, ev_timer *w, int revents) {
    auto &connection = *static_cast<std::shared_ptr<worker::connection> *> (w->data);
    auto &conn_data = connection->as<connection_t>();

    if (conn_data.stream_read_cnt > 0) {
        if (conn_data.read_total - conn_data.read_total_prev < 8 * 1024 * 1024) {
            MANAPIHTTP_LOG2("CONN_READ TIMEOUT");
        }
    }

    if (conn_data.stream_write_cnt > 0) {
        if (conn_data.write_total - conn_data.write_total_prev < 8 * 1024 * 1024) {
            MANAPIHTTP_LOG("CONN_WRITE TIMEOUT {}", conn_data.streams.size());

            // for (auto stream_it = conn_data.streams.begin(); stream_it != conn_data.streams.end(); ) {
            //     auto &stream = stream_it->second->as<connection_stream_t>();
            //     if (!http_v3_tquic::_flush_write_stream(conn_data, stream_it)) {
            //         ++stream_it;
            //     }
            // }
            // quiche_conn_close(conn_data.conn, true, QUICHE_ERR_DONE, reinterpret_cast<const uint8_t*> ("timeout"), sizeof("timeout") - 1);
            // conn_data.status.fetch_or(CONN_CLOSED);
        }
    }

    conn_data.read_total_prev = conn_data.read_total;
    conn_data.write_total_prev = conn_data.write_total;

    conn_data.timer.repeat = 0.2;
    conn_data.timer.again();
}

void manapi::net::worker::http_v3_tquic::_quic_timeout(ev::timer &timer, int revents) {
    //MANAPIHTTP_LOG2("timeout");
    quic_endpoint_on_timeout(this->_quic_server);
    this->_quic_process_connections();
    this->_quic_timeout_again();
}

void manapi::net::worker::http_v3_tquic::_quic_timeout_again() {
    auto t = static_cast<double>(quic_endpoint_timeout(this->_quic_server)) / 1e6f;
    this->timeout->repeat = t;
    this->timeout->again();
}

void manapi::net::worker::http_v3_tquic::_quic_try_new_connection(quic_conn_t *conn) {
    auto index = quic_conn_index(conn);
    auto connection_it = this->connections.find(index);
    connection_t *conn_data;
    std::shared_ptr<worker::connection> conn_shared;
    std::shared_ptr<worker::connection> *conn_ptr;
    if (connection_it != this->connections.end()) {
        /* already */
        conn_ptr = static_cast<std::shared_ptr<worker::connection>*> (quic_conn_context(connection_it->second));
        conn_shared = *conn_ptr;
        conn_data = &conn_shared->as<connection_t>();
    }
    else {
        conn_shared = std::make_shared<worker::connection>(worker::connection{new connection_t{
            .conn = {nullptr}, .timer = this->le->get_loop(), .write_watcher = this->le->get_loop(), .http3_conn = {nullptr},
            .streams = {}, .worker = this, .status = 0, .write_total = 0, .read_total = 0, .write_total_prev = 0, .read_total_prev = 0,
            .stream_read_cnt = 0, .stream_write_cnt = 0}, [] (void *ptr) -> void {
                MANAPIHTTP_LOG2("CONNECTION CLOSED");
                auto conn_data = static_cast<connection_t *> (ptr);
                if (conn_data->http3_conn) {
                    http3_conn_free(conn_data->http3_conn);
                }
                delete conn_data;
            }});

        conn_data = &conn_shared->as<connection_t>();
        conn_ptr = new decltype(conn_shared)(conn_shared);
        conn_data->conn = conn;
        conn_data->timer.data = conn_ptr;
        conn_data->write_watcher.data = conn_ptr;

        ev_async_init(&conn_data->write_watcher, http_v3_tquic::_write_watcher_cb);
        ev_timer_init(&conn_data->timer, http_v3_tquic::_connection_timer_check, 0.2, 0.0);

        quic_conn_set_context(conn, conn_ptr);

        this->connections.insert({index, conn});

        conn_data->timer.start();
        conn_data->write_watcher.start();
    }


    if (!conn_data->http3_conn) {
        conn_data->http3_conn = http3_conn_new(conn, this->_quic_h3_config);
        if (conn_data->http3_conn) {
            http3_conn_set_events_handler(conn_data->http3_conn, &http_v3_tquic::http3_methods, conn_ptr);
        }
    }
}

int manapi::net::worker::http_v3_tquic::_grab_headers(const uint8_t *name, size_t name_len, const uint8_t *value, size_t value_len, void *argp) {
    auto client = static_cast<http::http_v2 *> (argp);
    client->request_data.headers[std::string{reinterpret_cast<const char *>(name), name_len}] = std::string_view{reinterpret_cast<const char *>(value), value_len};
    return 0;
}

bool manapi::net::worker::http_v3_tquic::_validate_token(std::string_view token, std::string &odcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len) {
    if (!token.starts_with("quiche")
        || token.substr(sizeof("quiche") - 1, sockaddr_len) != std::string_view{reinterpret_cast<const char *> (&sockaddr_src), sockaddr_len}){
        return false;
    }

    odcid = token.substr(sizeof("quiche") - 1 + sockaddr_len);
    return true;
}

std::string manapi::net::worker::http_v3_tquic::_gen_mint_token(std::string_view dcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len) {
    std::string token;
    token.reserve(sizeof("quiche")-1 + sockaddr_len + dcid.size());
    token += "quiche";
    token += std::string_view{reinterpret_cast<const char *>(&sockaddr_src), sockaddr_len};
    token += dcid;
    return std::move(token);
}

void manapi::net::worker::http_v3_tquic::_quic_process_connections() {
    quic_endpoint_process_connections(this->_quic_server);
}

manapi::future<ssize_t> manapi::net::worker::http_v3_tquic::default_write(connection &conn, const void *buf, size_t size, bool flag) {
    auto &stream_data = conn.as<connection_stream_t>();
    ssize_t rhs = 0;
    while (size != rhs) {
        if (stream_data.wbuff_pos == stream_data.wbuff_caret) {
            stream_data.wbuff_pos = 0;
            stream_data.wbuff_caret = 0;
        }

        int len = std::min(static_cast<int>(sizeof (stream_data.wbuff) - stream_data.wbuff_caret), static_cast<int>(size-rhs));

        memcpy(stream_data.wbuff + stream_data.wbuff_caret, static_cast<const char *>(buf)+rhs, len);
        stream_data.wbuff_caret += len;

        bool _flag = (flag && size == rhs+len);
        if (stream_data.wbuff_caret == sizeof (stream_data.wbuff) || _flag) {
            MANAPIHTTP_LOG("write req: {} {} {}", stream_data.wbuff_pos, stream_data.wbuff_caret, _flag);
            auto &conn_data = stream_data.connection->as<connection_t>();
            if (_flag) {
                stream_data.finished = _flag;
            }

            co_await connection_io_await{stream_data.handle_io, stream_data.status, CONN_WRITE, &conn_data.write_watcher, &conn_data.stream_write_cnt};

            if (stream_data.status & CONN_CLOSED) {
                co_return -1;
            }
        }

        rhs += len;
    }
    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::http_v3_tquic::default_read(connection &conn, void *buf, size_t size) {
    auto &stream_data = conn.as<connection_stream_t>();

    if (stream_data.rbuff_pos == stream_data.rbuff_caret) {
        stream_data.rbuff_pos = 0;
        stream_data.rbuff_caret = 0;

        MANAPIHTTP_LOG("read req: {} {}", stream_data.rbuff_pos, stream_data.rbuff_caret);
        auto &conn_data = stream_data.connection->as<connection_t>();
        co_await connection_io_await{stream_data.handle_io, stream_data.status, CONN_READ, nullptr, &conn_data.stream_read_cnt};

        if (stream_data.status & CONN_CLOSED) {
            co_return -1;
        }
    }

    int len = stream_data.rbuff_caret - stream_data.rbuff_pos;
    memcpy(buf, stream_data.rbuff + stream_data.rbuff_pos, len);


    stream_data.rbuff_pos += len;

    co_return static_cast<ssize_t>(len);
}

#endif
