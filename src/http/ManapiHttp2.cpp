#include "../include/http/ManapiHttp2.hpp"

#include "encoding/ManapiUnicode.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "encoding/ManapiURL.hpp"
#include "components/ManapiURLDecodeStream.hpp"

#include "../include/http/ManapiHttp2Interface.hpp"
#include "worker/ManapiTcp.hpp"

#include "../include/ManapiUtils.hpp"

enum http_v2_priority {
    HTTP2_PRIORITY_0 = 0,
    HTTP2_PRIORITY_1,
    HTTP2_PRIORITY_2,
    HTTP2_PRIORITY_3,
    HTTP2_PRIORITY_4,
    HTTP2_PRIORITY_5,
    HTTP2_PRIORITY_6,
    HTTP2_PRIORITY_7,
    HTTP2_PRIORITY_MAX = HTTP2_PRIORITY_7
};

enum http2_frame_type {
    HTTP2_FRAME_DATA = 0x00,
    HTTP2_FRAME_HEADERS = 0x01,
    HTTP2_FRAME_PRIORITY = 0x02,
    HTTP2_FRAME_RST_STREAM = 0x03,
    HTTP2_FRAME_SETTINGS = 0x04,
    HTTP2_FRAME_PUSH_PROMISE = 0x05,
    HTTP2_FRAME_PING = 0x06,
    HTTP2_FRAME_GOAWAY = 0x07,
    HTTP2_FRAME_WINDOW_UPDATE = 0x08,
    HTTP2_FRAME_CONTINUATION = 0x09,
    HTTP2_FRAME_ALTSVC = 0x0a,
    HTTP2_FRAME_ORIGIN = 0x0c,
    HTTP2_FRAME_PRIORITY_UPDATE = 0x10
};

enum http2_flag_type {
    HTTP2_FLAG_HEADERS_END_STREAM     = 0b00000001,
    HTTP2_FLAG_HEADERS_END_HEADERS    = 0b00000100,
    HTTP2_FLAG_HEADERS_PADDED         = 0b00001000,
    HTTP2_FLAG_HEADERS_PRIORITY       = 0b00100000,

    HTTP2_FLAG_SETTINGS_ACK           = 0b00000001,

    HTTP2_FLAG_DATA_PADDED            = 0b00001000,
    HTTP2_FLAG_DATA_END_STREAM        = 0b00000001,

    HTTP2_FLAG_PING_ACK               = 0b00000001,
};

enum http2_setting_type {
    HTTP2_SETTING_RESERVED = 0x00,
    HTTP2_SETTING_HEADER_TABLE_SIZE = 0x01,
    HTTP2_SETTING_ENABLE_PUSH = 0x02,
    HTTP2_SETTING_MAX_CONCURRENT_STREAMS = 0x03,
    HTTP2_SETTING_INITIAL_WINDOW_SIZE = 0x04,
    HTTP2_SETTING_MAX_FRAME_SIZE = 0x05,
    HTTP2_SETTING_MAX_HEADER_LIST_SIZE = 0x06,
    HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x08,
    HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES = 0x09,
    HTTP2_SETTING_TLS_RENEG_PERMITTED = 0x10,
    HTTP2_SETTING_SETTINGS_ENABLE_METADATA = 0x4d44
};

enum http_v2_callback_type {
    HTTP2_CALLBACK_INIT = 0,
    HTTP2_CALLBACK_NEXT_LINE,
    HTTP2_CALLBACK_NEXT_LINE2,
    HTTP2_CALLBACK_SKIP_MSG,
    HTTP2_CALLBACK_PARSE_HEADER_LENGTH,
    HTTP2_CALLBACK_PARSE_NEW_FRAME,
    HTTP2_CALLBACK_PARSE_HEADER_TYPE,
    HTTP2_CALLBACK_SKIP_NULL_OCTECTS,
    HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID,
    HTTP2_CALLBACK_PARSE_HEADER_FLAG,
    HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID,
    HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE,
    HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA,
    HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE,
    HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION,
    HTTP2_CALLBACK_PARSE_PING_DATA,
    HTTP2_CALLBACK_PARSE_SETTING_ID,
    HTTP2_CALLBACK_PARSE_SETTING_VALUE,
    HTTP2_CALLBACK_PARSE_HEADER_DATA,
    HTTP2_CALLBACK_PARSE_BODY_DATA,
    HTTP2_CALLBACK_PARSE_SKIP_N_BYTES,
    HTTP2_CALLBACK_PARSE_FIELD_BLOCK,
    HTTP2_CALLBACK_PARSE_HEADER_PRIORITY,
    HTTP2_CALLBACK_PARSE_HEADER_WEIGHT,
    HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY1,
    HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY2,
    HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY3,
    HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY4,
    HTTP2_CALLBACK_PRIORITY_FRAME,
    HTTP2_CALLBACK_PARSE_NUMBER,
    HTTP2_CALLBACK_PARSE_PRI_UPDATE_FIELD,
    HTTP2_CALLBACK_GOAWAY,
    HTTP2_CALLBACK_ERROR
};

struct http_v2_goaway_t {
    int err_code{0};
    std::string_view err_msg;
};

static constexpr char smlabel[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n";
static constexpr int maxcnt = 1e9;

static std::map <int, manapi::json_mask> const allow_settings {
        {HTTP2_SETTING_RESERVED, manapi::json{"{null}"}},
        {HTTP2_SETTING_ENABLE_PUSH, manapi::json{"{integer(>=0 <=1)}"}},
        {HTTP2_SETTING_MAX_FRAME_SIZE, manapi::json{"{integer(>=16384 <=16777215)}"}},
        {HTTP2_SETTING_HEADER_TABLE_SIZE, manapi::json{"{integer(>=2048 <=65536)}"}},
        {HTTP2_SETTING_INITIAL_WINDOW_SIZE, manapi::json{"{integer(>=0 <=2147483647)}"}},
        {HTTP2_SETTING_MAX_HEADER_LIST_SIZE, manapi::json{"{integer(>=1024 <=262144)}"}},
        {HTTP2_SETTING_TLS_RENEG_PERMITTED, manapi::json{"{integer(0)}"}},
        {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, manapi::json{"{integer(>=1 <=2147483647)}"}},
        {HTTP2_SETTING_SETTINGS_ENABLE_METADATA, manapi::json{"{integer(>=0 <=1)}"}},
        {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, manapi::json{"{integer(>=0 <=1)}"}},
        {HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL, manapi::json{"{integer(>=0 <=1)}"}}
};

void stringify_stream_id(int stream_id, char *buffer) {
    int index = 0;
    buffer[index++] = static_cast<char> ((stream_id >> (8 * 3)) & 0x7F); // 127
    for (int i = 2; i >= 0; i--) {
        buffer[index++] = static_cast<char> ((stream_id >> i * 8) & 0xFF); // 256
    }
}

template <typename T>
void stringify_number (T n, char *buffer, int size = sizeof (T)) {
    int index = 0;
    for (int i = sizeof (n) - 1 - (sizeof (T) - size); i >= 0; --i) {
        buffer[index++] = static_cast<char> ((n >> i * 8) & 0xFF);
    }
}

/**
 * send a HTTP/2 frame
 *
 * @param ctx HTTP/2 Context
 * @param frame_type Frame Type
 * @param flags Frame Flags
 * @param stream_id Stream ID
 * @return 0 on success, -1 on write error
 */
int http_v2_send_frame (manapi::net::http::http_v2_t *ctx,  int frame_type, uint8_t flags, uint32_t stream_id, manapi::ev::buff_t buffs[], uint32_t nbuff, ssize_t size, bool force = true) MANAPIHTTP_NOEXPECT {
    char header[9];

    stringify_stream_id(stream_id, header + 5);
    stringify_number <int> (size, header, 3);
    header[3] = static_cast<char> (frame_type);
    header[4] = static_cast<char> (flags);

    auto rhs = ctx->worker->sync_write_ex(ctx->conn, header, sizeof (header), force && !size, maxcnt);
    if (rhs != sizeof (header))
        return manapi::ERR_INTERNAL;

    if (nbuff) {
        rhs = ctx->worker->sync_write_ex(ctx->conn, buffs, nbuff, size, force, maxcnt);
        if (rhs != size)
            return manapi::ERR_INTERNAL;
    }

    if (!(ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_BLOCK_WRITE)
        && !ctx->worker->is_writable(ctx->conn)) {
        ctx->flags |= manapi::net::http::HTTP2_CTX_FLAG_BLOCK_WRITE;
        ctx->worker->event_toggle(ctx->conn,
            true, manapi::ev::WRITE);
        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: wait write event");
    }

    return manapi::ERR_OK;
}

int http_v2_send_window_frame (manapi::net::http::http_v2_t *ctx,  int stream_id, int size) MANAPIHTTP_NOEXPECT {
    /**
     * RFC9113 (6.9) WINDOW_UPDATE
     *
     * WINDOW_UPDATE Frame {
     *  Length (24) = 0x04,
     *  Type (8) = 0x08,
     *
     *  Unused Flags (8),
     *
     *  Reserved (1),
     *  Stream Identifier (31),
     *
     *  Reserved (1),
     *  Window Size Increment (31),
     * }
     *
     */

    if (!size || (size & 0x80000000)) {
        /**
         * RFC9113 (6.9) WINDOW_UPDATE
         *
         * The frame payload of a WINDOW_UPDATE frame is one reserved
         * bit plus an unsigned 31-bit integer indicating the number
         * of octets that the sender can transmit in addition to the
         * existing flow-control window.
         * The legal range for the increment to the flow-control
         * window is 1 to 2^31-1 (2,147,483,647) octets.
         */

        return manapi::ERR_INTERNAL;
    }

    char out[4];
    stringify_number<int>(size, out, 4);
    manapi::ev::buff_t buff;
    buff.base = out;
    buff.len = static_cast<size_t>(4);

    return http_v2_send_frame(ctx,  HTTP2_FRAME_WINDOW_UPDATE, 0, stream_id, &buff, 1, buff.len);
}

int http_v2_send_ping_frame (manapi::net::http::http_v2_t *ctx, char *data) MANAPIHTTP_NOEXPECT {
    if (data) {
        manapi::ev::buff_t buf;
        buf.base = data;
        buf.len = 8;

        if (http_v2_send_frame(ctx, HTTP2_FRAME_PING, HTTP2_FLAG_PING_ACK, 0,&buf, 1, buf.len)) {
            return manapi::ERR_INTERNAL;
        }
    }
    else {
        try {
            auto res = manapi::crypto::random_string(8);
            if (!ctx->pings) {
                ctx->pings = std::make_unique<decltype(ctx->pings)::element_type>();
            }
            if (!res.ok())
                return manapi::ERR_INTERNAL;

            auto s = res.unwrap();
            manapi::ev::buff_t buf;
            buf.base = s.data();
            buf.len = static_cast<size_t>(8);
            if (http_v2_send_frame(ctx, HTTP2_FRAME_PING, 0, 0, &buf, 1, buf.len)) {
                return manapi::ERR_INTERNAL;
            }
            ctx->pings->insert(std::move(s));
        }
        catch (std::bad_alloc const &) {
            return manapi::ERR_RESOURCE_EXHAUSTED;
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "http_v2_send_ping_frame", e.what());
            return manapi::ERR_INTERNAL;
        }
    }


    return manapi::ERR_OK;
}

