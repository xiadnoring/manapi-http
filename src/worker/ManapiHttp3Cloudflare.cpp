#include "worker/ManapiHttp3Cloudflare.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

#include <cstring>
#include <quiche.h>

#include "http/ManapiHttpResponse.hpp"
#include "http/ManapiURLDecodeStream.hpp"
#include "std/ManapiAsyncSocket.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "ManapiString.hpp"
#include "ManapiVersions.hpp"
#include "worker/ManapiBaseUtils.hpp"
#include "../include/ManapiSiteInternal.hpp"

#define MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE 1350
#define MANAPIHTTP_QUICHE_CONN_ID_SIZE 16

static constexpr std::string_view many_connections_msg = "too many connections";
static constexpr size_t quiche_token_max_len_ = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;

struct grab_headers_t {
    std::map<std::string, std::string, std::less<>> *headers;
    manapi::net::http::config *config;
    uint32_t *headers_size;
    uint32_t max_headers_size;
};

enum http_v3_stream_flags {
    HTTP_V3_STREAM_WANT_READ = manapi::ev::READ,
    HTTP_V3_STREAM_WANT_WRITE = manapi::ev::WRITE,
    HTTP_V3_STREAM_CLOSED = manapi::ev::DISCONNECT,
    HTTP_V3_STREAM_REMOVED = manapi::net::worker::base::CONN_REMOVED,
    HTTP_V3_STREAM_RECV_END = manapi::net::worker::base::CONN_RECV_END,
    HTTP_V3_STREAM_SEND_END = manapi::net::worker::base::CONN_SEND_END,
    HTTP_V3_STREAM_IO_WAITING = manapi::net::worker::base::CONN_IO_WAITING,
};

struct manapi::net::worker::http_v3_cloudflare_quiche::connection_t {
    std::string_view cid;
    int flags;
    http_v3_cloudflare_quiche *worker;
    quiche_conn *conn;
    quiche_h3_conn *http3_conn;
    std::map <int64_t, std::shared_ptr<worker::connection>> streams;
    manapi::timer timeout;
    shared_conn self;
    uint32_t queue_send_size;
    std::unique_ptr<queue_udp_send_t> queue_send;
};

struct manapi::net::worker::http_v3_cloudflare_quiche::connection_stream_t : connection_prepared_t {
    int64_t id;
    connection_t *conn;
    std::unique_ptr<http::request_data_t> req;
};

template<typename T>
requires(manapi::macros::version_is_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) MANAPIHTTP_NOEXCEPT {
    return quiche_h3_event_headers_has_more_frames(static_cast<T>(event));
}

template<typename T>
requires(manapi::macros::version_is_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) MANAPIHTTP_NOEXCEPT {
    return quiche_h3_event_headers_has_body(static_cast<T>(event));
}
template<typename ...Args>
requires(manapi::macros::version_is_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args&&...args) MANAPIHTTP_NOEXCEPT {
    return quiche_h3_send_additional_headers(args...);
}

