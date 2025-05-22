#include "ManapiHttpResponse.hpp"
#include "components/ManapiURLDecodeStream.hpp"
#include "worker/HTTPv3_clouflare_quiche.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

#include <quiche.h>

#include "crypto/ManapiAEAD.hpp"
#include "ManapiString.hpp"
#include "ManapiVersions.hpp"

#define MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE 1350
#define MANAPIHTTP_QUICHE_CONN_ID_SIZE 16

static constexpr size_t quiche_token_max_len_ = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;

enum http_v3_worker_flags {
    HTTP_V3_QUICHE_WORKER_BUFFER_WAS_FREED = 1
};

enum http_v3_stream_flags {
    HTTP_V3_STREAM_WANT_READ = manapi::ev::READ,
    HTTP_V3_STREAM_WANT_WRITE = manapi::ev::WRITE,
    HTTP_V3_STREAM_CLOSED = manapi::ev::DISCONNECT,
    HTTP_V3_STREAM_RECV_END = 8,
    HTTP_V3_STREAM_REMOVED = 16
};

template<typename T>
requires(version_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
    return quiche_h3_event_headers_has_more_frames(static_cast<T>(event));
}

template<typename T>
requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
    return quiche_h3_event_headers_has_body(static_cast<T>(event));
}

template<typename ...Args>
requires(version_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args&&...args) {
    return quiche_h3_send_additional_headers(args...);
}

template<typename ...Args>
requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args&&...args) { /* skip */ return 0; }

manapi::net::worker::http_v3_cloudflare_quiche::http_v3_cloudflare_quiche(net::http::site site,
    std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config* config) : udp(std::move(site), std::move(wdata), config) {
    this->flags = 0;
    this->recv_buffer = nullptr;
}

manapi::net::worker::http_v3_cloudflare_quiche::~http_v3_cloudflare_quiche() = default;

std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche> manapi::net::worker::http_v3_cloudflare_quiche::create(
    net::http::site site, std::shared_ptr<worker::worker_config_t> wdata,
    std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::http_v3_cloudflare_quiche>(std::move(site), std::move(wdata), config.get());
    worker->self_ = std::weak_ptr (worker);
    return std::move(worker);
}

void manapi::net::worker::http_v3_cloudflare_quiche::init() {
    udp::init();
    do {
        if (auto rhs = this->udp_accept_->recv_start()) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't start recv due to result - {}", rhs);
            goto err;
        }

        this->quiche_config_ = quiche_config_new(QUICHE_PROTOCOL_VERSION);
        this->quiche_h3_config_ = quiche_h3_config_new();

        auto const ssl_config = &this->config_->ssl_config();

        if (!ssl_config->enabled) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_CONFIG_ERROR, "QUICHE: QUIC requires SSL be enabled");
        }

        if (0 != quiche_config_load_cert_chain_from_pem_file(this->quiche_config_, ssl_config->cert.data())) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load cert chain from pem file: {}", ssl_config->cert);
        }

        if (0 != quiche_config_load_priv_key_from_pem_file(this->quiche_config_, ssl_config->key.data())) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load priv key from pem file: {}", ssl_config->key);
        }

        if(quiche_config_set_application_protos(this->quiche_config_,
            reinterpret_cast <const uint8_t *> (QUICHE_H3_APPLICATION_PROTOCOL), sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1)) {
            goto err;
        }
        quiche_config_set_max_idle_timeout(this->quiche_config_, 5000);
        quiche_config_set_max_recv_udp_payload_size(this->quiche_config_, MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE);
        quiche_config_set_max_send_udp_payload_size(this->quiche_config_, MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE);
        quiche_config_set_initial_max_data(this->quiche_config_, 10000000);
        quiche_config_set_initial_max_stream_data_bidi_local(this->quiche_config_, 1000000);
        quiche_config_set_initial_max_stream_data_bidi_remote(this->quiche_config_, 1000000);
        quiche_config_set_initial_max_stream_data_uni(this->quiche_config_, 1000000);
        quiche_config_set_initial_max_streams_bidi (this->quiche_config_, 100);
        quiche_config_set_initial_max_streams_uni (this->quiche_config_, 100);
        //quiche_config_set_disable_active_migration (this->quiche_config_, true);
        quiche_config_verify_peer(this->quiche_config_, this->config_->verify_peer());
        if (this->config_->is_quic_debug()) {
            if (quiche_enable_debug_logging([] (const char *line, void *argp)
                -> void {
                MANAPIHTTP_LOG2(line);
            }, this)) {
                goto err;
            }
        }

        if (this->config_->quic_cc_algo() != http::versions::QUIC_CC_NONE) {
            quiche_cc_algorithm algo = QUICHE_CC_RENO;

            switch (this->config_->quic_cc_algo())
            {
                case http::versions::QUIC_CC_CUBIC:   algo = QUICHE_CC_CUBIC;     break;
                case http::versions::QUIC_CC_RENO:    algo = QUICHE_CC_RENO;      break;
                case http::versions::QUIC_CC_BBR:     algo = QUICHE_CC_BBR;       break;
                case http::versions::QUIC_CC_BBR2:    algo = QUICHE_CC_BBR2;      break;
                default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo: {}",
                        static_cast<int>(this->config_->quic_cc_algo()));
            }

            quiche_config_set_cc_algorithm (this->quiche_config_, algo);
        }

        /* every 1 second */
        this->limit_rate_timer = manapi::async::current()->timerpool()->append_interval_sync(1000,
            [this] (manapi::timer t) -> void { this->update_limit_rate(); });

        try {
            this->recv_buffer.reset(static_cast<char *>(malloc (MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE)));
        }
        catch (...) {
            manapi::async::current()->logger()->error(
                manapi::logger::default_service, ERR_FATAL, "udp buffer alloc failed");
            goto err;
        }
    }
    while (0);
    return;