int http_v2_send_data_frame (manapi::net::http::http_v2_t *ctx,  int stream_id, manapi::ev::buff_t buffs[], uint32_t nbuff, ssize_t size, bool finish) MANAPIHTTP_NOEXPECT {
    return http_v2_send_frame(ctx, HTTP2_FRAME_DATA, finish ? HTTP2_FLAG_DATA_END_STREAM : 0, stream_id, buffs, nbuff, size, finish);
}

int http_v2_send_data_frame (manapi::net::http::http_v2_t *ctx, int stream_id, const char *data, ssize_t size, bool finish) MANAPIHTTP_NOEXPECT {
    manapi::ev::buff_t buff;
    buff.base = (char*)(data);
    buff.len = static_cast<size_t>(size);
    return http_v2_send_frame(ctx, HTTP2_FRAME_DATA, finish ? HTTP2_FLAG_DATA_END_STREAM : 0, stream_id, &buff, 1, size, finish);
}

int http_v2_verify_setting (int key, int value) MANAPIHTTP_NOEXPECT {
    auto const it = allow_settings.find(key);
    if (it != allow_settings.end()) {
        try {
            auto res = it->second.valid(value);
            if (res.ok()) {
                return 0;
            }
        }
        catch (std::exception const &e) {
            /* ignore */
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, e.what());
        }
        /* setting incorrect */
        return manapi::ERR_INTERNAL;
    }
    return manapi::ERR_OK;
}