template<typename ...Args>
requires(manapi::macros::version_is_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
ssize_t manapi_quiche_h3_send_additional_headers_(Args&&...args) MANAPIHTTP_NOEXCEPT { /* skip */ return 0; }

struct udp_send_buff_deleter {
    void operator () (manapi::ev::buff_t *buff) {
        delete[] buff->base;
        delete buff;
    }
};

manapi::net::worker::http_v3_cloudflare_quiche::http_v3_cloudflare_quiche(net::http::site site,
    std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config* config) : udp(std::move(site), std::move(wdata), config) {
    this->flags = 0;
    this->count = 0;
    this->finish = nullptr;
}

manapi::net::worker::http_v3_cloudflare_quiche::~http_v3_cloudflare_quiche() {
    prepared::timer_clear(std::move(this->limit_rate_timer));

    if (this->quiche_h3_config_)
        quiche_h3_config_free(std::exchange(this->quiche_h3_config_, nullptr));

    if (this->quiche_config_)
        quiche_config_free(std::exchange(this->quiche_config_, nullptr));
}

std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche> manapi::net::worker::http_v3_cloudflare_quiche::create(
    net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata,
    std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::http_v3_cloudflare_quiche>(std::move(site), std::move(wdata), config.get());
    worker->self_ = std::weak_ptr (worker);
    return std::move(worker);
}

manapi::future<manapi::error::status> manapi::net::worker::http_v3_cloudflare_quiche::init(std::size_t deep) {
    auto status = co_await udp::init(deep + 1);
    if (!status)
        co_return std::move(status);
    typedef manapi::internal::config_interface cv;
    
#define as_cv_bool cv::get_config_param<bool>
#define as_cv_string cv::get_config_param<std::string>
#define as_cv_uint64_t cv::get_config_param<uint64_t>

    auto const verify_peer = as_cv_bool (this->config_->ssl, "verify_peer", true);
    auto const cert = as_cv_string(this->config_->ssl, "cert", {});
    auto const key = as_cv_string(this->config_->ssl, "key", {});

    auto const quic_debug = as_cv_bool(this->config_->quic, "debug", false);
    auto const active_migration = as_cv_bool(this->config_->quic, "active_migration", false);
    auto const dcid_reuse = as_cv_bool(this->config_->quic, "dcid_reuse", true);
    auto const hystart = as_cv_bool(this->config_->quic, "hystart", true);
    auto const pacing = as_cv_bool(this->config_->quic, "pacing", true);
    auto const early_data = as_cv_bool(this->config_->quic, "early_data", true);
    auto const grease = as_cv_bool(this->config_->quic, "grease", true);
    auto const pmtu = as_cv_bool(this->config_->quic, "discover_pmtu", false);

    auto const initial_max_data =  as_cv_uint64_t(this->config_->quic, "initial_max_data", 10000000);
    auto const initial_max_stream_data_bidi_local =  as_cv_uint64_t(this->config_->quic, "initial_max_stream_data_bidi_local", 1000000);
    auto const initial_max_stream_data_bidi_remote =  as_cv_uint64_t(this->config_->quic, "initial_max_stream_data_bidi_remote", 1000000);
    auto const initial_max_stream_data_uni =  as_cv_uint64_t(this->config_->quic, "initial_max_stream_data_uni", 1000000);
    auto const max_amplification_factor = as_cv_uint64_t(this->config_->quic, "max_amplification_factor", 3);
    auto const active_connection_id_limit = as_cv_uint64_t(this->config_->quic, "active_connection_id_limit", 2);
    auto const max_ack_delay = as_cv_uint64_t(this->config_->quic, "max_ack_delay", 25);
    auto const max_idle_timeout = as_cv_uint64_t(this->config_->quic, "max_idle_timeout", 0);

    auto const max_concurrent_streams = this->config_->max_concurrent_streams > 0 ? this->config_->max_concurrent_streams : 6;
    auto const window_connection_size = this->config_->window_connection_size > 0 ? this->config_->window_connection_size : 2000000;
    auto const window_stream_size = this->config_->window_stream_size > 0 ? this->config_->window_stream_size : 400000;

#undef as_cv_bool
#undef as_cv_string
#undef as_cv_uint64_t

    do {
        if (auto rhs = this->udp_accept_->recv_start()) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't start recv due to result - {}", rhs);
            goto err;
        }

        this->quiche_config_ = quiche_config_new(QUICHE_PROTOCOL_VERSION);
        this->quiche_h3_config_ = quiche_h3_config_new();

        if (quiche_config_load_cert_chain_from_pem_file(this->quiche_config_, cert.data()))
            co_return error::status_internal("cf quiche: failed to load cert chain from pem file");

        if (quiche_config_load_priv_key_from_pem_file(this->quiche_config_, key.data()))
            co_return error::status_internal("cf quiche: failed to load private key from pem file");

        if(quiche_config_set_application_protos(this->quiche_config_,
            reinterpret_cast <const uint8_t *> (QUICHE_H3_APPLICATION_PROTOCOL), sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1)) {
            goto err;
        }


        quiche_config_set_max_idle_timeout(this->quiche_config_, max_idle_timeout);
        quiche_config_set_max_recv_udp_payload_size(this->quiche_config_, MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE);
        quiche_config_set_max_send_udp_payload_size(this->quiche_config_, MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE);

        quiche_config_set_initial_max_data(this->quiche_config_, initial_max_data);
        quiche_config_set_initial_max_stream_data_bidi_local(this->quiche_config_, initial_max_stream_data_bidi_local);
        quiche_config_set_initial_max_stream_data_bidi_remote(this->quiche_config_, initial_max_stream_data_bidi_remote);
        quiche_config_set_initial_max_stream_data_uni(this->quiche_config_, initial_max_stream_data_uni);
        quiche_config_set_initial_max_streams_bidi (this->quiche_config_, max_concurrent_streams);
        quiche_config_set_initial_max_streams_uni (this->quiche_config_, max_concurrent_streams);
        quiche_config_set_disable_active_migration (this->quiche_config_, !active_migration);
        quiche_config_set_disable_dcid_reuse(this->quiche_config_, !dcid_reuse);
        quiche_config_set_max_connection_window(this->quiche_config_, window_connection_size);
        quiche_config_set_max_stream_window(this->quiche_config_, window_stream_size);
        quiche_config_set_max_amplification_factor(this->quiche_config_, max_amplification_factor);
        quiche_config_set_active_connection_id_limit(this->quiche_config_, active_connection_id_limit);
        quiche_config_enable_hystart(this->quiche_config_, hystart);
        quiche_config_enable_pacing(this->quiche_config_, pacing);
        if (early_data) quiche_config_enable_early_data(this->quiche_config_);
        quiche_config_grease(this->quiche_config_, grease);
        quiche_config_discover_pmtu(this->quiche_config_, pmtu);
        quiche_config_set_max_ack_delay(this->quiche_config_, max_ack_delay);



        quiche_config_verify_peer(this->quiche_config_, verify_peer);
        if (quic_debug) {
            if (quiche_enable_debug_logging([] (const char *line, void *argp)
                -> void {
                manapi_log_debug(line);
            }, this)) {
                /* already exists */
            }
        }

        auto &conn = this->config_->quic;
        if (conn.is_object()) {
            auto it = conn.as_object().find("cc_algo");
            if (it != conn.as_object().end() && it->second.is_string()) {
                auto &cc_algo = it->second.as_string();
                if (!cc_algo.empty()) {
                    quiche_cc_algorithm algo = QUICHE_CC_RENO;

                    if (manapi::string::equals("cubic", cc_algo, 0b10))
                        algo = QUICHE_CC_CUBIC;

                    else if (manapi::string::equals("reno", cc_algo, 0b10))
                        algo = QUICHE_CC_RENO;

                    else if (manapi::string::equals("bbr", cc_algo, 0b10))
                        algo = QUICHE_CC_BBR;

                    else if (manapi::string::equals("bbr2", cc_algo, 0b10))
                        algo = QUICHE_CC_BBR2;
                    else
                        co_return error::status_internal("cf quiche:Invalid cc_algo");

                    quiche_config_set_cc_algorithm (this->quiche_config_, algo);
                }
            }
        }

        /* every 1 second */
        auto rhs = manapi::async::current()->timerpool()->append_interval_sync(1000,
            [this] (manapi::timer t) -> void { this->update_limit_rate(); });

        if (!rhs.ok())
            co_return rhs.err();

        this->limit_rate_timer = rhs.unwrap();
    }
    while (0);
    co_return error::status_ok();
err:
    co_return error::status_internal("quiche: init() failed");
}

void manapi::net::worker::http_v3_cloudflare_quiche::stop(std::function<void()> cb) {
    std::function<void()> cb_next;
    MANAPIHTTP_MUST_ALLOC_START
    cb_next = [this, cb = std::move(cb)] () -> void {
        this->flags |= WORKER_BASE_FLAG_CLOSED;
        this->finish = cb;

        if (!this->count)
            this->finish();
    };
    MANAPIHTTP_MUST_ALLOC_END;
    udp::stop(std::move(cb_next));
}