err:
    THROW_MANAPIHTTP_EXCEPTION2(ERR_SOCKET, "quiche: init(...) failed");
}

void manapi::net::worker::http_v3_cloudflare_quiche::close_connection(const shared_conn &conn, bool clean) {
    auto s = conn->as<connection_stream_t>();
    if (s->flags & HTTP_V3_STREAM_REMOVED) {
        return;
    }

    s->flags |= HTTP_V3_STREAM_CLOSED|HTTP_V3_STREAM_REMOVED;

    if (s->ev_callback) {
        s->ev_callback->operator()(conn, ev::DISCONNECT, nullptr, 0);
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::reset_all_streams_(connection_t *conn_data) {
    for (const auto &s : *conn_data->streams) {
        close_connection(s.second, s.second->as<connection_stream_t>());
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::quiche_timeout_(manapi::timer w, const shared_conn &connection) {
    auto conn_data = connection->as<connection_t>();

    quiche_conn_on_timeout(conn_data->conn);

    http_v3_cloudflare_quiche::quiche_timeout_again_(conn_data);
    conn_data->worker->quiche_flush_egress_(connection, conn_data);
    http_v3_cloudflare_quiche::flush_connection_closed_(connection, conn_data);
}

int manapi::net::worker::http_v3_cloudflare_quiche::grab_headers_(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) {
    auto req = static_cast<http::request_data_t *> (argp);
    auto const key = std::string{reinterpret_cast<const char *>(name), name_len};
    req->headers[key].append(reinterpret_cast<const char *>(value), value_len);
    return 0;
}

bool manapi::net::worker::http_v3_cloudflare_quiche::validate_token_(char *token, size_t token_len, char *odcid, size_t *odcid_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) {
    const auto hs = sizeof ("quiche") - 1 + sockaddr_len;

    if (token_len < hs) {
        return false;
    }

    if (*odcid_len < token_len - hs) {
        return false;
    }

    if (0 != memcmp (token, static_cast<const char *>("quiche"), sizeof ("quiche") - 1)
        || 0 != memcmp (token + sizeof("quiche") - 1, reinterpret_cast<const char *> (sockaddr_src), sockaddr_len)){
        return false;
    }

    *odcid_len = token_len - hs;
    memcpy (odcid, token + hs, *odcid_len);

    return true;
}

int manapi::net::worker::http_v3_cloudflare_quiche::gen_mint_token_(char *dcid, size_t dcid_len, char *token, size_t *token_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) {
    std::size_t const s = sizeof("quiche")-1 + sockaddr_len + dcid_len;

    if (*token_len < s) {
        return -1;
    }

    *token_len = 0;

    memcpy (token, static_cast<const char *>("quiche"), sizeof ("quiche") - 1);
    *token_len += sizeof ("quiche") - 1;

    memcpy (token + *token_len, reinterpret_cast<const char *>(sockaddr_src), sockaddr_len);
    *token_len += sockaddr_len;

    memcpy (token + *token_len, static_cast<const char *> (dcid), dcid_len);
    *token_len += dcid_len;

    return 0;
}

int manapi::net::worker::http_v3_cloudflare_quiche::quiche_flush_egress_(const shared_conn &connection, connection_t *data) {
    if (data->flags & CONN_CLOSED) {
        return -1;
    }

    uint8_t out[MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(data->conn, out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            /* done writing */
            break;
        }

        if (written < 0) {
            /* failed to create packet */
            return -2;
        }

        ev::buff_t buff = {.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)};
        ssize_t const sent = this->udp_accept_->try_send(&buff, 1, reinterpret_cast<sockaddr *>(&send_info.to));
        if (sent < 0) {
            this->force_close_(connection, data);
            return -1;
        }
    }

    return 0;
}

void manapi::net::worker::http_v3_cloudflare_quiche::quiche_timeout_again_(connection_t *connection) {
    auto const repeat = static_cast<int64_t> (quiche_conn_timeout_as_millis(connection->conn));

    if (!connection->timeout.enabled()) {
        connection->timeout.again(repeat > 0 ? repeat : 200);
    }
    else {
    }
}

void quiche_set_header_(quiche_h3_header *header, std::string_view key, std::string_view value) {
    *header = {
        .name = reinterpret_cast<const uint8_t *> (key.data()),
        .name_len = key.size(),
        .value = reinterpret_cast<const uint8_t *> (value.data()),
        .value_len = value.size()
    };
}


void manapi::net::worker::http_v3_cloudflare_quiche::onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) {
    socklen_t sockaddr_len = 0;
    uint8_t out[MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE];

    auto sockaddr_src = (sockaddr *)addr;
    if (addr->sa_family == ev::IPv4) {
     sockaddr_len = sizeof (sockaddr_in);
    }
    else if (addr->sa_family == ev::IPv6) {
     sockaddr_len = sizeof (sockaddr_in6);
    }

    connection_t *conn_data{nullptr};

    uint8_t type;
    uint32_t version;

    char scid[QUICHE_MAX_CONN_ID_LEN];
    uint64_t scid_len = sizeof (scid);

    char dcid[QUICHE_MAX_CONN_ID_LEN];
    uint64_t dcid_len = sizeof (dcid);

    char odcid[QUICHE_MAX_CONN_ID_LEN];
    uint64_t odcid_len = sizeof (odcid);

    char token[quiche_token_max_len_];
    uint64_t token_len = sizeof (token);

    if(0 != quiche_header_info (reinterpret_cast<const uint8_t *> (buff), size, MANAPIHTTP_QUICHE_CONN_ID_SIZE,
        &version, &type, reinterpret_cast<uint8_t *> (scid), &scid_len, reinterpret_cast<uint8_t *> (dcid), &dcid_len,
    reinterpret_cast<uint8_t *> (token), &token_len)) {

        /* broken packet */
        return;
    }

    auto it = this->connections.find(std::string{dcid, dcid_len});
    if (it == this->connections.end()) {
        // if (this->connections.size() >= this->config_->max_connections()) {
        //     return;
        // }

        if (!quiche_version_is_supported(version)) {
             const int64_t written = quiche_negotiate_version(reinterpret_cast<uint8_t *> (scid),
                 scid_len, reinterpret_cast<uint8_t *> (dcid), dcid_len, out, sizeof (out));

             if (written < 0) {
                 return;
             }

             ev::buff_t buff2[1] = {{.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)}};
             ssize_t rhs = this->udp_accept_->try_send(buff2, 1, reinterpret_cast<sockaddr *>(sockaddr_src));

             if (rhs != written) {
                 /* failed to send */
             }

             return;
        }

        if (!token_len) {
            token_len = quiche_token_max_len_;
            if (http_v3_cloudflare_quiche::gen_mint_token_(dcid, dcid_len, token, &token_len, sockaddr_src, sockaddr_len)) {
                /* failed */
                return;
            }

            std::string new_scid = crypto::random_string(MANAPIHTTP_QUICHE_CONN_ID_SIZE);
            const int64_t written = quiche_retry(reinterpret_cast<uint8_t *> (scid), scid_len, reinterpret_cast<uint8_t *>(dcid), dcid_len,
             reinterpret_cast<uint8_t *>(new_scid.data()), new_scid.size(), reinterpret_cast<uint8_t *>(token), token_len, version, out, sizeof (out));

            if (written < 0) {
                return;
            }

            ev::buff_t buff2[1] = {{.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)}};
            ssize_t rhs = this->udp_accept_->try_send(buff2, 1, reinterpret_cast<sockaddr *>(sockaddr_src));

            if (rhs != written) {
                /* failed to send */
            }

            return;
        }

        if (!http_v3_cloudflare_quiche::validate_token_(token, token_len, odcid, &odcid_len, sockaddr_src, sockaddr_len)) {
            return;
        }

        auto quiche_conn_ = quiche_accept(reinterpret_cast<uint8_t *> (dcid), dcid_len,
         reinterpret_cast<uint8_t *>(odcid), odcid_len, &this->config_->server_address(), this->config_->server_len(),
         reinterpret_cast<sockaddr *>(sockaddr_src), sockaddr_len, this->quiche_config_);

        if (!quiche_conn_) {
            return;
        }

        auto connection = std::make_shared<worker::connection>(new connection_t {
            .flags = 0, .worker = this, .conn = quiche_conn_, .http3_conn = nullptr, .streams = {}}, +[] (void *conn_data) {
                std::cout << "CONN CLOSE\n";
                delete static_cast<connection_t*>(conn_data);
            }
        );

        conn_data = connection->as<connection_t>();

        conn_data->timeout = manapi::async::current()->timerpool()->append_timer_sync (200, [connection = std::weak_ptr (connection)] (manapi::timer t)
            -> void { http_v3_cloudflare_quiche::quiche_timeout_(std::move(t), connection.lock()); });

        conn_data->streams = std::make_unique<decltype(conn_data->streams)::element_type>();
        conn_data->self = connection;

        this->connections.insert({
            std::string{dcid, dcid_len},
            std::move(connection)
        });
    }
    else {
        conn_data = it->second->as<connection_t>();
    }

    decltype(this->connections)::iterator connection_it;

    quiche_recv_info recv_info {
         reinterpret_cast <sockaddr *>(sockaddr_src),
         sockaddr_len,
         (sockaddr *)&this->config_->server_address(),
         this->config_->server_len()
    };

    ssize_t done = quiche_conn_recv(conn_data->conn, reinterpret_cast<uint8_t *> (buff), size, &recv_info);

    if (done < 0) {
        return;
    }

    if ((quiche_conn_is_in_early_data(conn_data->conn) || quiche_conn_is_established(conn_data->conn)) && !conn_data->http3_conn) {
        conn_data->http3_conn = quiche_h3_conn_new_with_transport(conn_data->conn, this->quiche_h3_config_);

        if (!conn_data->http3_conn) {
            MANAPIHTTP_LOG2("QUICHE: assert(!conn_data->http3_conn) failed");
            return;
        }
    }

    try {
        if (conn_data->http3_conn && !(conn_data->flags & CONN_CLOSED)) {
            http_v3_cloudflare_quiche::flush_write_(it->second, conn_data);

            quiche_h3_event *event{nullptr};

            while (true) {
                int64_t stream_id = quiche_h3_conn_poll(conn_data->http3_conn, conn_data->conn, &event);

                if (stream_id < 0) {
                    break;
                }

                switch (quiche_h3_event_type(event)) {
                    case QUICHE_H3_EVENT_HEADERS: {
                        auto connection = std::make_shared<worker::connection>(new connection_stream_t {
                            .flags = 0,
                            .id = 0,
                            .conn = conn_data
                        }, +[] (void *ptr)
                            -> void { delete static_cast<connection_stream_t *> (ptr); });

                        auto s = connection->as<connection_stream_t>();

                        s->req = std::make_unique<http::request_data_t>();
                        s->top = std::make_unique<connection_io>();

                        if (quiche_h3_event_for_each_header(event, http_v3_cloudflare_quiche::grab_headers_, s->req.get())) {
                            goto err;
                        }

                        auto hit = s->req->headers.extract(":path");
                        if (hit.empty())
                            goto err;

                        s->req->uri = std::move(hit.mapped());

                        hit = s->req->headers.extract(":method");
                        if (hit.empty())
                            goto err;

                        s->req->method = std::move(hit.mapped());
                        s->req->divided = -1;
                        s->req->http = http::versions::HTTP_v3;

                        if (manapi_quiche_h3_event_headers_has_more_frames_(event)) {
                            auto contentlength = s->req->headers.find(http::HEADER.CONTENT_LENGTH);
                            s->req->body_size = contentlength != s->req->headers.end()
                             ? std::stoll(contentlength->second) : -1 /* The size isn't fixed */;
                        }
                        else {
                            s->req->body_size = 0;
                        }

                        http::url_decode_stream url_decoder;

                        if (auto rhs = url_decoder << s->req->uri) {
                            goto err;
                        }

                        s->req->path = url_decoder.result();
                        s->req->divided = url_decoder.divided();

                        auto const req_ptr = s->req.get();
                        auto cdata = std::make_unique<http::internal::handle_data_t>(connection, this->self_.lock(),
                            req_ptr, std::make_unique<http::internal::cont_callback_cb_t>(
                            [this, sconn = connection, conn = it->second, req = std::move(s->req)] (bool ok)
                            -> void {
                                auto const s = sconn->as<connection_stream_t>();
                                auto const conndata = conn->as<connection_t>();
                                s->ev_callback = nullptr;
                                s->flags = ev::DISCONNECT;

                                this->close_connection(sconn, ok);
                                conndata->streams->erase(s->id);
                                flush_connection_closed_(conn, conndata);
                        }));

                        conn_data->streams->insert({s->id, std::move(connection)});

                        // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                        // this->event_flags(conn, 0);

                        net::http::internal::handle_income_request(std::move(cdata), this->site().handler(req_ptr), http::OK_200);

                        break;
                    }
                    case QUICHE_H3_EVENT_DATA: {
                        auto stream_it = conn_data->streams->find(stream_id);
                        if (stream_it == conn_data->streams->end()) {
                            break;
                        }

                        auto &sconn = stream_it->second;

                        if (this->flush_read_(sconn)) {
                            goto err;
                        }

                        // skip

                        break;
                    }
                    case QUICHE_H3_EVENT_FINISHED: {
                        auto stream_it = conn_data->streams->find(stream_id);
                        if (stream_it == conn_data->streams->end()) {
                            break;
                        }

                        auto &stream_connection = stream_it->second;
                        auto stream = stream_connection->as<connection_stream_t>();

                        stream->flags |= HTTP_V3_STREAM_RECV_END;

                        if ((stream->flags & ev::READ) &&  stream->ev_callback) {
                            stream->ev_callback->operator()(stream_connection, ev::READ, nullptr, 0);
                        }

                        break;
                    }
                    case QUICHE_H3_EVENT_RESET: {
                        auto stream_it = conn_data->streams->find(stream_id);
                        if (stream_it == conn_data->streams->end()) {
                            break;
                        }

                        this->close_connection(stream_it->second, false);

                        break;
                    }
                    case QUICHE_H3_EVENT_PRIORITY_UPDATE: {
                        break;
                    }
                    case QUICHE_H3_EVENT_GOAWAY: {
                        http_v3_cloudflare_quiche::reset_all_streams_(conn_data);

                        break;
                    }
                }

                quiche_h3_event_free(event);
            }
        }



        /* setup timeout */
        if (it != this->connections.end()) {
            this->quiche_flush_egress_(it->second, conn_data);
            http_v3_cloudflare_quiche::quiche_timeout_again_(conn_data);
            http_v3_cloudflare_quiche::flush_connection_closed_(it->second, conn_data);
        }
    }
    catch (...) {
        goto err;
    }

    return;

    err: {
        if (it != this->connections.end()) {
            this->force_close_(it->second, it->second->as<connection_t>());
        }
    }
}