int http_v2_insert_priority (manapi::net::http::http_v2_t *ctx, const manapi::net::worker::shared_conn &sconn, manapi::net::http::http_v2_stream_t *sdata, uint8_t upriority) MANAPIHTTP_NOEXPECT {
    using namespace manapi::net::http;

    bool flg = false;
    if (upriority == 78) {
        sdata->flags |= manapi::net::http::HTTP2_STREAM_WINDOW_EMPTY;
    }
    try {
        //std::cout << "insert " << sdata->id << " " << (int)upriority << "\n";
        auto const prit = ctx->priorities->insert(
            {{upriority, sdata->id}, sconn});

        if (prit.second) {
            if (prit.first == ctx->priorities->begin()) {
                if (sdata->flags & HTTP2_STREAM_PRIORITY_LOCKED) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: unlock %d stream", sdata->id);
                    sdata->flags ^= HTTP2_STREAM_PRIORITY_LOCKED;
                }

                flg = true;
            }
            else {
                auto const prev_prit = std::prev(prit.first);
                if (prev_prit->first == prit.first->first) {
                    /* the priorities are same */
                    auto prev_sdata = (*prev_prit->second).as<http_v2_stream_t>();
                    if (prev_sdata->flags & (HTTP2_STREAM_PRIORITY_LOCKED|HTTP2_STREAM_PRIORITY_INCR)) {
                        /* it also must be locked or the previous stream has priority incr. flag */
                        sdata->flags |= HTTP2_STREAM_PRIORITY_LOCKED;
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: lock %d stream", sdata->id);
                    }
                    else if (!(sdata->flags & HTTP2_STREAM_PRIORITY_INCR)) {
                        if (sdata->flags & HTTP2_STREAM_PRIORITY_LOCKED) {
                            sdata->flags ^= HTTP2_STREAM_PRIORITY_LOCKED;
                            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: unlock %d stream", sdata->id);
                        }

                        flg = true;
                    }
                }
                else {
                    /* it has a lower priority */
                    sdata->flags |= HTTP2_STREAM_PRIORITY_LOCKED;
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: lock %d stream", sdata->id);
                }
            }
            if (flg) {
                /* it's so important to us */
                for (auto it = std::next(prit.first); it != ctx->priorities->end(); ++it) {
                    auto const s_oth_data = (*it->second).as<http_v2_stream_t>();
                    if (s_oth_data->flags & HTTP2_STREAM_PRIORITY_LOCKED) {
                        /* no work left */
                        break;
                    }
                    if (it->first == prit.first->first) {
                        if (sdata->flags & HTTP2_STREAM_PRIORITY_INCR) {
                            s_oth_data->flags |= HTTP2_STREAM_PRIORITY_LOCKED;
                            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: lock %d stream", s_oth_data->id);
                        }
                    }
                    else {
                        s_oth_data->flags |= HTTP2_STREAM_PRIORITY_LOCKED;
                        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: lock %d stream", s_oth_data->id);
                    }
                }
            }
        }

    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http_v2_insert_priority", e.what());
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

uint8_t http_v2_remove_priority (manapi::net::http::http_v2_t *ctx, manapi::net::http::http_v2_stream_t *s, uint8_t upriority) MANAPIHTTP_NOEXPECT {
    using namespace manapi::net::http;

    //std::cout << "remove " << s->id << " " << (int)upriority << "\n";

    if (upriority == 78) {
        if (s->flags & manapi::net::http::HTTP2_STREAM_WINDOW_EMPTY)
            s->flags ^= manapi::net::http::HTTP2_STREAM_WINDOW_EMPTY;
    }

    try {
        auto opit = ctx->priorities->find({upriority, s->id});
        assert(opit!=ctx->priorities->end());
        if (opit != ctx->priorities->end()) {
            if (opit == ctx->priorities->begin()) {
                {
                    int cnt = 0;
                    auto pit = std::next(opit);
                    if (pit != ctx->priorities->end()) {
                        auto const priority = pit->first;
                        for (; pit != ctx->priorities->end(); ++pit) {
                            if (pit->first != priority)
                                break;

                            auto sdata = (*pit->second).as<http_v2_stream_t>();
                            if ((sdata->flags & HTTP2_STREAM_PRIORITY_LOCKED)) {
                                if (cnt && (sdata->flags & HTTP2_STREAM_PRIORITY_INCR))
                                    break;

                                cnt += 1;
                                sdata->flags ^= HTTP2_STREAM_PRIORITY_LOCKED;
                                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: unlock %d stream", sdata->id);

                                sdata->ctx->http_v2_worker->feed_event(pit->second,
                                    manapi::ev::WRITE, nullptr, 0, nullptr);


                                if (sdata->flags & HTTP2_STREAM_PRIORITY_INCR)
                                    break;
                            }
                        }
                    }
                }
            }

            ctx->priorities->erase(opit);
        }
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("http2: priority update failed due to {}", e.what());
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

int http_v2_real_priority_by_stream (manapi::net::http::http_v2_stream_t *s) MANAPIHTTP_NOEXPECT {
    if ((s->write_window <= 0))
        return 78;

    return s->priority;
}

int http_v2_update_priority (manapi::net::http::http_v2_t *ctx, const manapi::net::worker::shared_conn &sconn, manapi::net::http::http_v2_stream_t *s) MANAPIHTTP_NOEXPECT {
    uint8_t rs;
    uint8_t as;
    if (http_v2_real_priority_by_stream (s) == s->priority) {
        rs = 78;
        as = s->priority;
    }
    else {
        rs = s->priority;
        as = 78;
    }

    if (http_v2_remove_priority(ctx, s, rs))
        return manapi::ERR_INTERNAL;

    return http_v2_insert_priority(ctx, sconn, s, as);
}

int http_v2_apply_setting (manapi::net::http::http_v2_t *ctx, int key, int value, bool server) MANAPIHTTP_NOEXPECT {
    auto const settings = server ? ctx->server.get() : ctx->client.get();

    if (http_v2_verify_setting(key, value))
        return manapi::ERR_INTERNAL;

    switch (key) {
        case HTTP2_SETTING_HEADER_TABLE_SIZE: {
            try {
                settings->header_table_size = value;
                if (server) {
                    ctx->encoder->max_table_size(value);
                }
                else {
                    ctx->decoder->m_dynamic_max(value);
                }
            }
            catch (std::bad_alloc const &) {
                return manapi::ERR_RESOURCE_EXHAUSTED;
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                return manapi::ERR_INTERNAL;
            }
            break;
        }
        case HTTP2_SETTING_ENABLE_PUSH:
            settings->enable_push = value;
        break;
        case HTTP2_SETTING_MAX_FRAME_SIZE:
            settings->max_frame_size = value;
        break;
        case HTTP2_SETTING_INITIAL_WINDOW_SIZE: {
            ssize_t const t = (value - settings->initial_window_size);
            for (const auto &conn : *ctx->streams) {
                auto const s = conn.second->as<manapi::net::http::http_v2_stream_t>();
                if (server)
                    s->read_window += t;
                else
                    s->write_window += t;
            }
            settings->initial_window_size = value;
            break;
        }
        case HTTP2_SETTING_TLS_RENEG_PERMITTED:
            settings->tls_reneg_permitted = value;
        break;
        case HTTP2_SETTING_MAX_CONCURRENT_STREAMS:
            settings->max_concurret_streams = value;
        break;
        case HTTP2_SETTING_SETTINGS_ENABLE_METADATA:
            settings->settings_enable_metadata = value;
        break;
        case HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES:
            settings->settings_no_rfc7540_priorities = value;
        break;
        case HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL:
            settings->settings_enable_connect_protocol = value;
        break;
        default:
            /**
             * RFC9113 (6.5.3) Settings Synchronization
             *
             * Unsupported settings MUST be ignored.
             */
            break;
    }

    return 0;
}

int http_v2_send_goaway (manapi::net::http::http_v2_t *ctx, http_v2_goaway_t *http_goaway) MANAPIHTTP_NOEXPECT {
    manapi::ev::buff_t data[2];
    char nums[8];

    data[0].base = nums;
    data[0].len = sizeof (nums);

    data[1].base = (char *)http_goaway->err_msg.data();
    data[1].len = http_goaway->err_msg.size();

    stringify_number<int> (ctx->last_stream_id, data[0].base);
    stringify_number<int>(http_goaway->err_code, data[0].base + 4);

    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "http2: GOAWAY SEND. err code: %d, stream id: %zu, msg: %.*s",
        http_goaway->err_code, ctx->last_stream_id, http_goaway->err_msg.size(), http_goaway->err_msg.data());

    if (auto rhs = http_v2_send_frame(ctx, HTTP2_FRAME_GOAWAY, 0, 0, data, 2, data[0].len + data[1].len)) {
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

int http_v2_send_settings (manapi::net::http::http_v2_t *ctx, const std::vector<std::pair<short, int>> & options) MANAPIHTTP_NOEXPECT {
    if (ctx->timeout) {
        return manapi::ERR_INTERNAL;
    }

    try {
        size_t const len = options.size() * 6;
        std::string buffer;
        buffer.reserve(len);
        for (int i = 0; i < options.size(); ++i) {
            if (http_v2_apply_setting(ctx, options[i].first, options[i].second, true)) {
                return manapi::ERR_INTERNAL;
            }

            stringify_number<short>(options[i].first, buffer.data() + i * 6);
            stringify_number<int>(options[i].second, buffer.data() + i * 6 + 2);
        }
        manapi::ev::buff_t bufs;
        bufs.base = buffer.data();
        bufs.len = len;
        if (http_v2_send_frame(ctx, HTTP2_FRAME_SETTINGS, 0, 0, &bufs, 1, bufs.len)) {
            return manapi::ERR_INTERNAL;
        }

        /* check for priority */
        for (const auto &conn : *ctx->streams) {
            auto const s = conn.second->as<manapi::net::http::http_v2_stream_t>();
            if (s->flags & manapi::net::http::HTTP2_STREAM_WINDOW_EMPTY && (s->write_window > 0)) {
                if (http_v2_update_priority(ctx, conn.second, s))
                    return manapi::ERR_UNKNOWN;
            }
        }


        ctx->timeout = manapi::async::current()->timerpool()->append_timer_sync(3000,
            [ctx, conn = ctx->conn] (manapi::timer t) -> void {
                http_v2_goaway_t http_goaway = {
                    .err_code = manapi::net::worker::HTTP2_ERROR_SETTINGS_TIMEOUT,
                    .err_msg = "SETTINGS timeout"
                };

                auto const rhs = http_v2_send_goaway (ctx, &http_goaway);
                ctx->worker->feed_event(conn, manapi::ev::DISCONNECT, nullptr, 0, nullptr);
        }).unwrap();
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return manapi::ERR_INTERNAL;
    }


    return manapi::ERR_OK;
}

int http_v2_rst_stream_ex (manapi::net::http::http_v2_t *ctx, int stream_id, int errcode) MANAPIHTTP_NOEXPECT {
    if (ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_REALY_CLOSE)
        return 0;

    char errid[4];
    stringify_number<int>(errcode, errid);
    manapi::ev::buff_t buff;
    buff.base = errid;
    buff.len = 4;
    return http_v2_send_frame(ctx, HTTP2_FRAME_RST_STREAM, 0, stream_id, &buff, 1, buff.len);
}

int manapi::net::http::http_v2_on_close (http_v2_t *ctx) MANAPIHTTP_NOEXPECT {
    ctx->flags |= HTTP2_CTX_FLAG_WANT_CLOSE;

    if (ctx->timeout) {
        ctx->timeout.stop();
        ctx->timeout = nullptr;
    }

    if (ctx->streams) {
        for (auto it = ctx->streams->begin(); it != ctx->streams->end(); ) {
            auto data = it->second->as<http_v2_stream_t>();
            if (data->req)
                it = ctx->streams->erase(it);
            else {
                ctx->http_v2_worker->close_connection(it->second, worker::CLOSE_CONN_ERR);
                ++it;
            }
        }
    }

    return 0;
}

int manapi::net::http::http_v2_on_close_stream(http_v2_t *ctx, uint32_t id) MANAPIHTTP_NOEXPECT {
    try {
        auto it = ctx->streams->find(id);
        if (it == ctx->streams->end()) {
            return manapi::ERR_INTERNAL;
        }
        auto s = it->second->as<http_v2_stream_t>();

        if (!(s->flags & HTTP2_STREAM_SEND_END)) {
            ctx->concurrent_streams_size--;
            s->flags |= HTTP2_STREAM_SEND_END;
        }

        manapi_log_trace(debug::LOG_TRACE_LOW, "http2: erase %d stream", s->id);

        if (http_v2_remove_priority (ctx, s, http_v2_real_priority_by_stream(s))) {
            /* ignore */
        }

        ctx->streams->erase(it);
    }
    catch (std::exception const &e) {
        manapi_log_error("http2: stream erase failed due to %s", e.what());
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

bool http_v2_stream_on_write (const manapi::net::worker::shared_conn &conn, manapi::net::http::http_v2_stream_t *data) MANAPIHTTP_NOEXPECT {
    if ((data->flags & manapi::ev::WRITE)) {
        if (data->ev_callback) {
            try {
                data->ev_callback->operator()(conn, manapi::ev::WRITE, nullptr, 0, nullptr);
            }
            catch (std::exception const &e) {
                manapi_log_error("%s: %s failed due to %s", "http2", "event callback", e.what());
            }
        }

        return true;
    }
    return false;
}

int manapi::net::http::http_v2_on_write(http_v2_t *ctx) MANAPIHTTP_NOEXPECT {
    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "http2: write event received");

    if (ctx->current) {
        if (ctx->flags & HTTP2_CTX_FLAG_BLOCK_WRITE)
            ctx->flags ^= HTTP2_CTX_FLAG_BLOCK_WRITE;

        bool no_one = true;
        for (const auto &priority : *ctx->priorities) {
            //auto &conn = (*priority.second);
            auto const data = priority.second->as<http_v2_stream_t>();
            if (ctx->flags & HTTP2_CTX_FLAG_BLOCK_WRITE)
                break;

            if (http_v2_stream_on_write(priority.second, data))
                no_one = false;
        }

        if (no_one)
            ctx->worker->event_toggle(ctx->conn, false, ev::WRITE);
    }

    return 0;
}

int http_v2_process_window (const manapi::net::worker::shared_conn &conn, manapi::net::http::http_v2_stream_t *s) MANAPIHTTP_NOEXPECT {
    auto ssw = (s->top->recv_size + 1) * s->ctx->worker->config()->buffer_size;
    auto config = s->ctx->worker->config();

    ssw = std::max<ssize_t>(static_cast<ssize_t>(0),
        std::min<ssize_t>(config->window_stream_size - ssw, s->write_window));

    if (s->read_window < ssw) {
        /**
         * a server window size can't be more than a client window size
         * because the trasfering can be self-blocked
         *
         * For example - Echo Server
         * It accepts 16 * 4096 bytes using 200k window size, and stops
         * reading because server can't send it because the client window size is 0
         * and we can't accept window frames to update the value
         */

        const auto allow = static_cast<int>(ssw - s->read_window);
        if (!(s->flags & (manapi::net::http::HTTP2_STREAM_CLOSED|manapi::net::http::HTTP2_STREAM_RECV_END))) {
            if (http_v2_send_window_frame(s->ctx, s->id, allow))
                return manapi::ERR_INTERNAL;

            s->read_window += allow;
        }
    }

    auto const window_half = config->window_connection_size / 2;
    if (s->ctx->read_window <= window_half) {
        const auto allow = config->window_connection_size - s->ctx->read_window;
        if (http_v2_send_window_frame(s->ctx, 0,
            allow)) {
            return manapi::ERR_INTERNAL;
        }
        s->ctx->read_window += allow;
    }
    return manapi::ERR_OK;
}

int manapi::net::http::http_v2_on_read_stream(const worker::shared_conn &conn) MANAPIHTTP_NOEXPECT {
    auto const s = conn->as<http_v2_stream_t>();

    auto const config = s->ctx->worker->config();
    auto const bs = config->max_buffer_stack;
    bool const read_blocked = s->top->recv_size >= bs;

    if (auto const rhs = worker::http_v2_flush_recv (config, conn, s))
        return rhs;

    if (read_blocked && s->top->recv_size < read_blocked)
        s->ctx->worker->event_toggle(s->ctx->conn, true, ev::READ);

    return http_v2_process_window (conn, s);
}

void http_v2_setup_goaway (manapi::net::http::http_v2_t *ctx, http_v2_goaway_t &http_goaway, int errnum, const char *msg) MANAPIHTTP_NOEXPECT {
    http_goaway.err_code = errnum;
    http_goaway.err_msg = msg;
    ctx->flags |= manapi::net::http::HTTP2_CTX_FLAG_REALY_CLOSE;
    ctx->current = HTTP2_CALLBACK_GOAWAY;
}

void connection_interface_eraser (manapi::net::worker::connection *ptr) MANAPIHTTP_NOEXPECT {
    auto uptr = std::unique_ptr<manapi::net::worker::connection> (ptr);
    delete uptr->as<manapi::net::http::http_v2_stream_t>();
}

int manapi::net::http::http_v2_work(http_v2_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize) MANAPIHTTP_NOEXPECT {
    http_v2_goaway_t http_goaway;

    ssize_t pos = 0;

    auto &buffer = *nbuffer;
    auto &size = *nsize;

    try {
        while (pos != size) {
            repeat: switch(ctx->current) {
                case HTTP2_CALLBACK_INIT: {
                    try {
                        ctx->current = HTTP2_CALLBACK_SKIP_MSG;
                        ctx->next = HTTP2_CALLBACK_ERROR;

                        ctx->frame_buffer.reserve(64);
                        ctx->last_stream_id = 0;
                        ctx->n1 = 0;
                        ctx->n2 = 0;
                        ctx->pos1 = 0;

                        ctx->read_window = 65535;
                        ctx->write_window = 65535;

                        ctx->server = std::make_unique<http_v2_settings_t>();
                        ctx->client = std::make_unique<http_v2_settings_t>();

                        ctx->decoder = std::make_unique<decltype(ctx->decoder)::element_type>(4096,
                            config->max_headers_size, config->max_header_key_size, config->max_header_value_size);
                        ctx->encoder = std::make_unique<decltype(ctx->encoder)::element_type>();

                        ctx->priorities = std::make_unique<decltype(ctx->priorities)::element_type>();

                        ctx->server->header_table_size = 4096;
                        ctx->server->enable_push = 1;
                        ctx->server->max_concurret_streams = std::numeric_limits<int>::max();
                        ctx->server->initial_window_size = 65535;
                        ctx->server->max_frame_size = 16384;
                        ctx->server->max_header_list_size = std::numeric_limits<int>::max();
                        ctx->server->settings_no_rfc7540_priorities = 0;
                        ctx->server->settings_enable_metadata = 0;
                        ctx->server->settings_enable_connect_protocol = 0;
                        ctx->server->tls_reneg_permitted = 0;

                        *ctx->client = *ctx->server;

                        // ctx->encoder->max_table_size(ctx->server->header_table_size);
                        // ctx->decoder->m_dynamic_max(ctx->client->header_table_size);

                        ctx->streams = std::make_unique<decltype(ctx->streams)::element_type>();

                        std::vector<std::pair<short, int>> ops{
                            {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
                            {HTTP2_SETTING_ENABLE_PUSH, 0},
                        };
                        int v = config->max_concurrent_streams < 0 ? 100 : config->max_concurrent_streams;
                        ops.emplace_back(HTTP2_SETTING_MAX_CONCURRENT_STREAMS, v);
                        v = config->max_frame_size;
                        if (v > 0)
                            ops.emplace_back(HTTP2_SETTING_MAX_FRAME_SIZE, v);
                        v = config->max_hpack_list_size < 0 ? 4096 : config->max_hpack_list_size;
                        ops.emplace_back(HTTP2_SETTING_MAX_HEADER_LIST_SIZE, v);
                        v = config->initial_window_size < 0 ? 65535 : config->initial_window_size;
                        ops.emplace_back(HTTP2_SETTING_INITIAL_WINDOW_SIZE, v);

                        if (http_v2_send_settings(ctx, ops)) {
                            return EHTTP_V2_PROTOCOL_ERROR;
                        }
                    }
                    catch (std::bad_alloc const &) {
                        return EHTTP_V2_PROTOCOL_ERROR;
                    }
                    catch (std::exception const &e) {
                        manapi_log_error("%s: %s failed due to %s", "http2", "init", e.what());
                        return EHTTP_V2_PROTOCOL_ERROR;
                    }

                    break;
                }
                case HTTP2_CALLBACK_NEXT_LINE: {
                    if (buffer[pos] == '\r') {
                        ctx->current = HTTP2_CALLBACK_NEXT_LINE2;
                        pos++;
                        break;
                    }
                    return EHTTP_V2_PROTOCOL_ERROR;
                }
                case HTTP2_CALLBACK_NEXT_LINE2: {
                    if (buffer[pos] == '\n') {
                        ctx->current = ctx->next;
                        ctx->next = HTTP2_CALLBACK_ERROR;
                        pos++;
                        break;
                    }
                    return EHTTP_V2_PROTOCOL_ERROR;
                }
                case HTTP2_CALLBACK_SKIP_MSG: {
                    while (pos != size) {
                        if (buffer[pos] != smlabel[ctx->pos1]) {
                            ctx->pos1 = 0;
                            http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR;
                            http_goaway.err_msg = "invalid sm label";
                            ctx->current = HTTP2_CALLBACK_GOAWAY;
                            ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;
                            break;
                        }

                        pos ++;
                        if (++ctx->pos1 == sizeof (smlabel) - 1) {
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                            ctx->current = HTTP2_CALLBACK_NEXT_LINE;
                            ctx->pos1 = 0;

                            break;
                        }

                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_LENGTH: {
                    while (pos != size) {
                        ctx->frame_length = static_cast<unsigned int>((static_cast<unsigned int>(ctx->frame_length << 8)
                            | static_cast<unsigned char>(buffer[pos++])));

                        /* 3 chars only */
                        if (++ctx->pos1 == 3) {
                            ctx->current = HTTP2_CALLBACK_PARSE_HEADER_TYPE;
                            ctx->pos1 = 0;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_NEW_FRAME: {
                    ctx->frame_length = 0;
                    ctx->frame_stream_id = 0;
                    ctx->frame_buffer.clear();

                    ctx->current = HTTP2_CALLBACK_PARSE_HEADER_LENGTH;

                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_TYPE: {
                    ctx->frame_type = static_cast<int>(static_cast<unsigned char> (buffer[pos++]));
                    ctx->current = HTTP2_CALLBACK_PARSE_HEADER_FLAG;
                    break;
                }
                case HTTP2_CALLBACK_SKIP_NULL_OCTECTS: {
                    while (pos != size) {
                        ctx->current = ctx->next;
                        ctx->next = HTTP2_CALLBACK_ERROR;

                        if (buffer[pos] == '\0') {
                            pos++;
                            break;
                        }

                        perror(("bug"));

                        break;
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID: {
                    while (pos != size) {
                        ctx->frame_stream_id = static_cast< int>((static_cast<unsigned int>(ctx->frame_stream_id << 8)
                            | static_cast<unsigned char>(buffer[pos++])));

                        if (++ctx->pos1 == 4) {
                            // STREAM ID

                            // Reserved (1),
                            // Stream Identifier (31)

                            ctx->frame_stream_id = ctx->frame_stream_id & 0x7FFFFFFF; // 31
                            ctx->pos1 = 0;

                            ctx->current = HTTP2_CALLBACK_PARSE_FIELD_BLOCK;

                            goto repeat;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_FLAG: {
                    ctx->frame_flag = static_cast<int>(static_cast<unsigned char>(buffer[pos++]));
                    ctx->current = HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID;
                    break;
                }
                case HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID: {
                    while (pos != size) {
                        ctx->n1 = static_cast<unsigned int>((static_cast<unsigned int>(ctx->n1 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 4) {
                            ctx->n2 = static_cast<int> (ctx->n1 & 0x7FFFFFFF);
                            ctx->n1 = 0;
                            ctx->pos1 = 0;
                            ctx->frame_length -= 4;
                            ctx->current = HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE: {
                    while (pos != size) {
                        ctx->n1 = static_cast<unsigned int>((static_cast<unsigned int>(ctx->n1 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 4) {
                            ctx->pos1 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA;
                            ctx->frame_length -= 4;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA: {
                    auto copy = std::min<ssize_t>(static_cast<ssize_t>(ctx->frame_length), size - pos);
                    copy = std::min<ssize_t>(ctx->frame_buffer.size() + copy, 64 - ctx->frame_buffer.size());

                    ctx->frame_buffer.append(buffer + pos, copy);

                    pos += copy;
                    ctx->frame_length -= copy;

                    if (!ctx->frame_length || ctx->frame_buffer.size() == 64) {
                        /*
                         * connection was closed
                         * n1 - error code
                         * n2 - last stream id
                         * frame_buffer - additional debug data
                         */


                        if (ctx->n1 >= worker::HTTP2_ERROR_PROTOCOL_ERROR &&
                            ctx->n1 <= worker::HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                            /**
                             * DEEPSEEK SAID
                             *
                             * "The last-stream-id in the GOAWAY frame contains the highest-numbered stream
                             * identifier for which the sender of the GOAWAY frame might
                             * have taken some action on or might yet take action on."
                             * "Endpoints MUST NOT open additional streams on the connection,
                             * but a new connection can be established for new streams.
                             * If additional data is received for streams whose
                             * identifiers are higher than the indicated
                             * last-stream-id, the receiver of the GOAWAY
                             * frame MUST treat those streams as though they
                             * had never been created at all..."
                             *
                             */
                            for (auto it = ctx->streams->upper_bound(ctx->n2); it != ctx->streams->end(); ++it) {
                                ctx->http_v2_worker->close_connection(it->second, worker::CLOSE_CONN_ERR);
                            }

                            manapi_log_trace("HTTP2: GOAWAY RECV. err code: %d, stream id: %zu, msg: %.*s",
                                ctx->n1, ctx->n2, ctx->frame_buffer.size(), ctx->frame_buffer.data());

                            ctx->n1 = 0;
                            ctx->n2 = 0;
                            ctx->frame_buffer.clear();

                            ctx->flags |= HTTP2_CTX_FLAG_WANT_CLOSE;

                            if (ctx->streams->empty())
                                return EHTTP_V2_PROTOCOL_OK;
                        }
                        if (ctx->frame_length) {
                            ctx->n1 = ctx->frame_length;
                            ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                        else {
                            ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE: {
                    while (pos != size) {
                        ctx->n1 = static_cast<unsigned int>((static_cast<unsigned int>(ctx->n1 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 4) {
                            ctx->pos1 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                            ctx->frame_length -= 4;

                            /**
                             * n1 - Length
                             */


                            if (!ctx->n1) {
                                /**
                                 * RFC9113 (6.9) WINDOW_UPDATE
                                 *
                                 * A receiver MUST treat the receipt of a WINDOW_UPDATE frame
                                 * with a flow-control window increment of 0 as a stream
                                 * error (Section 5.4.2) of type PROTOCOL_ERROR;
                                 * errors on the connection flow-control window MUST be treated
                                 * as a connection error (Section 5.4.1).
                                 */

                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "invalid WINDOW_UPDATE");
                                goto repeat;
                            }

                            if (ctx->frame_stream_id) {
                                auto const s = ctx->streams->find(ctx->frame_stream_id);

                                if (s == ctx->streams->end()) {
                                    if (ctx->frame_stream_id > ctx->last_stream_id) {
                                        http_v2_setup_goaway(ctx, http_goaway,
                                            worker::HTTP2_ERROR_PROTOCOL_ERROR, "stream wasn't created");
                                        goto repeat;
                                    }
                                    break;
                                }

                                auto const sdata = s->second->as<http_v2_stream_t>();

                                if (std::numeric_limits<int32_t>::max() - sdata->write_window < ctx->n1) {
                                    http_v2_rst_stream_ex(ctx, sdata->id, worker::HTTP2_ERROR_FLOW_CONTROL_ERROR);
                                    ctx->http_v2_worker->close_connection(s->second, worker::CLOSE_CONN_ERR);
                                }
                                else {
                                    manapi_log_trace(debug::LOG_TRACE_LOW, "http2: window frame received: id=%u v=%d prev=%u",
                                        sdata->id, sdata->write_window, ctx->n1);
                                    sdata->write_window += ctx->n1;

                                    if (sdata->write_window > 0) {
                                        if (sdata->flags & HTTP2_STREAM_WINDOW_EMPTY)
                                            http_v2_update_priority(ctx, s->second, sdata);
                                        if ((sdata->flags & (ev::WRITE|ev::DISCONNECT)) == ev::WRITE
                                        && (sdata->ev_callback)) {
                                            sdata->ev_callback->operator()(s->second, ev::WRITE, nullptr, 0, nullptr);
                                        }
                                    }
                                }
                            }
                            else {
                                if (std::numeric_limits<int32_t>::max() - ctx->write_window < ctx->n1) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                                        "overflow");
                                    goto repeat;
                                }
                                manapi_log_trace(debug::LOG_TRACE_LOW, "http2: window frame received: id=%u v=%d prev=%u",
                                    0, ctx->write_window, ctx->n1);

                                ctx->write_window += static_cast<int>(ctx->n1);
                                if (ctx->write_window <= ctx->n1) {
                                    for (const auto &s : *ctx->streams) {
                                        auto const sdata = s.second->as<http_v2_stream_t>();

                                        if (ctx->flags & HTTP2_CTX_FLAG_BLOCK_WRITE)
                                            break;

                                        http_v2_stream_on_write(s.second, sdata);
                                    }
                                }
                            }

                            ctx->n1 = 0;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION: {
                    while (pos != size) {
                        ctx->n1 = static_cast<unsigned int>((static_cast<unsigned int>(ctx->n1 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 4) {
                            ctx->pos1 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                            ctx->frame_length -= 4;

                            /**
                             * n1 - error code
                             */

                            if (ctx->n1 >= manapi::net::worker::HTTP2_ERROR_NO_ERROR
                                && ctx->n1 <= manapi::net::worker::HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                                auto s = ctx->streams->find(ctx->frame_stream_id);
                                if (s == ctx->streams->end()) {
                                    if (ctx->frame_stream_id > ctx->last_stream_id) {
                                        http_v2_setup_goaway(ctx, http_goaway,
                                            worker::HTTP2_ERROR_PROTOCOL_ERROR, "stream wasn't created");
                                        goto repeat;
                                    }
                                }
                                else {
                                    auto const data = s->second->as<http_v2_stream_t>();
                                    if (!(data->flags & HTTP2_STREAM_SEND_END))
                                        ctx->concurrent_streams_size--;

                                    data->flags |= HTTP2_STREAM_RECV_END|HTTP2_STREAM_SEND_END;
                                    ctx->http_v2_worker->close_connection(s->second, worker::CLOSE_CONN_SHUTDOWN);
                                }
                            }

                            ctx->n1 = 0;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_PING_DATA : {
                    /* length is 0 or 8 */

                    auto const copy = std::min(static_cast<ssize_t> (ctx->frame_length), size - pos);
                    ctx->frame_buffer.append(buffer + pos, copy);
                    pos += copy;
                    ctx->frame_length -= copy;

                    if (!ctx->frame_length) {
                        if (ctx->frame_flag & HTTP2_FLAG_PING_ACK) {
                            bool flg = true;

                            if (ctx->pings) {
                                auto const it = ctx->pings->find(ctx->frame_buffer);
                                if (it != ctx->pings->end()) {
                                    ctx->pings->erase(it);
                                    flg = false;
                                }
                            }

                            if (flg) {

                            }
                        }
                        else {
                            http_v2_send_ping_frame(ctx, ctx->frame_buffer.data());
                        }

                        ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        ctx->frame_buffer.clear();
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_SETTING_ID: {
                    while (pos != size) {
                        ctx->n1 = static_cast<unsigned int>((static_cast<unsigned int>(ctx->n1 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 2) {
                            ctx->pos1 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_SETTING_VALUE;
                            ctx->frame_length -= 2;

                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_SETTING_VALUE: {
                    while (pos != size) {
                        ctx->n2 = static_cast<int>((static_cast<unsigned int>(ctx->n2 << 8) | static_cast<unsigned char>(buffer[pos++])));
                        if (++ctx->pos1 == 4) {
                            ctx->pos1 = 0;
                            ctx->frame_length -= 4;

                            /**
                             * n1 - setting id
                             * n2 - setting value
                             */

                            if (http_v2_apply_setting(ctx, static_cast<short> (ctx->n1), ctx->n2, false)) {
                                ctx->n1 = 0;
                                ctx->n2 = 0;
                                http_v2_setup_goaway(ctx,http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "setting is invalid");
                                goto repeat;
                            }

                            if (ctx->frame_length) {
                                ctx->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;

                                /* send ack frame */
                                if (http_v2_send_frame(ctx, HTTP2_FRAME_SETTINGS, HTTP2_FLAG_SETTINGS_ACK, 0, nullptr, 0, 0)) {
                                    return EHTTP_V2_PROTOCOL_ERROR;
                                }
                                /* check for priority */
                                for (const auto &conn : *ctx->streams) {
                                    auto const s = conn.second->as<http_v2_stream_t>();
                                    if (s->flags & HTTP2_STREAM_WINDOW_EMPTY && (s->write_window > 0)) {
                                        if (http_v2_update_priority(ctx, conn.second, s))
                                            return EHTTP_V2_PROTOCOL_ERROR;
                                    }
                                }

                            }

                            ctx->n1 = 0;
                            ctx->n2 = 0;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_DATA : {
                    /**
                     * n1 - depends on stream
                     * n2 - padding size
                     * pos1 - weight
                     */
                    if (ctx->n2 >= ctx->frame_length) {
                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                            "padded");
                        goto repeat;
                    }

                    auto datasize = ctx->frame_length - static_cast<ssize_t>(ctx->n2);

                    const auto cutsize = std::min(static_cast<ssize_t>(size - pos), datasize);

                    auto s = ctx->streams->end();

                    http_v2_stream_t *sdata = nullptr;

                    if (ctx->frame_type == HTTP2_FRAME_HEADERS) {
                        s = ctx->streams->find(ctx->frame_stream_id);

                        if (s == ctx->streams->end()) {
                            ctx->flags |= HTTP2_CTX_FLAG_NEW_CONNECTION;
                            if (ctx->flags & HTTP2_CTX_FLAG_WANT_CLOSE) {
                                ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;

                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "conn was closed");
                                ctx->n1 = 0;
                                ctx->n2 = 0;
                                ctx->pos1 = 0;
                                goto repeat;
                            }

                            auto p = std::make_unique<http_v2_stream_t>();

                            p->write_window = ctx->client->initial_window_size;
                            p->read_window = ctx->server->initial_window_size;
                            p->id = ctx->frame_stream_id;
                            p->ctx = ctx;

                            auto sconn = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, connection_interface_eraser);
                            p.release();

                            s = ctx->streams->insert({ctx->frame_stream_id, std::move(sconn)}).first;
                            ctx->concurrent_streams_size++;
                            sdata = s->second->as<http_v2_stream_t>();
                            sdata->req = std::make_unique<request_data_t>();
                            sdata->top = std::make_unique<worker::connection_io>();
                            sdata->speed_min_delay = static_cast<int>(config->speed_check_delay);
                        }
                        else {
                            http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                            "stream is half closed(remote)");
                            goto repeat;
                        }
                    }

                    bool flg = ctx->frame_flag & HTTP2_FLAG_HEADERS_END_HEADERS;

                    if (s == ctx->streams->end()) {
                        s = ctx->streams->find(ctx->frame_stream_id);

                        if (s == ctx->streams->end()) {
                            if (ctx->frame_stream_id <= ctx->last_stream_id) {
                                // if (http_v2_rst_stream_ex(ctx, ctx->frame_stream_id, worker::HTTP2_ERROR_STREAM_CLOSED)) {
                                //     http_v2_setup_goaway (ctx, http_goaway,
                                //         worker::HTTP2_ERROR_INTERNAL_ERROR, "internal error");
                                //     goto repeat;
                                // }
                                http_v2_setup_goaway(ctx, http_goaway,
                                    worker::HTTP2_ERROR_STREAM_CLOSED, "stream is closed");
                                goto repeat;
                            }
                            else {
                                http_v2_setup_goaway(ctx, http_goaway,
                                    worker::HTTP2_ERROR_PROTOCOL_ERROR, "stream wasn't created");
                                goto repeat;
                            }

                            // http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR;
                            // http_goaway.err_msg = "stream doesn't exists";
                            // ctx->current = HTTP2_CALLBACK_GOAWAY;
                            flg = false;
                            ctx->decoder->headers(config->max_headers_size);
                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION)
                                ctx->flags ^= HTTP2_CTX_FLAG_NEW_CONNECTION;
                            goto header_skip;
                        }

                        sdata = s->second->as<http_v2_stream_t>();
                    }

                    if (sdata->flags & HTTP2_STREAM_CLOSED) {
                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                            "stream is closed");
                        goto repeat;
                    }

                    if (!sdata->req) {
                        /* connection was already received all headers */
                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                            "stream already received");
                        goto repeat;
                    }

                    if (!(sdata->flags & HTTP2_STREAM_BAD_STATUS)) {
                        auto res = ctx->decoder->decode(std::string_view (buffer + pos, cutsize));

                        if (!res.ok()) {
                            sdata->flags |= HTTP2_STREAM_BAD_STATUS;
                            switch (res.code()) {
                                case ERR_RESOURCE_EXHAUSTED: ctx->status = PAYLOAD_TOO_LARGE_413; break;
                                case ERR_OUT_OF_RANGE: ctx->status = BAD_REQUEST_400; break;
                                default: ctx->status = INTERNAL_SERVER_ERROR_500; break;
                            }
                        }
                    }

header_skip:

                    ctx->frame_length -= cutsize;
                    pos += cutsize;

                    if (!ctx->frame_length) {
                        if (ctx->frame_type == HTTP2_FRAME_HEADERS
                            && ctx->frame_flag & HTTP2_FLAG_HEADERS_END_STREAM)
                            sdata->flags |= HTTP2_STREAM_RECV_END;

                        if (flg) {
                            /**
                             * RFC9218 (4.1) Urgency
                             * The urgency (u) parameter value is Integer (see Section 3.3.1 of
                             * [STRUCTURED-FIELDS]), between 0 and 7 inclusive, in descending order
                             * of priority.  The default is 3.
                             *
                             */
                            sdata->priority = 3;

                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION)
                                ctx->flags ^= HTTP2_CTX_FLAG_NEW_CONNECTION;

                            sdata->req->http = http::versions::HTTP_v2;

                            auto hres = ctx->decoder->headers(config->max_headers_size);

                            if (hres.ok()) {
                                auto headers = std::move(hres.unwrap());
                                while (!headers.empty()) {
                                    auto it = headers.begin();
                                    auto value = std::move(it->second);
                                    auto node = headers.extract(it);
                                    auto &key = node.key();
                                    if (key.empty()) {
                                    }
                                    else {
                                        for (auto &c : key) {
                                            if (isupper(c)) {
                                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                                    "header name uppercase");
                                                goto repeat;
                                            }
                                        }
                                        if (key[0] == ':') {
                                            std::string_view const header_name (key.data() + 1, key.size() - 1);
                                            /* :status for server */
                                            if (header_name != "path" &&
                                                header_name != "method" &&
                                                header_name != "authority" &&
                                                header_name != "scheme") {
                                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                                    "header name incorrect");
                                                goto repeat;
                                            }
                                        }
                                    }

                                    sdata->req->headers.insert({std::move(key), std::move(value)});
                                }

                                if (sdata->req->headers.contains(HEADER.CONNECTION)) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        "connection header is denied");
                                    goto repeat;
                                }

                                auto hv = sdata->req->headers.extract(":method");
                                if (hv.empty() || hv.mapped().empty()) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        ":method is empty");
                                    goto repeat;
                                }

                                sdata->req->method = std::move(hv.mapped());

                                hv = sdata->req->headers.extract("te");
                                if (!hv.empty()) {
                                    auto rhs = parse_header_value(hv.mapped());
                                    if (rhs.ok())
                                        return EHTTP_V2_PROTOCOL_ERROR;
                                    auto p = rhs.unwrap();
                                    if (p.size() != 1) {
                                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                            "TE with invalid value");
                                        goto repeat;
                                    }
                                }

                                hv = sdata->req->headers.extract(":path");
                                if (hv.empty() || hv.mapped().empty()) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        ":path is empty");
                                    goto repeat;
                                }


                                sdata->req->uri = std::move(hv.mapped());

                                hv = sdata->req->headers.extract(":scheme");
                                if (hv.empty() || hv.mapped().empty()) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        ":scheme is empty");
                                    goto repeat;
                                }

                                auto hit = sdata->req->headers.find(http::HEADER.CONTENT_LENGTH);
                                if (hit == sdata->req->headers.end()) {
                                    sdata->req->body_size = manapi::ERR_INTERNAL;
                                }
                                else {
                                    try {
                                        sdata->req->body_size = std::stoll(hit->second);
                                    }
                                    catch (...) {
                                        sdata->req->body_size = manapi::ERR_INTERNAL;
                                    }

                                    if (sdata->req->body_size < 0) {
                                        sdata->req->body_size = 0;
                                        sdata->flags |= HTTP2_STREAM_BAD_STATUS;
                                        ctx->status = BAD_REQUEST_400;
                                    }
                                }

                                sdata->content_length = sdata->req->body_size;

                                url_decode_stream url_decoder;

                                if (auto rhs = url_decoder << sdata->req->uri) {
                                    return EHTTP_V2_PROTOCOL_ERROR;
                                }

                                sdata->req->path = url_decoder.result();
                                sdata->req->divided = url_decoder.divided();


                                try {
                                    hit = sdata->req->headers.find(http::HEADER.PRIORITY);
                                    if (hit != sdata->req->headers.end()) {
                                        auto rhs = parse_header_value(hit->second);
                                        if (!rhs.ok())
                                            return EHTTP_V2_PROTOCOL_ERROR;
                                        auto val = rhs.unwrap();
                                        for (const auto &p : val) {
                                            if (p.value.empty()) {
                                                auto vit = val[0].params.find("u");
                                                if (vit != val[0].params.end()) {
                                                    const auto priority = std::stoi(vit->second);
                                                    if (priority >= HTTP2_PRIORITY_0
                                                        && priority <= HTTP2_PRIORITY_MAX)
                                                        sdata->priority = static_cast<uint8_t> (priority);
                                                }
                                            }
                                            else if (p.value == "i") {
                                                //sdata->flags |= HTTP2_STREAM_PRIORITY_INCR;
                                            }
                                        }
                                    }
                                }
                                catch (std::exception const &e) {
                                    /* ignore */
                                    manapi_log_trace(debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "http2", "priority header", e.what());
                                }
                            }
                            else {
                                // if (sdata->flags & HTTP2_STREAM_BAD_STATUS) {
                                //
                                // }
                                // else {
                                //     MANAPIHTTP_LOG("hpack error({}): {}", hres.status_msg(), hres.message());
                                //     switch (hres.code()) {
                                //         case ERR_RESOURCE_EXHAUSTED: ctx->status = PAYLOAD_TOO_LARGE_413; break;
                                //         case ERR_OUT_OF_RANGE: ctx->status = BAD_REQUEST_400; break;
                                //         default: ctx->status = INTERNAL_SERVER_ERROR_500; break;
                                //     }
                                //     sdata->flags |= HTTP2_STREAM_BAD_STATUS;
                                // }
                                //
                                // sdata->req->divided = -1;
                                // sdata->req->uri = "/";
                                // sdata->req->body_size = 0;

                                /* compression error */
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_COMPRESSION_ERROR,
                                     "headers incorrect");
                                goto repeat;
                            }

                            if (http_v2_insert_priority (ctx, s->second, sdata,
                                http_v2_real_priority_by_stream(sdata))) {
                                http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_INTERNAL_ERROR;
                                http_goaway.err_msg = "priority status failed";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;
                                goto repeat;
                            }
                        }

                        ctx->pos1 = 0;
                        if (ctx->n2) {
                            ctx->n1 = ctx->n2;
                            ctx->n2 = 0;
                            ctx->frame_length -= ctx->n1;
                            ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                        else {
                            ctx->n1 = 0;
                            ctx->n2 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }

                        if (flg) {
                            buffer += pos;
                            size -= pos;
                            pos = 0;

                            if (!(sdata->flags & HTTP2_STREAM_BAD_STATUS))
                                ctx->status = OK_200;


                            // if (http_v2_check_real_working_stream(ctx)) {
                            //     http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_INTERNAL_ERROR;
                            //     http_goaway.err_msg = "working streams";
                            //     ctx->current = HTTP2_CALLBACK_GOAWAY;
                            //     ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;
                            // }
                            return EHTTP_V2_NEW_STREAM;
                        }
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_BODY_DATA : {
                    if (ctx->n2 >= ctx->frame_length) {
                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                            "padded");
                        goto repeat;
                    }

                    auto datasize = ctx->frame_length - ctx->n2;
                    assert((datasize <= ctx->server->max_frame_size));
                    datasize = std::min(static_cast<int>(size - pos), static_cast<int>(datasize));

                    ctx->frame_length -= datasize;
                    ssize_t const datapos = pos;
                    pos += datasize;

                    bool const frame_fin = !(ctx->frame_length-ctx->n2);

                    {
                        //MANAPIHTTP_LOG("RECV DATA {}", len);

                        auto s = ctx->streams->find(ctx->frame_stream_id);
                        if (s == ctx->streams->end()) {
                            if (ctx->frame_stream_id <= ctx->last_stream_id) {
                                if (http_v2_rst_stream_ex(ctx, ctx->frame_stream_id, worker::HTTP2_ERROR_STREAM_CLOSED)) {
                                    http_v2_setup_goaway (ctx, http_goaway,
                                        worker::HTTP2_ERROR_INTERNAL_ERROR, "internal error");
                                    goto repeat;
                                }
                            }
                            else {
                                http_v2_setup_goaway(ctx, http_goaway,
                                    worker::HTTP2_ERROR_PROTOCOL_ERROR, "stream wasn't created");
                                goto repeat;
                            }

                        }
                        else {
                            auto const sdata = s->second->as<http_v2_stream_t>();

                            if (sdata->flags & HTTP2_STREAM_CLOSED) {
                                http_v2_setup_goaway(ctx, http_goaway,
                                    worker::HTTP2_ERROR_STREAM_CLOSED, "stream is closed");
                                goto repeat;
                            }

                            if (sdata->flags & HTTP2_STREAM_RECV_END) {
                                http_v2_setup_goaway(ctx, http_goaway,
                                    worker::HTTP2_ERROR_STREAM_CLOSED, "stream is half closed(remote)");
                                goto repeat;
                            }

                            if (sdata->read_window < datasize || ctx->read_window < datasize) {
                                http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_FLOW_CONTROL_ERROR;
                                http_goaway.err_msg = "read buffer overflow";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;
                                break;
                            }


                            sdata->read_window -= static_cast<int> (datasize);
                            ctx->read_window -= static_cast<int> (datasize);


                            if (http_v2_process_window (s->second, sdata)) {
                                return EHTTP_V2_PROTOCOL_ERROR;
                            }

                            sdata->transfered_k += static_cast<int> (datasize);

                            if ((sdata->flags & ev::DISCONNECT)) {
                                http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_REFUSED_STREAM;
                                http_goaway.err_msg = "stream was closed";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                ctx->flags |= HTTP2_CTX_FLAG_REALY_CLOSE;
                                break;
                            }

                            /**
                             * recv data
                             */
                            if (worker::http_v2_flush_recv (config, s->second, sdata)) {
                                return EHTTP_V2_PROTOCOL_ERROR;
                            }

                            if (sdata->content_length >= 0) {
                                sdata->content_length -= datasize;
                                if (sdata->content_length < 0 ||
                                    (sdata->content_length != 0 && frame_fin && ctx->frame_flag & HTTP2_FLAG_DATA_END_STREAM)) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        "content-length incorrect");
                                    goto repeat;
                                }
                            }

                            if (datasize) {
                                if ((sdata->flags & ev::READ)
                                    && sdata->ev_callback && !sdata->top->recv_size && !config->max_merge_buffer_stack) {
                                    /* CHECK IF CALLBACK IS ASYNC/SYNC */
                                    int flags = ev::READ;
                                    if (ctx->frame_flag & HTTP2_FLAG_DATA_END_STREAM) {
                                        sdata->flags |= HTTP2_STREAM_RECV_END;
                                        flags |= HTTP2_STREAM_RECV_END;
                                    }

                                    sdata->ev_callback->operator()(s->second, flags, buffer + datapos, datasize, nullptr);
                                }
                                else {
                                    if (ctx->frame_flag & HTTP2_FLAG_DATA_END_STREAM)
                                        sdata->flags |= HTTP2_STREAM_RECV_END;


                                    if (datasize != worker::base::connection_io_send(&sdata->top->recv, buffer + datapos,
                                        datasize, &sdata->ctx->worker->bufferpool(), static_cast<int>(config->buffer_size),
                                        &sdata->top->recv_size, maxcnt))
                                        return EHTTP_V2_PROTOCOL_ERROR;

                                    if (sdata->flags & ev::READ) {
                                        if (worker::http_v2_flush_recv (config, s->second, sdata)) {
                                            return EHTTP_V2_PROTOCOL_ERROR;
                                        }
                                    }

                                    if (sdata->top->recv_size >= config->max_buffer_stack)
                                        ctx->worker->event_toggle(ctx->conn, false, ev::READ);
                                }
                            }
                            else {
                                if (ctx->frame_flag & HTTP2_FLAG_DATA_END_STREAM) {
                                    sdata->flags |= HTTP2_STREAM_RECV_END;
                                    if (!sdata->top->recv_size && (sdata->flags & ev::READ) && sdata->ev_callback)
                                        sdata->ev_callback->operator()(s->second, HTTP2_STREAM_RECV_END, buffer + datapos, datasize, nullptr);
                                }
                                else {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        "data frame is empty");
                                    goto repeat;
                                }
                            }

                            // if (sdata->recv_size < config->max_buffer_stack) {
                            //     ctx->worker->event_toggle(ctx->conn, true, ev::READ);
                            // }
                        }
                    }

                    if (frame_fin) {
                        if (ctx->n2) {
                            ctx->n1 = ctx->n2;
                            ctx->n2 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                        else {
                            ctx->n1 = 0;
                            ctx->n2 = 0;
                            ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_SKIP_N_BYTES: {
                    while (pos != size) {
                        pos++;
                        if (--ctx->n1 <= 0) {
                            ctx->n1 = 0;
                            ctx->current = ctx->next;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_FIELD_BLOCK: {
                    switch (ctx->frame_type) {
                        case HTTP2_FRAME_DATA: {
                            if (!(ctx->frame_stream_id % 2)) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid DATA");
                                goto finish;
                            }
                            if (ctx->frame_flag & HTTP2_FLAG_DATA_PADDED) {
                                if (ctx->frame_length <= 0) {
                                    http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                        "frame is empty");
                                    goto finish;
                                }

                                ctx->n1 = 1; /* size */
                                ctx->n2 = 0; /* return value */
                                ctx->current = HTTP2_CALLBACK_PARSE_NUMBER;
                                ctx->next = HTTP2_CALLBACK_PARSE_BODY_DATA;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_BODY_DATA;
                            }
                            break;
                        }
                        case HTTP2_FRAME_HEADERS: {
                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION) {
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "continue instead of header");
                                goto finish;
                            }

                            size_t additional = 0;
                            if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PADDED)
                                additional += 1;
                            if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PRIORITY)
                                additional += 5;

                            if (ctx->frame_length <= additional) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "frame is empty");
                                goto finish;
                            }

                            if (!ctx->frame_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "0 is reserved");
                                goto finish;
                            }

                            if (ctx->frame_stream_id < ctx->last_stream_id || (ctx->frame_stream_id % 2 == 0)) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "unexpected stream id");
                                goto finish;
                            }


                            if (ctx->frame_stream_id == ctx->last_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_STREAM_CLOSED,
                                    "stream is closed");
                                goto finish;
                            }


                            if (ctx->concurrent_streams_size >= ctx->server->max_concurret_streams) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_REFUSED_STREAM,
                                    "max concurrent stream");
                                goto finish;
                            }

                            ctx->last_stream_id = ctx->frame_stream_id;

                            if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PADDED) {
                                ctx->n1 = 1; /* number size */
                                ctx->n2 = 0; /* return value */
                                ctx->current = HTTP2_CALLBACK_PARSE_NUMBER;
                                ctx->next = HTTP2_CALLBACK_PARSE_HEADER_PRIORITY;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_HEADER_PRIORITY;
                            }

                            break;
                        }
                        case HTTP2_FRAME_PRIORITY: {
                            if (ctx->frame_length != 5) {
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "invalid PRIORITY");
                                goto repeat;
                            }
                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION) {
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "headers is expected");
                                goto repeat;
                            }

                            /* stream id contains 31 bit (but the last one (32) is reserved) */
                            ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY1;
                            ctx->next = HTTP2_CALLBACK_PRIORITY_FRAME;
                            break;
                        }
                        case HTTP2_FRAME_RST_STREAM: {
                            if (!ctx->frame_stream_id || ctx->frame_length != 4) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid RST_STREAM");
                                goto finish;
                            }

                            ctx->current = HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION;
                            break;
                        }
                        case HTTP2_FRAME_SETTINGS: {
                            // setting param size - 6 bytes
                            if (ctx->frame_length % 6 != 0 || ctx->frame_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid SETTINGS");
                                goto finish;
                            }

                            if (!ctx->frame_length) {
                                if (!(ctx->frame_flag & HTTP2_FLAG_SETTINGS_ACK)) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        "invalid ack settings");
                                    goto finish;
                                }
                                /* ack server settings */
                                ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                                if (ctx->timeout) {
                                    ctx->timeout.stop();
                                    ctx->timeout = nullptr;
                                }
                                else {

                                }
                            }
                            else {
                                if ((ctx->frame_flag & HTTP2_FLAG_SETTINGS_ACK)) {
                                    http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                        "invalid ack settings");
                                    goto finish;
                                }
                                ctx->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
                            }
                            break;
                        }
                        case HTTP2_FRAME_PUSH_PROMISE: {
                            /* The endpoint MUST treat this as a connection error of type PROTOCOL_ERROR */
                            http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                "push promise incorrect");
                            goto repeat;
                        }
                        case HTTP2_FRAME_PING: {
                            if (ctx->frame_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "invalid PING");
                                goto finish;
                            }
                            if (ctx->frame_length != 8) {
                                /**
                                 * RFC9113 (6.7) PING
                                 *
                                 * Receipt of a PING frame with a length field value other than
                                 * 8 MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
                                 */
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid PING");
                                goto finish;
                            }
                            ctx->current = HTTP2_CALLBACK_PARSE_PING_DATA;
                            break;
                        }
                        case HTTP2_FRAME_GOAWAY: {
                            if (ctx->frame_length < 8 || ctx->frame_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid GOAWAY");
                                goto finish;
                            }
                            ctx->current = HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID;
                            break;
                        }
                        case HTTP2_FRAME_WINDOW_UPDATE: {
                            if (ctx->frame_length != 4) {
                                /**
                                 * RFC9113 (6.9) WINDOW_UPDATE
                                 *
                                 * A WINDOW_UPDATE frame with a length other
                                 * than 4 octets MUST be treated as
                                 * a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
                                 */


                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "invalid WINDOW_UPDATE");
                                goto finish;
                            }
                            ctx->current = HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE;
                            break;
                        }
                        case HTTP2_FRAME_CONTINUATION: {
                            if (!ctx->frame_stream_id) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                        "invalid 0 is reserved");
                                goto finish;
                            }

                            if (!ctx->frame_length) {
                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                                    "frame is empty");
                                goto finish;
                            }

                            if (ctx->frame_stream_id != ctx->last_stream_id) {

                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "provided stream id isn't handled");

                                goto finish;
                            }

                            ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                            break;
                        }
                        case HTTP2_FRAME_PRIORITY_UPDATE: {
                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION) {
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "headers is expected");
                                goto repeat;
                            }
                            /**
                             * RFC9218 (7.1) HTTP/2 PRIORITY_UPDATE Frame
                             *
                             * The Stream Identifier field (see Section 5.1.1 of [HTTP/2])
                             * in the PRIORITY_UPDATE frame header MUST be zero (0x0).
                             * Receiving a PRIORITY_UPDATE frame with a field of any
                             * other value MUST be treated as a connection error of type PROTOCOL_ERROR.
                             *
                             */
                            if (ctx->frame_stream_id != 0) {

                                http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_CONNECT_ERROR,
                                    "PRIORITY_UPDATE incorrect");
                                goto repeat;
                            }

                            /* stream id contains 31 bit (but the last one (32) is reserved) */
                            ctx->n1 = 4;
                            ctx->current = HTTP2_CALLBACK_PARSE_NUMBER;
                            ctx->next = HTTP2_CALLBACK_PARSE_PRI_UPDATE_FIELD;
                            ctx->frame_length -= ctx->n1;

                            break;
                        }
                        default: {
                            if (ctx->flags & HTTP2_CTX_FLAG_NEW_CONNECTION) {
                                http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                    "undefined frame");
                                goto repeat;
                            }
                            /* undefined frame */
                            ctx->n1 = ctx->frame_length;
                            ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                    }

                    if (ctx->frame_length > ctx->server->max_frame_size) {
                        http_v2_setup_goaway(ctx, http_goaway, manapi::net::worker::HTTP2_ERROR_FRAME_SIZE_ERROR,
                            "frame length is invalid");
                        goto finish;
                    }

                    break;
                    finish:
                        goto repeat;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_PRIORITY: {
                    if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PRIORITY) {
                        ctx->n1 = 0;
                        ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY1;
                        ctx->next = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                    }
                    else {
                        ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_WEIGHT: {
                    ctx->frame_length--;
                    ctx->current = ctx->next;
                    ctx->next = HTTP2_CALLBACK_ERROR;
                    ctx->pos1 = static_cast<uint32_t>(buffer[pos++]);
                    goto repeat;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY1:
                case HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY2:
                case HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY3:
                case HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY4: {
                    ctx->frame_length--;
                    ctx->n1 = static_cast<int>((static_cast<unsigned int>(ctx->n1) << 8) | static_cast<unsigned char> (buffer[pos++]));

                    if (ctx->current == HTTP2_CALLBACK_PARSE_HEADER_DEPENDENCY4) {
                        if (ctx->n1 == ctx->frame_stream_id) {
                            http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR,
                                "depends on itself");
                            goto repeat;
                        }
                        ctx->current = HTTP2_CALLBACK_PARSE_HEADER_WEIGHT;
                    }
                    else
                        ctx->current++;

                    break;
                }
                case HTTP2_CALLBACK_PRIORITY_FRAME: {
                    /**
                     * n1 - depends on stream
                     * pos1 - weight
                     */

                    ctx->n1 = 0;
                    ctx->pos1 = 0;
                    ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                    break;
                }
                case HTTP2_CALLBACK_PARSE_NUMBER: {
                    while (pos != size) {
                        if (ctx->n1 == 0) {
                            ctx->current = ctx->next;
                            ctx->next = HTTP2_CALLBACK_ERROR;
                            break;
                        }

                        ctx->n2 = static_cast<int>((static_cast<unsigned int>(ctx->n2) << 8) | static_cast<unsigned char> (buffer[pos++]));
                        ctx->n1--;
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_PRI_UPDATE_FIELD: {

                    auto copy = std::min(static_cast<ssize_t>(ctx->frame_length), size - pos);
                    if (ctx->frame_buffer.size() + copy > 64) {
                        ctx->frame_buffer.clear();
                        ctx->n2 = 0;

                        http_v2_setup_goaway(ctx, http_goaway, worker::HTTP2_ERROR_PROTOCOL_ERROR, "overflow");
                        break;
                    }

                    ctx->frame_buffer.append(buffer + pos, copy);

                    pos += copy;
                    ctx->frame_length -= copy;

                    if (!ctx->frame_length) {
                        /**
                         * n2 - Stream ID
                         */

                        /**
                         * RFC9218 (7.1) HTTP/2 PRIORITY_UPDATE Frame
                         *
                         * The priority update value in ASCII text,
                         * encoded using Structured Fields.
                         * This is the same representation as the
                         * Priority header field value.
                         */

                        auto const sit = ctx->streams->find(ctx->n2 & 0x7FFFFFFF);
                        ctx->n2 = 0;

                        ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;

                        if (sit == ctx->streams->end()) {
                            /**
                             * RFC9218 (7.1) HTTP/2 PRIORITY_UPDATE Frame
                             *
                             * Servers can discard frames where
                             * the prioritized stream ID refers
                             * to a stream in the "half-closed
                             * (local)" or "closed" state
                             * (i.e., streams where no further
                             * data will be sent).
                             */
                        }
                        else {
                            auto const sdata = sit->second->as<http_v2_stream_t>();
                            uint8_t npriority = 3;
                            bool nincr = false;

                            {
                                auto rhs = parse_header_value(ctx->frame_buffer);
                                if (rhs.ok()) {
                                    auto val = rhs.unwrap();
                                    for (const auto &p : val) {
                                        if (p.value.empty()) {
                                            auto vit = val[0].params.find("u");
                                            if (vit != val[0].params.end()) {
                                                const auto priority = std::stoi(vit->second);
                                                if (priority >= HTTP2_PRIORITY_0
                                                    && priority <= HTTP2_PRIORITY_MAX)
                                                    npriority = static_cast<uint8_t> (priority);
                                            }
                                        }
                                        else if (p.value == "i") {
                                            nincr = true;
                                        }
                                    }
                                }
                                else {
                                    return EHTTP_V2_PROTOCOL_ERROR;
                                }
                            }
                            ctx->frame_buffer.clear();

                            auto const prev_sdata_flags = sdata->flags;

                            if (http_v2_remove_priority(ctx, sdata, http_v2_real_priority_by_stream(sdata))) {
                                http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "priority update failed";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                            sdata->priority = npriority;

                            // if (nincr)
                            //     sdata->flags |= HTTP2_STREAM_PRIORITY_INCR;
                            // else if (sdata->flags & HTTP2_STREAM_PRIORITY_INCR)
                            //     sdata->flags ^= HTTP2_STREAM_PRIORITY_INCR;

                            if (http_v2_insert_priority(ctx, sit->second, sdata, http_v2_real_priority_by_stream(sdata))) {
                                http_goaway.err_code = manapi::net::worker::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "priority update failed";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                            // if ((prev_sdata_flags & HTTP2_STREAM_PRIORITY_LOCKED)
                            //     && !(sdata->flags & HTTP2_STREAM_PRIORITY_LOCKED)) {
                            //     ctx->http_v2_worker->feed_event(sit->second, ev::WRITE, nullptr, 0, nullptr);
                            // }
                        }

                    }

                    break;
                }
                case HTTP2_CALLBACK_GOAWAY: {
                    /**
                     * RFC9113 (6.8) GOAWAY
                     *
                     * GOAWAY Frame {
                     *  Length (24),
                     *  Type (8) = 0x07,
                     *
                     *  Unused Flags (8),
                     *
                     *  Reserved (1),
                     *  Stream Identifier (31) = 0,
                     *
                     *  Reserved (1),
                     *  Last-Stream-ID (31),
                     *  Error Code (32),
                     *  Additional Debug Data (..),
                     * }
                     *
                     * The GOAWAY frame does not define any flags.
                     *
                     * The GOAWAY frame applies to the connection, not a specific stream.
                     *
                     */

                    ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                    if (!(ctx->flags & HTTP2_CTX_FLAG_WANT_CLOSE)) {
                        ctx->flags |= HTTP2_CTX_FLAG_WANT_CLOSE;

                        if (http_v2_send_goaway (ctx, &http_goaway)) {
                            return EHTTP_V2_PROTOCOL_ERROR;
                        }
                    }

                    if (ctx->streams->empty()
                        || ctx->flags & HTTP2_CTX_FLAG_REALY_CLOSE)
                        return EHTTP_V2_PROTOCOL_OK;

                    break;
                }
                case HTTP2_CALLBACK_ERROR: {
                    return EHTTP_V2_PROTOCOL_ERROR;
                }
                default: {
                    return EHTTP_V2_PROTOCOL_ERROR;
                }
            }
        }
    }
    catch (std::exception const &e) {
        ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;
        /* some errors */
        MANAPIHTTP_LOG("http2 bug: {}", e.what());
        return EHTTP_V2_PROTOCOL_ERROR;
    }

    return EHTTP_V2_PROTOCOL_WANT_READ;
}

ssize_t manapi::net::http::http_v2_write(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT {
    auto s = conn->as<http_v2_stream_t>();
    if (s->flags & (HTTP2_STREAM_CLOSED|http::HTTP2_STREAM_PRIORITY_LOCKED))
        return (s->flags & ev::DISCONNECT) ? -1 : 0;

    if (s->ctx->flags & http::HTTP2_CTX_FLAG_BLOCK_WRITE)
        return 0;

    auto copy = worker::base::buffs_cut_by_size (buff, nbuff,
        (std::min<ssize_t>(s->write_window, s->ctx->write_window)), finish);

    if (copy <= 0)
        return 0;

    if (!(s->write_window -= static_cast<int>(copy))) {
        if (http_v2_update_priority(s->ctx, conn, s))
            return -1;
    }

    s->ctx->write_window -= static_cast<int>(copy);

    ssize_t res = 0;

    while (res != copy) {
        auto frame_size = static_cast<std::size_t>(s->ctx->client->max_frame_size);

        bool fin = finish;
        uint32_t pnbuff = nbuff;
        std::size_t lencut = 0;
        ssize_t size = 0;
        char *bufcut{nullptr};

        for (uint32_t i = 0; i < pnbuff; ++i) {
            auto const want = (frame_size - size);

            if (buff[i].len >= want) {
                /* cut it */
                lencut = buff[i].len - (want);
                buff[i].len = want;
                bufcut = buff[i].base + want;
                /* current number */
                pnbuff = i + 1;
                /* obviously */
                size = static_cast<ssize_t>(frame_size);
                /* was cut */
                if (fin)
                    fin = pnbuff == nbuff && lencut == 0;

                break;
            }

            size += static_cast<ssize_t>(buff[i].len);
        }

        if (http_v2_send_data_frame(s->ctx, s->id, buff, pnbuff, size, fin))
            return -1;

        res += size;

        if (!s->ctx->http_v2_worker->is_writable(conn))
            break;

        if (lencut) {
            pnbuff--;
            /* pnbuff as i since now */
            buff[pnbuff].base = bufcut;
            buff[pnbuff].len = lencut;
        }

        buff += pnbuff;
        nbuff -= pnbuff;
    }

    s->transfered_k += static_cast<int>(res);

    if (finish && res == copy) {
        if (!(s->flags & HTTP2_STREAM_SEND_END)) {
            /* yay */
            s->flags |= HTTP2_STREAM_SEND_END;
            s->ctx->concurrent_streams_size--;
        }
    }

    return res;
}

int manapi::net::http::http_v2_rst_stream(const worker::shared_conn &s, int errcode) MANAPIHTTP_NOEXPECT {
    auto const data = s->as<http_v2_stream_t>();
    return http_v2_rst_stream_ex (data->ctx, data->id, errcode);
}

manapi::future<int> manapi::net::http::http_v2_response(worker::base *worker, const worker::shared_conn &connection, int status, std::map<std::string, std::string, std::less<>> headers, bool finish) {
    auto const s = connection->as<http_v2_stream_t>();

    if (s->flags & ev::DISCONNECT)
        co_return manapi::ERR_ABORTED;

    *s->ctx->encoder = decltype(s->ctx->encoder)::element_type ();
    s->ctx->encoder->max_table_size(s->ctx->server->header_table_size);
    s->ctx->encoder->add (compress::hpack::header_t(":status", std::to_string(status)));
    for (auto &header: headers) {
        s->ctx->encoder->add (compress::hpack::header_t(header.first, std::move(header.second)));
    }
    uint8_t cflag = 0x0;
    const auto data = s->ctx->encoder->data();
    size_t cnt = 0;
    auto frameSize = static_cast<size_t>(s->ctx->client->max_frame_size);
    http2_frame_type ft = HTTP2_FRAME_HEADERS;
    if (finish) {
        s->flags |= HTTP2_STREAM_SEND_END;
        cflag |= HTTP2_FLAG_HEADERS_END_STREAM;
        s->ctx->concurrent_streams_size--;
    }
    goto skip;
    while (cnt < data.size()) {
        ft = HTTP2_FRAME_CONTINUATION;
        skip:
        auto left = std::min(frameSize, data.size() - cnt);
        if (cnt + left == data.size()) {
            cflag |= HTTP2_FLAG_HEADERS_END_HEADERS;
        }
        ev::buff_t buf;
        buf.base = (char*)data.data() + cnt;
        buf.len = static_cast<std::size_t>(left);
        if (http_v2_send_frame(s->ctx, ft, cflag, s->id, &buf, 1, buf.len)) {
            co_return manapi::ERR_INTERNAL;
        }
        cflag = 0;
        cnt += left;
    }
    co_return manapi::ERR_OK;
}