void manapi::net::worker::http_v3_cloudflare_quiche::close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT {
    if (!conn)
        return;

    auto s = conn->as<connection_stream_t>();

    if (s->flags & HTTP_V3_STREAM_REMOVED) {
        return;
    }

    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "quiche:close_connection() %p stream=%zu flags=%d",
        s->conn, static_cast<std::size_t>(s->id), flags);

    s->flags |= HTTP_V3_STREAM_CLOSED;

    s->flags |= HTTP_V3_STREAM_REMOVED;

    prepared::event_callback_clear(conn, s);
    prepared::top_buffer_clear(s);

    conn->cancellation.cancel();
}

void manapi::net::worker::http_v3_cloudflare_quiche::reset_all_streams_(connection_t *conn_data) MANAPIHTTP_NOEXCEPT {
    for (const auto &s : conn_data->streams)
        this->close_connection(s.second, CLOSE_CONN_ERR);
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v3_cloudflare_quiche::ipdata(
    worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_stream_t>();
    return data->conn->self->ipdata.get();
}

void manapi::net::worker::http_v3_cloudflare_quiche::quiche_timeout_(manapi::timer w, const shared_conn &connection) MANAPIHTTP_NOEXCEPT {
    auto conn_data = connection->as<connection_t>();

    quiche_conn_on_timeout(conn_data->conn);

    http_v3_cloudflare_quiche::quiche_timeout_again_(conn_data);
    conn_data->worker->quiche_flush_egress_(conn_data);
    http_v3_cloudflare_quiche::flush_connection_closed_(connection, conn_data);
}

int manapi::net::worker::http_v3_cloudflare_quiche::grab_headers_(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) MANAPIHTTP_NOEXCEPT {
    try {
        auto req = static_cast<grab_headers_t *> (argp);
        auto const key = std::string_view{reinterpret_cast<const char *>(name), name_len};
        auto const val = std::string_view{reinterpret_cast<const char *>(value), value_len};

        if (key.size() > req->config->max_header_key_size)
            return 1;

        if (val.size() > req->config->max_header_value_size)
            return 1;

        (*req->headers_size) += key.size() + val.size();

        if (*req->headers_size > req->max_headers_size)
            return 1;

        auto it =  req->headers->find(key);
        if (it == req->headers->end()) {
            req->headers->insert({std::string{key}, std::string{val}});
        }
        else {
            it->second.append(val);
        }

        return 0;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s:%s failed due to %s", "cf quiche", "grab headers", e.what());
    }
    return 1;
}

bool manapi::net::worker::http_v3_cloudflare_quiche::validate_token_(char *token, size_t token_len, char *odcid, size_t *odcid_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) MANAPIHTTP_NOEXCEPT {
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

int manapi::net::worker::http_v3_cloudflare_quiche::gen_mint_token_(char *dcid, size_t dcid_len, char *token, size_t *token_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) MANAPIHTTP_NOEXCEPT {
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

manapi::error::status quiche_udp_send_data_ (manapi::ev::udp *udp, manapi::ev::buff_t *buff, sockaddr *send_addr, socklen_t send_len) MANAPIHTTP_NOEXCEPT {
    try {
        ssize_t const sent = udp->try_send(buff, 1, send_addr);
        if (sent != buff->len) {
            std::unique_ptr<manapi::ev::buff_t, udp_send_buff_deleter> buffs (new manapi::ev::buff_t{});
            auto queue_item = std::make_unique<manapi::net::worker::http_v3_cloudflare_quiche::queue_udp_send_t>();

            buffs->len = buff->len;
            buffs->base = new char[buff->len];

            std::unique_ptr<char, manapi::ev::chars_deleter> addr_storage;

            addr_storage.reset(new char[send_len]);

            memcpy (addr_storage.get(), send_addr, send_len);

            auto buffptr = buffs.get();
            auto send_to = reinterpret_cast<sockaddr *>(addr_storage.get());

            manapi::ev::udp_send_cb cb = [buffs = std::move(buffs), addr_storage = std::move(addr_storage)]
                    (const manapi::ev::shared_udp_send &n, int status)
                        -> void {
                if (status) {
                    manapi_log_trace (manapi::debug::LOG_TRACE_MEDIUM, "udp_send:Send failed due to %s(%d):%s",
                        manapi::ev::namerror(status), status, manapi::ev::strerror(status));
                }
            };

            auto res = manapi::async::current()->eventloop()->create_watcher_udp_send(
                udp, std::move(cb), buffptr, 1, send_to);

            if (!res.ok())
                return res.err();
        }

        return manapi::error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return manapi::error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "quiche_udp_send_data_ failed", e.what());
    }

    return manapi::error::status_internal("quiche_udp_send_data_ failed");
}

int manapi::net::worker::http_v3_cloudflare_quiche::quiche_flush_egress_(connection_t *data) MANAPIHTTP_NOEXCEPT {
    if (data->flags & CONN_CLOSED)
        return -1;

    try {
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

            ev::buff_t buff;
            buff.base = reinterpret_cast<char *> (out);
            buff.len = static_cast<std::size_t>(written);
            ssize_t const sent = this->udp_accept_->try_send(&buff, 1, reinterpret_cast<sockaddr *>(&send_info.to));

            if (sent != written) {
                if (sent == ev::ERR_AGAIN) {
                    std::unique_ptr<ev::buff_t, udp_send_buff_deleter> buffs (new ev::buff_t{});
                    auto queue_item = std::make_unique<queue_udp_send_t>();

                    buffs->len = written;
                    buffs->base = new char[written];

                    std::unique_ptr<char, ev::chars_deleter> addr_storage;

                    addr_storage.reset(new char[send_info.to_len]);

                    memcpy (addr_storage.get(), &send_info.to, send_info.to_len);

                    auto buffptr = buffs.get();
                    auto send_to = reinterpret_cast<sockaddr *>(addr_storage.get());

                    ev::udp_send_cb cb = [buffs = std::move(buffs), addr_storage = std::move(addr_storage), data]
                        (const ev::shared_udp_send &n, int status)
                            -> void {
                        if (status) {
                            manapi_log_trace (debug::LOG_TRACE_MEDIUM, "udp_send:Send failed due to %s(%d):%s",
                                ev::namerror(status), status, ev::strerror(status));

                            if (status == ev::ERR_CANCELED)
                                return;
                        }

                        assert(data->queue_send->w == n.get());

                        data->queue_send = std::move(data->queue_send->next);
                        data->queue_send_size--;

                        if (!data->queue_send_size) {
                            for (auto & s : data->streams) {
                                data->worker->feed_event(s.second, ev::WRITE, nullptr, 0, nullptr);
                            }
                        }
                    };

                    auto res = manapi::async::current()->eventloop()->create_watcher_udp_send(
                        this->udp_accept_.get(), std::move(cb), buffptr, 1, send_to);

                    if (res.ok()) {
                        auto w = res.unwrap();

                        queue_item->w = w.get();
                        queue_item->next = std::move(data->queue_send);

                        data->queue_send = std::move(queue_item);
                        data->queue_send_size++;

                        return 0;
                    }
                }
                return -1;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "quiche_flush_egress_()", e.what());
        return -1;
    }

    return 0;
}

void manapi::net::worker::http_v3_cloudflare_quiche::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT {
    prepared::waiting(conn, state);
}

void manapi::net::worker::http_v3_cloudflare_quiche::quiche_timeout_again_(connection_t *connection) MANAPIHTTP_NOEXCEPT {
    auto const repeat = static_cast<int64_t> (quiche_conn_timeout_as_millis(connection->conn));

    auto rhs = connection->timeout.again(repeat > 0 ? repeat : 200);
    if (!rhs) {
        /* failure */
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::connection_interface_eraser(connection *ptr) MANAPIHTTP_NOEXCEPT {
    auto const uptr = std::unique_ptr<manapi::net::worker::connection> (ptr);
    auto const p = uptr->as<connection_t>();
    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "quiche: close %p conn", p);
    delete p;
}

void manapi::net::worker::http_v3_cloudflare_quiche::stream_interface_eraser(worker::connection *ptr) MANAPIHTTP_NOEXCEPT {
    auto uptr = std::unique_ptr<manapi::net::worker::connection> (ptr);
    delete uptr->as<connection_stream_t>();
}

void quiche_set_header_(quiche_h3_header *header, std::string_view key, std::string_view value) MANAPIHTTP_NOEXCEPT {
    *header = {
        .name = reinterpret_cast<const uint8_t *> (key.data()),
        .name_len = key.size(),
        .value = reinterpret_cast<const uint8_t *> (value.data()),
        .value_len = value.size()
    };
}

static void wrk_close_connection ( manapi::net::worker::shared_conn conn, manapi::net::worker::shared_conn stream_conn, manapi::net::worker::base *w, bool ok) {
    MANAPIHTTP_MUST_ALLOC_START
    manapi::async::current()->etaskpool()->append_super_task(
        [w, ok, stream_conn, conn] () -> void {
        auto const s = stream_conn->as<manapi::net::worker::http_v3_cloudflare_quiche::connection_stream_t>();
        auto const conndata = conn->as<manapi::net::worker::http_v3_cloudflare_quiche::connection_t>();

        w->close_connection(stream_conn, ok ? 0 : manapi::net::worker::CLOSE_CONN_ERR);
        conndata->streams.erase(s->id);
        manapi::net::worker::http_v3_cloudflare_quiche::flush_connection_closed_(conn, conndata);
    });
    MANAPIHTTP_MUST_ALLOC_END
}

void manapi::net::worker::http_v3_cloudflare_quiche::onrecv(const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) MANAPIHTTP_NOEXCEPT {
    std::array<char, 17> iparr{};
    shared_conn connection{nullptr};

    iparr[0] = static_cast<char>(http::version_ip_by_addr (addr));
    http::ip_by_addr(addr, iparr.data() + 1);

    socklen_t sockaddr_len = 0;
    uint8_t out[MANAPIHTTP_QUICHE_MAX_DATAGRAM_SIZE];

    auto sockaddr_src = (sockaddr *)addr;
    sockaddr_len = async::socklen (sockaddr_src);

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

    bool not_exists = true;
    decltype(this->connections)::value_type::second_type::iterator it;
    auto conn_by_ip = this->connections.find(iparr);

    if (conn_by_ip != this->connections.end()) {
        it = conn_by_ip->second.find(std::string_view{dcid, dcid_len});
        not_exists = it == conn_by_ip->second.end();
    }

    if (not_exists) {
        if (!quiche_version_is_supported(version)) {
            const int64_t written = quiche_negotiate_version(reinterpret_cast<uint8_t *> (scid),
                scid_len, reinterpret_cast<uint8_t *> (dcid), dcid_len, out, sizeof (out));

            if (written < 0) {
                return;
            }

            ev::buff_t buff2;
            buff2.base = reinterpret_cast<char *> (out);
            buff2.len = static_cast<std::size_t>(written);
            auto rhs = quiche_udp_send_data_(this->udp_accept_.get(), &buff2, reinterpret_cast<sockaddr *>(sockaddr_src), sockaddr_len);

            if (!rhs) {
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

            auto res = manapi::crypto::random_string(MANAPIHTTP_QUICHE_CONN_ID_SIZE);
            if (!res.ok())
                return;

            std::string new_scid = res.unwrap();
            const int64_t written = quiche_retry(reinterpret_cast<uint8_t *> (scid), scid_len, reinterpret_cast<uint8_t *>(dcid), dcid_len,
             reinterpret_cast<uint8_t *>(new_scid.data()), new_scid.size(), reinterpret_cast<uint8_t *>(token), token_len, version, out, sizeof (out));

            if (written < 0) {
                return;
            }

            ev::buff_t buff2;
            buff2.base = reinterpret_cast<char *> (out);
            buff2.len = static_cast<std::size_t>(written);
            auto rhs = quiche_udp_send_data_(this->udp_accept_.get(), &buff2, reinterpret_cast<sockaddr *>(sockaddr_src), sockaddr_len);

            if (!rhs) {
                /* failed to send */
            }

            return;
        }

        if (!http_v3_cloudflare_quiche::validate_token_(token, token_len, odcid, &odcid_len, sockaddr_src, sockaddr_len)) {
            return;
        }

        if (dcid_len != MANAPIHTTP_QUICHE_CONN_ID_SIZE) {
            return;
        }

        auto quiche_conn_ = quiche_accept(reinterpret_cast<uint8_t *> (dcid), dcid_len,
         reinterpret_cast<uint8_t *>(odcid), odcid_len, reinterpret_cast<sockaddr *> (&this->config_->server_addr), this->config_->server_len,
         reinterpret_cast<sockaddr *>(sockaddr_src), sockaddr_len, this->quiche_config_);

        if (!quiche_conn_) {
            return;
        }

        try {
            std::string conn_id{dcid, dcid_len};

            auto p = std::make_unique<connection_t>(std::string_view{conn_id}, 0, this, quiche_conn_, nullptr, std::map<int64_t, shared_conn>{});
            connection = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, connection_interface_eraser);
            p.release();

            conn_data = connection->as<connection_t>();

            conn_data->timeout = manapi::async::current()->timerpool()->append_timer_sync (1000, [connection = std::weak_ptr (connection)] (manapi::timer t)
                -> void { http_v3_cloudflare_quiche::quiche_timeout_(std::move(t), connection.lock()); }).unwrap();

            conn_data->self = connection;

            connection->ipdata = std::make_unique<decltype(connection)::element_type::ipdata_t>();
            memcpy (connection->ipdata->client.data, sockaddr_src, sockaddr_len);
            connection->ipdata->len = sockaddr_len;


            if (conn_by_ip == this->connections.end())
                conn_by_ip = this->connections.insert({iparr, decltype(this->connections)::value_type::second_type{}}).first;

            auto res = conn_by_ip->second.insert({
                std::move(conn_id),
                connection
            });


            if (!res.second)
                throw std::runtime_error("already exists");

            conn_data->cid = res.first->first;

            this->count++;
        }
        catch (std::exception const &e) {
            manapi_log_error("%s:%s failed due to %s", "cf quiche", "onrecv", e.what());

            quiche_conn_free(quiche_conn_);

            if (conn_data) {
                conn_data->conn = nullptr;
            }

            connection = nullptr;
        }

        try {
            if (!connection)
                return;

            if (this->count > this->config_->max_connections + this->config_->max_connections ||
                conn_by_ip->second.size() > this->config_->max_connections_by_ip + this->config_->max_connections_by_ip) {
                auto const rhs = quiche_conn_close(conn_data->conn, false, 0x02, reinterpret_cast<const uint8_t *> (many_connections_msg.data()), many_connections_msg.size());
                if (rhs)
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %d for conn %p", "quche_conn_close", conn_data, rhs);
            }

            if (this->count > this->config_->max_connections ||
                conn_by_ip->second.size() > this->config_->max_connections_by_ip) {
                auto const rhs = quiche_conn_close(conn_data->conn, false, 0x02,  reinterpret_cast<const uint8_t *> (many_connections_msg.data()), many_connections_msg.size());
                if (rhs)
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %d for conn %p", "quche_conn_close", conn_data, rhs);
            }
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "quiche: onrecv", e.what());
            return;
        }
    }
    else {
        connection = it->second;
    }

    conn_data = connection->as<connection_t>();

    quiche_recv_info recv_info {
        (sockaddr *)(sockaddr_src),
         sockaddr_len,
         (sockaddr *)&this->config_->server_addr,
         this->config_->server_len
    };


    ssize_t done = quiche_conn_recv(conn_data->conn, reinterpret_cast<uint8_t *> (buff), size, &recv_info);

    if (done < 0) {
        return;
    }

    if ((quiche_conn_is_in_early_data(conn_data->conn) || quiche_conn_is_established(conn_data->conn)) && !conn_data->http3_conn) {
        conn_data->http3_conn = quiche_h3_conn_new_with_transport(conn_data->conn, this->quiche_h3_config_);

        if (!conn_data->http3_conn) {
            manapi_log_trace(debug::LOG_TRACE_HIGH, "quiche: !conn_data->http3_conn failed");
            return;
        }
    }

    quiche_h3_event *event{nullptr};

    try {
        if (conn_data->http3_conn
            && !(conn_data->flags & (CONN_REMOVED|CONN_CLOSED))) {
            http_v3_cloudflare_quiche::flush_write_(connection, conn_data);


            while (true) {
                int64_t stream_id = quiche_h3_conn_poll(conn_data->http3_conn, conn_data->conn, &event);

                if (stream_id < 0) {
                    break;
                }

                switch (quiche_h3_event_type(event)) {
                    case QUICHE_H3_EVENT_HEADERS: {
                        auto stream_it = conn_data->streams.find(stream_id);
                        if (stream_it == conn_data->streams.end()) {
                            shared_conn stream_conn{nullptr};

                            try {
                                auto p = std::make_unique<connection_stream_t>();

                                p->id = stream_id;
                                p->conn = conn_data;

                                stream_conn = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, stream_interface_eraser);
                                p.release();

                                auto s = stream_conn->as<connection_stream_t>();

                                s->req = std::make_unique<http::request_data_t>();
                                s->top = std::make_unique<connection_io>();

                                grab_headers_t grab_headers_data {};
                                grab_headers_data.config = this->config_;
                                grab_headers_data.headers = &s->req->headers;
                                grab_headers_data.headers_size = &s->req->headers_size;
                                grab_headers_data.max_headers_size = this->config_->max_headers_size;

                                if (quiche_h3_event_for_each_header(event, http_v3_cloudflare_quiche::grab_headers_, &grab_headers_data)) {
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
                                    auto contentlength = s->req->headers.find(http::header::CONTENT_LENGTH);
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
                                auto cdata = std::make_unique<http::internal::handle_data_t>(stream_conn, this->copy(),
                                    req_ptr, std::make_unique<http::internal::cont_callback_cb_t>(
                                    [this, stream_conn, conn = connection] (bool ok) mutable
                                    -> void {
                                        auto const s = stream_conn->as<connection_stream_t>();
                                        auto const conndata = conn->as<connection_t>();
                                        s->ev_callback = nullptr;
                                        s->flags = ev::DISCONNECT;

                                        wrk_close_connection(conn, stream_conn, this, ok);
                                }));

                                if (!conn_data->streams.insert({s->id, std::move(stream_conn)}).second) {
                                    goto err;
                                }

                                // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                                // this->event_flags(conn, 0);

                                cdata->router = this->site().handler(req_ptr);
                                net::http::internal::handle_income_request(std::move(cdata), http::OK_200);
                            }
                            catch (std::exception const &e) {
                                manapi_log_error("%s: %s failed due to %s", "cf quiche", "onrecv", e.what());
                                if (stream_conn)
                                    wrk_close_connection(connection, stream_conn, this, false);
                            }
                        }
                        else {
                            auto &sconn = stream_it->second;
                            auto s = sconn->as<connection_stream_t>();

                            grab_headers_t grab_headers_data {};
                            grab_headers_data.config = this->config_;
                            grab_headers_data.headers = &s->req->trailers;
                            grab_headers_data.headers_size = &s->req->trailers_size;
                            grab_headers_data.max_headers_size = s->req->handler->trailers_size;

                            if (quiche_h3_event_for_each_header(event, http_v3_cloudflare_quiche::grab_headers_, &grab_headers_data)) {
                                this->close_connection(sconn, CLOSE_CONN_ERR);
                            }
                        }

                        break;
                    }
                    case QUICHE_H3_EVENT_DATA: {
                        auto stream_it = conn_data->streams.find(stream_id);
                        if (stream_it == conn_data->streams.end()) {
                            break;
                        }

                        auto &sconn = stream_it->second;

                        if (const auto rhs = this->flush_read_(sconn)) {
                            if (rhs != CONN_IO_WANT_READ) {
                                goto err;
                            }
                        }

                        // skip

                        break;
                    }
                    case QUICHE_H3_EVENT_FINISHED: {
                        auto stream_it = conn_data->streams.find(stream_id);
                        if (stream_it == conn_data->streams.end()) {
                            break;
                        }

                        auto &stream_connection = stream_it->second;
                        auto stream = stream_connection->as<connection_stream_t>();

                        stream->flags |= HTTP_V3_STREAM_RECV_END;

                        if (auto rhs = this->flush_read_(stream_connection)) {
                            if (rhs != CONN_IO_WANT_READ)
                                goto err;
                        }

                        break;
                    }
                    case QUICHE_H3_EVENT_RESET: {
                        auto stream_it = conn_data->streams.find(stream_id);
                        if (stream_it == conn_data->streams.end()) {
                            break;
                        }

                        this->close_connection(stream_it->second, CLOSE_CONN_ERR);

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
                event = nullptr;
            }
        }


        /* setup timeout */
        if (connection) {
            this->quiche_flush_egress_(conn_data);
            http_v3_cloudflare_quiche::quiche_timeout_again_(conn_data);
            http_v3_cloudflare_quiche::flush_connection_closed_(connection, conn_data);
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s", "cf quiche", "onrecv", e.what());

        goto err;
    }

    return;

    err: {
        if (event) {
            quiche_h3_event_free(event);
            event = nullptr;
        }

        if (connection)
            flush_connection_closed_(connection, connection->as<connection_t>());
    }
}

ssize_t manapi::net::worker::http_v3_cloudflare_quiche::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
    auto data = conn->as<connection_stream_t>();

    auto const size = manapi::net::worker::http_v3_cloudflare_quiche::buffs_cut_by_size(buff, nbuff, this->config_->speed_limit_rate - data->transfered, finish);

    if (!size)
        return 0;

    return sync_write_ex (conn, buff, nbuff, size, finish, static_cast<int>(this->config_->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v3_cloudflare_quiche::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXCEPT {
    auto s = conn->as<connection_stream_t>();

    if (s->flags & ev::DISCONNECT)
        return -1;

    if (s->conn->queue_send_size > maxcnt)
        return 0;

    ssize_t res = 0;

    for (uint32_t i = 0; i < nbuff; ) {
        auto rhs = quiche_h3_send_body(s->conn->http3_conn, s->conn->conn, s->id,
            reinterpret_cast <const uint8_t *> (buff[i].base), buff[i].len, finish && (res + buff[i].len == size));

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

        if (s->conn->worker->quiche_flush_egress_(s->conn))
            return -1;

        if (!rhs)
            break;

        if (buff[i].len == rhs)
            i++;
        else {
            buff[i].base += rhs;
            buff[i].len -= rhs;
        }

        s->transfered += rhs;


        res += rhs;
    }

    if (s->conn->worker->quiche_flush_egress_(s->conn))
        return -1;

    return res;
}

int manapi::net::worker::http_v3_cloudflare_quiche::event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::event_flags(conn);
}

int manapi::net::worker::http_v3_cloudflare_quiche::event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_stream_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP(data) {
        if ((status & (ev::READ|ev::DISCONNECT)) == ev::READ && data->ev_callback) {

            if (this->flush_read_(conn)) {
                /* error */
            }

            if (this->quiche_flush_egress_(data->conn)) {
                /* error */
            }

            if ((status & CONN_READ) && (status & CONN_RECV_END)) {
                assert(!data->top->recv_size);
                if (http_v3_cloudflare_quiche::call_user_callback(&data->ev_callback,conn, CONN_RECV_END, nullptr, 0, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
        }

        MANAPIHTTP_WORKER_EVENT_BREAK(data);
    }

    return prev;
}

manapi::net::worker::worker_watcher_cb manapi::net::worker::http_v3_cloudflare_quiche::event_on( const shared_conn &conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT {
    return prepared::event_on(conn, std::move(callback));
}

void manapi::net::worker::http_v3_cloudflare_quiche::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    prepared::feed_event(this, conn, flags, buff, size, p);
}

bool manapi::net::worker::http_v3_cloudflare_quiche::is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_stream_t>();
    return prepared::is_writable(this->config_, conn, data);
}

std::size_t manapi::net::worker::http_v3_cloudflare_quiche::recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    return prepared::recv_count(conn);
}

manapi::bytebuffer manapi::net::worker::http_v3_cloudflare_quiche::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::recv_first_buffer(conn);
}

//
// void manapi::net::worker::http_v3_cloudflare_quiche::recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) {
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


void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate() MANAPIHTTP_NOEXCEPT {
    /* in the event loop */
    for (auto nit = this->connections.begin(); nit != this->connections.end(); ) {
        if (nit->second.empty()) {
            nit = this->connections.erase(nit);
            continue;
        }

        for (auto &conn : nit->second) {
            this->update_limit_rate_connection(conn.second);
        }

        ++nit;
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate_connection(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const conn_data = conn->as<connection_t>();

    for (const auto &s : conn_data->streams) {
        this->update_limit_rate_stream(s.second);
    }

    quiche_flush_egress_(conn_data);
}

void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate_stream(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const conn_data = conn->as<connection_stream_t>();

    if (conn_data->transfered >= this->config_->speed_limit_rate
        && conn_data->ev_callback) {
        conn_data->transfered = 0;

        if (conn_data->flags & ev::READ)
            this->flush_read_(conn);

        if (conn_data->flags & ev::WRITE)
            if(manapi::net::worker::http_v3_cloudflare_quiche::call_user_callback(&conn_data->ev_callback, conn, ev::WRITE, nullptr, 0, nullptr)) {
                this->close_connection(conn, CLOSE_CONN_EOF);
            }
    }
    else {
        conn_data->transfered_k += conn_data->transfered;

        if (--conn_data->speed_min_delay == 0) {
            if (conn_data->flags & HTTP_V3_STREAM_IO_WAITING
                && conn_data->transfered_k < this->config_->speed_check_bytes) {
                this->close_connection(conn, CLOSE_CONN_EOF);
                return;
            }
            conn_data->transfered_k = 0;
            conn_data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
        }
        conn_data->transfered = 0;
    }
    if (conn_data->flags & ev::WRITE) {
        if (manapi::net::worker::http_v3_cloudflare_quiche::call_user_callback(&conn_data->ev_callback,conn, ev::WRITE, nullptr, 0, nullptr))
            this->close_connection(conn, CLOSE_CONN_EOF);
    }
}

int manapi::net::worker::http_v3_cloudflare_quiche::flush_read_buffers_(const shared_conn &conn, connection_stream_t *s) MANAPIHTTP_NOEXCEPT {
    prepared::flush_read_(this, conn, s);
    return ERR_OK;
}

int manapi::net::worker::http_v3_cloudflare_quiche::flush_read_(const shared_conn &stream) MANAPIHTTP_NOEXCEPT {
    auto const s = stream->as<connection_stream_t>();
    auto top = &s->top->recv;
    ssize_t rhs;
    auto const &maxcnt = this->config_->max_buffer_stack;

    if (quiche_conn_stream_finished(s->conn->conn, s->id))
        s->flags |= CONN_RECV_END;

    if (auto const res = this->flush_read_buffers_(stream, s))
        return res;

    char buffer[65536];


    while (!prepared::read_buffs_is_full(s->top.get(), maxcnt)) {
        rhs = quiche_h3_recv_body(s->conn->http3_conn, s->conn->conn, s->id,
            reinterpret_cast<uint8_t *>(buffer), sizeof (buffer));

        if (rhs > 0) {
            auto copy = manapi::net::worker::http_v3_cloudflare_quiche::connection_io_send(top, buffer, rhs, &this->bufferpool(), this->config_->buffer_size,
                &s->top->recv_size, 1e5);

            if (copy != rhs)
                return CONN_IO_ERROR;
        }

        if (quiche_conn_stream_finished(s->conn->conn, s->id))
            s->flags |= CONN_RECV_END;

        if (auto const res = this->flush_read_buffers_(stream, s))
            return res;

        if (rhs > 0) {
            continue;
        }

        break;
    }

    return CONN_IO_OK;

    // do {
    //     if (!top->last_deque || top->last_deque->buffer.size() == top->deque_cursor) {
    //         if (auto const res = this->flush_read_buffers_(stream, s)) {
    //             return res;
    //         }
    //
    //         if (s->top->recv_size >= maxcnt)
    //             return CONN_IO_WANT_READ;
    //
    //         auto bufres = this->bufferpool().buffer(this->config_->buffer_size);;
    //         if (!bufres.ok())
    //             return CONN_IO_ERROR;
    //
    //         auto buffer = bufres.unwrap();
    //
    //         std::unique_ptr<buffer_deque> obj (new (std::nothrow) buffer_deque(std::move(buffer), nullptr));
    //
    //         if (!obj)
    //             return CONN_IO_ERROR;
    //
    //         if (top->last_deque) {
    //             top->last_deque->next = std::move(obj);
    //             top->last_deque = top->last_deque->next.get();
    //         }
    //         else {
    //             assert(!top->deque);
    //             top->deque = std::move(obj);
    //             top->last_deque = top->deque.get();
    //             top->deque_current = 0;
    //         }
    //
    //         top->deque_cursor = 0;
    //         s->top->recv_size++;
    //     }
    //
    //     rhs = quiche_h3_recv_body(s->conn->http3_conn, s->conn->conn, s->id, reinterpret_cast<uint8_t *>(top->last_deque->buffer.data() + top->deque_cursor),
    //         static_cast<int>(top->last_deque->buffer.size() - top->deque_cursor));
    //
    //     if (rhs > 0)
    //         top->deque_cursor += static_cast<int>(rhs);
    //     else
    //         break;
    // }
    // while (true);
    //
    // if (quiche_conn_stream_finished(s->conn->conn, s->id))
    //     s->flags |= CONN_RECV_END;
    //
    // if (auto const res = this->flush_read_buffers_(stream, s)) {
    //     return res;
    // }
    //
    // return CONN_IO_OK;
}

void manapi::net::worker::http_v3_cloudflare_quiche::force_close_(shared_conn conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT {
    if (!conn)
        return;

    if (quiche_conn_is_closed(conn_data->conn)) {
        std::array<char, 17> iparr{};

        auto const sock_data = reinterpret_cast <sockaddr *>(conn->ipdata->client.data);
        iparr[0] = static_cast<char>(http::version_ip_by_addr (sock_data));
        http::ip_by_addr(sock_data, iparr.data() + 1);

        auto const wrk = conn_data->worker;

        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(conn_data->conn, &stats);
        quiche_conn_path_stats(conn_data->conn, 0, &path_stats);

        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "quiche: connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu",
                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        while (conn_data->queue_send) {
            manapi::async::current()->eventloop()->stop_watcher_ptr(conn_data->queue_send->w);
            conn_data->queue_send = std::move(conn_data->queue_send->next);
        }

        auto nit = conn_data->worker->connections.find(iparr);
        if (nit != conn_data->worker->connections.end()) {
            auto it = nit->second.find(conn_data->cid);
            if (it != nit->second.end())
                nit->second.erase(it);
        }

        if (conn_data->timeout) {
            prepared::timer_clear(std::move(conn_data->timeout));
        }

        if (conn_data->http3_conn) {
            quiche_h3_conn_free(std::exchange(conn_data->http3_conn, nullptr));
        }

        quiche_conn_free(std::exchange(conn_data->conn, nullptr));

        conn_data->self.reset();

        wrk->count--;

        if (wrk->flags & WORKER_BASE_FLAG_CLOSED
            && !wrk->count
            && wrk->finish) {
            try {
                wrk->finish();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "force_close: finish()", e.what());
            }
        }
    }
    else {
        if (!(conn_data->flags & CONN_REMOVED)) {
            conn_data->flags|=CONN_REMOVED;
            /** refuse connection */
            auto rhs = quiche_conn_close(conn_data->conn, false, 0x02, reinterpret_cast <const uint8_t *> ("i/o timeout"), sizeof ("i/o timeout") - 1);
            if (rhs) {
                manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %d for conn %p", "quche_conn_close", conn_data, rhs);
            }
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT {
    udp::wrk_global(data);
    /* wrk global not working with cloudflare http3 */
    this->global_.send_response = cloudflare_wrk_http3_send_response;
}

void manapi::net::worker::http_v3_cloudflare_quiche::flush_connection_closed_(const shared_conn &conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT {
    // if (!(conn_data->status & CONN_CLOSED) && (conn_data->status & CONN_HALF_CLOSED) && conn_data->streams.empty()) {
    //     conn_data->status.fetch_xor(CONN_HALF_CLOSED);
    // }

    if (quiche_conn_is_closed(conn_data->conn)) {
        if (!(conn_data->flags & CONN_CLOSED)) {
            conn_data->flags|=(CONN_CLOSED);
            if (conn_data->streams.empty()) {
                force_close_(conn, conn_data);
            }
            else {
                conn_data->worker->reset_all_streams_(conn_data);
            }
        }
        else {
            if (conn_data->streams.empty()) {
                /* retry */
                force_close_(conn, conn_data);
            }
        }
    }
}

void manapi::net::worker::http_v3_cloudflare_quiche::flush_write_(const shared_conn &conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT {
    if (conn_data->worker->quiche_flush_egress_(conn_data)) {
        return;
    }

    /* send data */
    int64_t stream_id;
    quiche_stream_iter *stream = quiche_conn_writable(conn_data->conn);
    while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
        auto stream_it = conn_data->streams.find(stream_id);
        //MANAPIHTTP_LOG("WRITE STREAM:{}", stream_id);
        if (stream_it != conn_data->streams.end()) {
            auto const s = stream_it->second->as<connection_stream_t>();
            if ((s->flags & ev::WRITE) && s->ev_callback) {
                if (manapi::net::worker::http_v3_cloudflare_quiche::call_user_callback(&s->ev_callback, stream_it->second, ev::WRITE, nullptr, 0, nullptr))
                    conn_data->worker->close_connection(stream_it->second, CLOSE_CONN_ERR);
            }
        }
    }
    quiche_stream_iter_free(stream);
}


manapi::future<int> manapi::net::worker::http_v3_cloudflare_quiche::cloudflare_wrk_http3_send_response( const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response *res, bool finish) {
    try {
        struct q_headers_deleter {
            void operator()(quiche_h3_header *v) {
                delete[] v;
            }
        };
        auto s = conn->as<connection_stream_t>();

        auto headers = res->headers();
        std::unique_ptr<quiche_h3_header, q_headers_deleter> q_headers (new quiche_h3_header[headers.size() + 1]);

        std::size_t headers_size = headers.size() + 1;
        std::size_t header_cursor = 0;
        size_t i = 1;

        constexpr size_t max_header_value_len = 1000;
        for (auto header = headers.begin(); header != headers.end(); ++header) {
            size_t header_value_cursor = 0;
            while (true) {
                if (header->first.size() > max_header_value_len) {
                    THROW_MANAPIHTTP_EXCEPTION (ERR_INTERNAL, "header key is too long. Size: {}", header->first.size());
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
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "failed to realloc(...) headers buffer");
                }
            }
        }

        i = 0;

        auto &status_header = headers.operator[](":status");
        status_header = std::to_string(res->status_code());
        quiche_set_header_(q_headers.get()+(i++), ":status", status_header);

        size_t current_headers_size = 0;
        size_t constexpr max_headers_size = 4000;

        using promise = manapi::async::promise_sync<ssize_t>;

        worker_watcher_cb prev_cb{nullptr};
        int prev_events{0};

        const auto rhs = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            prev_events = w->event_flags(conn, ev::WRITE);
            prev_cb = w->event_on(conn,  [&, resolve = std::move(resolve)] (const shared_conn & conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) -> void {
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
                                if constexpr (macros::version_is_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0")) {
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

                            if (s->conn->worker->quiche_flush_egress_(s->conn)) {

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
                        w->event_flags(conn, 0);
                    }
            });

            w->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
        });

        w->event_on(conn, std::move(prev_cb));
        w->event_flags(conn, prev_events);

        if (rhs > 0)
            co_return ERR_OK;

        co_return ERR_ABORTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "quiche: response", e.what());
    }
    co_return ERR_INTERNAL;
}

#endif