ssize_t manapi::net::worker::http_v3_cloudflare_quiche::sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    return sync_write_ex (conn, buff, size, finish, static_cast<int>(this->config_->max_buffer_stack()));
}

ssize_t manapi::net::worker::http_v3_cloudflare_quiche::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto s = conn->as<connection_stream_t>();
    if (s->flags & ev::DISCONNECT) {
        return -1;
    }

    auto const copy = size;

    if (!copy) {
        return copy;
    }

    auto rhs = quiche_h3_send_body(s->conn->http3_conn, s->conn->conn, s->id, static_cast<const uint8_t *> (buff), size, finish);

    if (rhs < 0) {
        switch (rhs) {
            case QUICHE_H3_ERR_DONE: {
                rhs = 0;
                break;
            }
            default: {
                return -1;
            }
        }
    }

    if (s->conn->worker->quiche_flush_egress_(s->conn->self, s->conn)) {
        return -1;
    }

    return rhs;
}

void manapi::net::worker::http_v3_cloudflare_quiche::configure_connection(const shared_conn &conn, oncont_cb cb)  {
    cb.call(true);
}

int manapi::net::worker::http_v3_cloudflare_quiche::event_flags(const shared_conn &conn) {
    return conn->as<connection_stream_t>()->flags & 0b111;
}

int manapi::net::worker::http_v3_cloudflare_quiche::event_flags(const shared_conn &conn, int flags) {
    auto const data = conn->as<connection_stream_t>();
    auto &flags_ = data->flags;
    return std::exchange(flags_, ((flags_ >> 2) << 2) | flags);
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::http_v3_cloudflare_quiche::event_on( const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<connection_stream_t>();
    auto n = std::exchange(conn_data->ev_callback, std::move(callback));
    return std::move(n);
}

void manapi::net::worker::http_v3_cloudflare_quiche::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto const data = conn->as<connection_stream_t>();
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size);
    }
}

bool manapi::net::worker::http_v3_cloudflare_quiche::is_valid_connection(worker::connection *connection) {
    return true;
}

bool manapi::net::worker::http_v3_cloudflare_quiche::is_writable(const shared_conn &conn) {
    auto const data = conn->as<connection_stream_t>();
    return data->top->send_size < this->config_->max_buffer_stack();
}

// dvoid manapi::net::worker::http_v3_cloudflare_quiche::recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) {
//     assert(this->flags & HTTP_V3_QUICHE_WORKER_BUFFER_WAS_FREED);
//
//     if (this->flags & HTTP_V3_QUICHE_WORKER_BUFFER_WAS_FREED) {
//         buff->base = this->recv_buffer.get();
//         buff->len = MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE;
//         this->flags ^= HTTP_V3_QUICHE_WORKER_BUFFER_WAS_FREED;
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::recv_buffer_dealloc_(const ev::buff_t *buf) {
//     this->flags |= HTTP_V3_QUICHE_WORKER_BUFFER_WAS_FREED;
// }

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate() {

}

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate_connection(connection &conn) {

}

int manapi::net::worker::http_v3_cloudflare_quiche::flush_read_buffers_(const shared_conn &conn, connection_stream_t *s, connection_io_part *top, int *cnt) {
    try {
        while (top->last_deque) {
            auto object = std::move(top->deque->buffer);
            top->deque = std::move(top->deque->next);

            if (!top->deque) {
                object->resize(top->deque_cursor);
                top->last_deque = nullptr;
                top->deque_cursor = 0;
            }

            if (top->deque_current) {
                object->shift_add(top->deque_current);
                top->deque_current = 0;
            }

            if (cnt)
                (*cnt)--;

            s->ev_callback->operator()(conn, ev::READ, object->data(), object->size() /**,std::move(object)**/);
        }

        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        std::cerr << e.what() << "\n";
    }

    return CONN_IO_ERROR;
}

int manapi::net::worker::http_v3_cloudflare_quiche::flush_read_(const shared_conn &stream) {
    auto const s = stream->as<connection_stream_t>();
    auto top = &s->top->recv;
    buffer_deque *parent = nullptr;
    ssize_t rhs;
    int flags;
    int const maxcnt = this->config_->max_buffer_stack();

    do {
        if (!top->last_deque || top->last_deque->buffer->size() == top->deque_cursor) {
            if (auto const res = this->flush_read_buffers_(stream, s, top, &s->top->recv_size)) {
                return res;
            }

            if (s->top->recv_size >= maxcnt)
                return CONN_IO_WANT_READ;

            auto buffer = this->bufferpool()->get();
            buffer->resize_max(this->config_->buffer_size());

            auto obj = std::make_unique<buffer_deque>(std::move(buffer), nullptr);
            if (top->last_deque) {
                parent = top->last_deque;
                top->last_deque->next = std::move(obj);
                top->last_deque = top->last_deque->next.get();
            }
            else {
                parent = nullptr;
                top->deque = std::move(obj);
                top->last_deque = top->deque.get();
                top->deque_current = 0;
            }

            top->deque_cursor = 0;
            flags |= 1 /* an empty buffer was created */;
            s->top->recv_size++;
        }
        else
            flags = 0;

        rhs = quiche_h3_recv_body(s->conn->http3_conn, s->conn->conn, s->id, reinterpret_cast<uint8_t *>(top->last_deque->buffer->data() + top->deque_cursor),
            static_cast<int>(top->last_deque->buffer->size() - top->deque_cursor));

        if (rhs >= 0) {
            top->deque_cursor += rhs;
            if (!rhs && (flags /* an empty buffer was created */ )) {
                /* remove an empty buffer at the end */
                connection_io_trim(top, parent, &s->top->recv_size);
            }
        }

    }
    while (rhs > 0);

    if (auto const res = this->flush_read_buffers_(stream, s, top, &s->top->recv_size)) {
        return res;
    }

    return CONN_IO_OK;
}

void manapi::net::worker::http_v3_cloudflare_quiche::force_close_(shared_conn conn, connection_t *conn_data) {
    if (quiche_conn_is_closed(conn_data->conn)) {
        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(conn_data->conn, &stats);
        quiche_conn_path_stats(conn_data->conn, 0, &path_stats);

        fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu\n",
                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        conn_data->worker->connections.erase(conn_data->cid);
        conn_data->timeout.stop();

        if (conn_data->http3_conn) {
            quiche_h3_conn_free(std::exchange(conn_data->http3_conn, nullptr));
        }

        quiche_conn_free(std::exchange(conn_data->conn, nullptr));
    }
    else {
        if (!(conn_data->flags & CONN_REMOVED)) {
            conn_data->flags|=CONN_REMOVED;
            /** refuse connection */
            quiche_conn_close(conn_data->conn, true, 0x02, reinterpret_cast <const uint8_t *> ("i/o timeout"), sizeof ("i/o timeout") - 1);
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::flush_connection_closed_(const shared_conn &conn, connection_t *conn_data) {
    // if (!(conn_data->status & CONN_CLOSED) && (conn_data->status & CONN_HALF_CLOSED) && conn_data->streams.empty()) {
    //     conn_data->status.fetch_xor(CONN_HALF_CLOSED);
    // }

    if (quiche_conn_is_closed(conn_data->conn)) {
        if (!(conn_data->flags & CONN_CLOSED)) {
            conn_data->flags|=(CONN_CLOSED);
            if (conn_data->streams->size()) {
                conn_data->worker->reset_all_streams_(conn_data);
            }
            else {
                force_close_(conn, conn_data);
            }
        }
        else {
            if (conn_data->streams->empty()) {
                /* retry */
                force_close_(conn, conn_data);
            }
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::flush_write_(const shared_conn &conn, connection_t *conn_data) {
    if (conn_data->worker->quiche_flush_egress_(conn, conn_data)) {
        return;
    }

    /* send data */
    int64_t stream_id;
    quiche_stream_iter *stream = quiche_conn_writable(conn_data->conn);
    while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
        auto stream_it = conn_data->streams->find(stream_id);
        //MANAPIHTTP_LOG("WRITE STREAM:{}", stream_id);
        if (stream_it != conn_data->streams->end()) {
            auto const s = stream_it->second->as<connection_stream_t>();
            if (s->flags & ev::WRITE && s->ev_callback) {
                s->ev_callback->operator()(stream_it->second, ev::WRITE, nullptr, 0);
            }
        }
    }
    quiche_stream_iter_free(stream);
}

manapi::future<ssize_t> manapi::net::worker::http_v3_cloudflare_quiche::response(const shared_conn &connection, http::response *resp, bool finish) {
    auto s = connection->as<connection_stream_t>();

    auto headers = resp->headers();
    std::unique_ptr<quiche_h3_header> q_headers (new quiche_h3_header[headers.size() + 1]);

    std::size_t headers_size = headers.size() + 1;
    std::size_t header_cursor = 0;
    size_t i = 1;

    constexpr size_t max_header_value_len = 1000;
    for (auto header = headers.begin(); header != headers.end(); ++header) {
        size_t header_value_cursor = 0;
        while (true) {
            if (header->first.size() > max_header_value_len) {
                THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "header key is too long. Size: {}", header->first.size());
            }
            auto len = std::min(max_header_value_len - header->first.size(), header->second.size() - header_value_cursor);
            quiche_set_header_(q_headers.get() + (i++), header->first, std::string_view{header->second.data() + header_value_cursor, len});
            header_value_cursor += len;
            if (header_value_cursor == header->second.size()) {
                break;
            }
            ++headers_size;
            auto const nheaders = static_cast<quiche_h3_header *>(realloc(q_headers.release(), sizeof (quiche_h3_header) * headers_size));
            if (nheaders) {
                q_headers.reset(nheaders);
            }
            else {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "failed to realloc(...) headers buffer");
            }
        }
    }

    i = 0;

    auto &status_header = headers.operator[](":status");
    status_header = std::to_string(resp->status_code());
    quiche_set_header_(q_headers.get()+(i++), ":status", status_header);

    size_t current_headers_size = 0;
    size_t constexpr max_headers_size = 4000;

    using promise = manapi::async::promise<ssize_t, std::false_type>;

    std::unique_ptr<worker_watcher_cb> prev_cb{nullptr};
    int prev_events{0};

    const auto rhs = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> void {
        prev_events = this->event_flags(connection, ev::WRITE);
        prev_cb = this->event_on(connection, std::make_unique<worker_watcher_cb>(
            [&, resolve = std::move(resolve)] (const shared_conn & conn, int flags, const char *buffer, ssize_t nsize) -> void {
                if (flags & ev::DISCONNECT) {
                    resolve(-1);
                    goto finish;
                }

                if (flags & ev::WRITE) {
                    for (size_t i = header_cursor; i <= headers_size; ) {
                        if (i < headers_size) {
                            auto header = q_headers.get() + i;

                            if (current_headers_size + header->name_len + header->value_len < max_headers_size) {
                                current_headers_size += header->name_len + header->value_len;
                                ++i;

                                continue;
                            }
                        }

                        int rhs;
                        auto cheaders = q_headers.get() + header_cursor;
                        if (header_cursor != 0) {
                            if constexpr (version_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0")) {
                                rhs = manapi_quiche_h3_send_additional_headers_(s->conn->http3_conn, s->conn->conn, s->id, cheaders, i - header_cursor, false, finish && i == headers_size);
                            }
                            else {
                                rhs = 0;
                            }
                        }
                        else {
                            rhs = quiche_h3_send_response(s->conn->http3_conn, s->conn->conn, s->id, cheaders, i - header_cursor, finish && i == headers_size);
                        }

                        if (rhs == 0) {
                            header_cursor = i;
                        }
                        else if (rhs == QUICHE_H3_ERR_STREAM_BLOCKED) {
                            break;
                        }
                        else {
                            resolve (-1);
                            goto finish;
                        }

                        if (s->conn->worker->quiche_flush_egress_(s->conn->self, s->conn)) {

                        }

                        if (i == headers_size) {
                            /* finish */
                            resolve(i);
                            goto finish;
                        }
                    }
                }

                return;
                finish: {
                    this->event_flags(conn, 0);
                }
        }));

        this->feed_event(connection, ev::WRITE, nullptr, 0);
    });

    this->event_on(connection, std::move(prev_cb));
    this->event_flags(connection, prev_events);

    co_return rhs;
}

#endif
