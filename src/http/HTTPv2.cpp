#include "http/HTTPv2.hpp"

#include "encoding/ManapiUnicode.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "encoding/ManapiURL.hpp"
#include "components/ManapiURLDecodeStream.hpp"

#include "worker/HTTPv2.hpp"


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
    HTTP2_CALLBACK_PARSE_NUMBER,
    HTTP2_CALLBACK_GOAWAY,
    HTTP2_CALLBACK_FINISH,
    HTTP2_CALLBACK_ERROR
};

struct http_v2_goaway_t {
    int err_code{0};
    std::string err_msg;
};

static constexpr char smlabel[] = "\r\n\r\nSM\r\n";
static constexpr int maxcnt = 1e9;
static constexpr int conn_max_window_hlf = 2000000 / 2;
static constexpr int conn_min_stream_summary = 400000;

std::map <int, manapi::json_mask> allow_settings {
        {HTTP2_SETTING_RESERVED, manapi::json{"{null}"}},
        {HTTP2_SETTING_ENABLE_PUSH, manapi::json{"{integer(>=0 <=1)}"}},
        {HTTP2_SETTING_MAX_FRAME_SIZE, manapi::json{"{integer(>=16000 <=100000)}"}},
        {HTTP2_SETTING_HEADER_TABLE_SIZE, manapi::json{"{integer(>=2048 <=65536)}"}},
        {HTTP2_SETTING_INITIAL_WINDOW_SIZE, manapi::json{"{integer(>=1024 <=80000)}"}},
        {HTTP2_SETTING_MAX_HEADER_LIST_SIZE, manapi::json{"{integer(>=1024 <=1048576)}"}},
        {HTTP2_SETTING_TLS_RENEG_PERMITTED, manapi::json{"{integer(0)}"}},
        {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, manapi::json{"{integer(>=1 <=5)}"}},
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
int http_v2_send_frame (manapi::net::http::http_v2_t *ctx,  int frame_type, uint8_t flags, int stream_id, const manapi::ev::buff_t buffs[], int nbuff) {
    char header[9];
    int size = 0;

    for (int i = 0; i < nbuff; ++i) {
        size += static_cast<int>(buffs[i].len);
    }

    stringify_stream_id(stream_id, header + 5);
    stringify_number <int> (size, header, 3);
    header[3] = static_cast<char> (frame_type);
    header[4] = static_cast<char> (flags);

    auto rhs = ctx->worker->sync_write_ex(ctx->conn, header, sizeof (header), !size, maxcnt);
    if (rhs != sizeof (header))
        return -1;

    if (size) {
        for (int i = 0; i < nbuff; ++i) {
            rhs = ctx->worker->sync_write_ex(ctx->conn,
                static_cast<const char *>(buffs[i].base), static_cast<ssize_t>(buffs[i].len), nbuff == i + 1, maxcnt);
            if (rhs != buffs[i].len)
                return -1;
        }
    }

    return 0;
}

int http_v2_send_window_frame (manapi::net::http::http_v2_t *ctx,  int stream_id, int size) {
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

        return -1;
    }

    char out[4];
    stringify_number<int>(size, out, 4);
    manapi::ev::buff_t const buff = {out, 4};

    return http_v2_send_frame(ctx,  HTTP2_FRAME_WINDOW_UPDATE, 0, stream_id, &buff, 1);
}

int http_v2_send_ping_frame (manapi::net::http::http_v2_t *ctx, char *data) {
    if (data) {
        manapi::ev::buff_t const buf = {.base = data, .len = 8};

        if (http_v2_send_frame(ctx, HTTP2_FRAME_PING, HTTP2_FLAG_PING_ACK, 0,&buf, 1)) {
            return -1;
        }
    }
    else {
        auto s = manapi::crypto::random_string(8);
        if (!ctx->pings) {
            ctx->pings = std::make_unique<decltype(ctx->pings)::element_type>();
        }
        manapi::ev::buff_t const buf = {.base = s.data(), .len = 8};
        if (http_v2_send_frame(ctx, HTTP2_FRAME_PING, 0, 0, &buf, 1)) {
            return -1;
        }
        ctx->pings->insert(std::move(s));
    }

    return 0;
}

int http_v2_send_data_frame (manapi::net::http::http_v2_t *ctx,  int stream_id, manapi::ev::buff_t const buffs[], int nbuff, bool finish) {
    return http_v2_send_frame(ctx, HTTP2_FRAME_DATA, finish ? HTTP2_FLAG_DATA_END_STREAM : 0, stream_id, buffs, nbuff);
}

int http_v2_send_data_frame (manapi::net::http::http_v2_t *ctx, int stream_id, const char *data, ssize_t size, bool finish) {
    manapi::ev::buff_t buff = {.base = (char*)(data), .len = static_cast<size_t>(size)};
    return http_v2_send_frame(ctx, HTTP2_FRAME_DATA, finish ? HTTP2_FLAG_DATA_END_STREAM : 0, stream_id, &buff, 1);
}

int http_v2_apply_setting (manapi::net::http::http_v2_t *ctx, int key, int value, bool server) {
    auto const settings = server ? ctx->server.get() : ctx->client.get();

    switch (key) {
        case HTTP2_SETTING_HEADER_TABLE_SIZE: {
            settings->header_table_size = value;
            if (server) {
                ctx->encoder->max_table_size(value);
            }
            else {
                ctx->decoder->m_dynamic_max(value);
            }
            break;
        }
        case HTTP2_SETTING_ENABLE_PUSH:
            settings->enable_push = value;
        break;
        case HTTP2_SETTING_MAX_FRAME_SIZE:
            settings->max_frame_size = value;
        break;
        case HTTP2_SETTING_INITIAL_WINDOW_SIZE:
            settings->initial_window_size = value;
        break;
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

int http_v2_send_goaway (manapi::net::http::http_v2_t *ctx, http_v2_goaway_t *http_goaway) {
    manapi::ev::buff_t data[2];
    char nums[8];

    data[0].base = nums;
    data[0].len = sizeof (nums);

    data[1].base = http_goaway->err_msg.data();
    data[1].len = http_goaway->err_msg.size();

    stringify_number<int> (ctx->last_stream_id, data[0].base);
    stringify_number<int>(http_goaway->err_code, data[0].base + 4);

    manapi::async::current()->logger()->debug(manapi::logger::default_service, "HTTP2: GOAWAY SEND. "
                                                                                    "err code: {}, stream id: {}, msg: {}", http_goaway->err_code, ctx->last_stream_id, http_goaway->err_msg);

    if (auto rhs = http_v2_send_frame(ctx, HTTP2_FRAME_GOAWAY, 0, 0, data, 2)) {
        return -1;
    }

    return 0;
}

int http_v2_send_settings (manapi::net::http::http_v2_t *ctx, const std::vector<std::pair<short, int>> & options) {
    if (ctx->timeout) {
        return -1;
    }

    size_t const len = options.size() * 6;
    char buffer[len];
    for (int i = 0; i < options.size(); ++i) {
        if (http_v2_apply_setting(ctx, options[i].first, options[i].second, true)) {
            return -1;
        }

        stringify_number<short>(options[i].first, buffer + i * 6);
        stringify_number<int>(options[i].second, buffer + i * 6 + 2);
    }
    manapi::ev::buff_t const bufs = {.base = buffer, .len = len};
    if (http_v2_send_frame(ctx, HTTP2_FRAME_SETTINGS, 0, 0, &bufs, 1)) {
        return -1;
    }

    ctx->timeout = manapi::async::current()->timerpool()->append_timer_sync(3000,
        [ctx] (manapi::timer t) -> void {
            http_v2_goaway_t http_goaway = {
                .err_code = manapi::net::http::HTTP2_ERROR_SETTINGS_TIMEOUT,
                .err_msg = "SETTINGS timeout"
            };

            auto const rhs = http_v2_send_goaway (ctx, &http_goaway);
            ctx->worker->feed_event(ctx->conn, manapi::ev::DISCONNECT, nullptr, 0, nullptr);
    });

    return 0;
}

int http_v2_flush_recv (const manapi::net::worker::shared_conn &conn, manapi::net::http::http_v2_stream_t *s) {
    while (s->recv->last_deque && (s->flags & manapi::ev::READ)) {
        auto b = std::move(s->recv->deque->buffer);
        s->recv->deque = std::move(s->recv->deque->next);
        s->recv_size--;

        if (!s->recv->deque) {
            s->recv->last_deque = nullptr;
            b->resize(s->recv->deque_cursor);

            s->recv->deque_cursor = 0;
        }

        if (s->recv->deque_current) {
            b->shift_add(s->recv->deque_current);
            s->recv->deque_current = 0;
        }

        s->ev_callback->operator()(conn, manapi::ev::READ, b->data(), static_cast<ssize_t>(b->size()), &b);
    }

    return 0;
}

int http_v2_rst_stream_ex (manapi::net::http::http_v2_t *ctx, int stream_id, int errcode) {
    char errid[4];
    stringify_stream_id(stream_id, errid);
    manapi::ev::buff_t buff = {.base = errid, .len = 4};
    return http_v2_send_frame(ctx, HTTP2_FRAME_RST_STREAM, 0, stream_id, &buff, 1);
}

int manapi::net::http::http_v2_on_close (http_v2_t *ctx) {
    if (ctx->timeout) {
        ctx->timeout.stop();
        ctx->timeout = nullptr;
    }

    for (const auto &s : *ctx->streams) {
        auto const data = s.second->as<http_v2_stream_t>();
        data->flags |= ev::DISCONNECT;
        if (data->ev_callback) {
            data->ev_callback->operator()(s.second, ev::DISCONNECT, nullptr, 0, nullptr);
        }
    }

    return 0;
}

int manapi::net::http::http_v2_on_close_stream(http_v2_t *ctx, int id) {
    auto it = ctx->streams->find(id);
    if (it == ctx->streams->end()) {
        return -1;
    }
    auto s = it->second->as<http_v2_stream_t>();
    if (!(s->flags & HTTP2_STREAM_SEND_END)) {
        ctx->concurrent_streams_size--;
        s->flags |= HTTP2_STREAM_SEND_END;
    }
    ctx->streams->erase(it);
    return 0;
}

int manapi::net::http::http_v2_on_write(http_v2_t *ctx) {
    bool no_one = true;
    for (const auto &s : *ctx->streams) {
        auto const data = s.second->as<http_v2_stream_t>();
        if ((data->flags & ev::WRITE)) {
            no_one = false;
            if (data->ev_callback)
                data->ev_callback->operator()(s.second, ev::WRITE, nullptr, 0, nullptr);
        }
    }

    if (no_one) {
        ctx->worker->event_toggle(ctx->conn, false, ev::WRITE);
    }

    return 0;
}

int http_v2_process_window (const manapi::net::worker::shared_conn &conn, manapi::net::http::http_v2_stream_t *s) {
    auto ssw = (s->recv_size + 1) * s->ctx->worker->config()->buffer_size;
    ssw = std::max(static_cast<ssize_t>(0),
        static_cast<ssize_t>(conn_min_stream_summary - ssw));

    if (s->read_window < ssw) {
        const auto allow = static_cast<int>(ssw - s->read_window);
        if (http_v2_send_window_frame(s->ctx, s->id,
            allow)) {
            return -1;
            }
        s->read_window += allow;
    }

    if (s->ctx->read_window <= conn_max_window_hlf) {
        const auto allow = conn_max_window_hlf + conn_max_window_hlf - s->ctx->read_window;
        if (http_v2_send_window_frame(s->ctx, 0,
            allow)) {
            return -1;
            }
        s->ctx->read_window += allow;
    }
    return 0;
}

int manapi::net::http::http_v2_on_read_stream(const worker::shared_conn &conn, http_v2_stream_t *s) {
    if (http_v2_flush_recv (conn, s)) {
        return -1;
    }
    return http_v2_process_window (conn, s);
}

int manapi::net::http::http_v2_work(http_v2_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize) {
    http_v2_goaway_t http_goaway;

    ssize_t pos = 0;

    auto &buffer = *nbuffer;
    auto &size = *nsize;

    try {
        while (pos != size) {
            switch(ctx->current) {
                case HTTP2_CALLBACK_INIT: {
                    ctx->current = HTTP2_CALLBACK_SKIP_MSG;
                    ctx->next = HTTP2_CALLBACK_ERROR;


                    ctx->last_stream_id = 0;
                    ctx->n1 = 0;
                    ctx->n2 = 0;
                    ctx->pos1 = 0;

                    ctx->read_window = 65535;
                    ctx->write_window = 65535;

                    ctx->server = std::make_unique<http_v2_settings_t>();
                    ctx->client = std::make_unique<http_v2_settings_t>();

                    ctx->decoder = std::make_unique<decltype(ctx->decoder)::element_type>();
                    ctx->encoder = std::make_unique<decltype(ctx->encoder)::element_type>();

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

                    if (http_v2_send_settings(ctx, {
                        {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
                        {HTTP2_SETTING_ENABLE_PUSH, 0},
                        {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, 100},
                        {HTTP2_SETTING_MAX_HEADER_LIST_SIZE,  65000},
                        {HTTP2_SETTING_INITIAL_WINDOW_SIZE, 65535}
                    })) {
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
                            http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                            http_goaway.err_msg = "invalid sm label";
                            ctx->current = HTTP2_CALLBACK_GOAWAY;
                            break;
                        }

                        if (++pos == sizeof (smlabel) - 1) {
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                            ctx->current = HTTP2_CALLBACK_NEXT_LINE;
                            ctx->pos1 = 0;

                            break;
                        }
                        ctx->pos1++;
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
                case HTTP2_CALLBACK_PARSE_HEADER_TYPE:
                    ctx->frame_type = static_cast<int>(static_cast<unsigned char> (buffer[pos++]));
                ctx->current = HTTP2_CALLBACK_PARSE_HEADER_FLAG;
                break;
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

                            if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PRIORITY) {
                                //deprecated
                                ctx->n1 = 5; // skip 5 bytes
                                ctx->frame_length -= ctx->n1;

                                ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                                ctx->next = HTTP2_CALLBACK_PARSE_FIELD_BLOCK;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_FIELD_BLOCK;
                            }

                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_HEADER_FLAG:

                    ctx->frame_flag = static_cast<int>(static_cast<unsigned char>(buffer[pos++]));
                ctx->current = HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID;
                break;
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
                    auto const copy = std::min(static_cast<ssize_t>(ctx->frame_length), size - pos);
                    ctx->frame_buffer.append(buffer + pos, copy);

                    pos += copy;
                    ctx->frame_length -= copy;

                    if (!ctx->frame_length) {
                        /*
                         * connection was closed
                         * n1 - error code
                         * n2 - last stream id
                         * frame_buffer - additional debug data
                         */

                        ctx->current = HTTP2_CALLBACK_FINISH;

                        /* TODO: close connection */

                        manapi::async::current()->logger()->debug(manapi::logger::default_service, "HTTP2: GOAWAY RECV. "
                                                                                                   "err code: {}, stream id: {}, msg: {}", ctx->n1, ctx->n2, ctx->frame_buffer);

                        ctx->n1 = 0;
                        ctx->n2 = 0;
                        ctx->frame_buffer.clear();

                        return EHTTP_V2_PROTOCOL_OK;
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

                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "invalid WINDOW_UPDATE";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                            if (ctx->frame_stream_id) {
                                auto const s = ctx->streams->find(ctx->frame_stream_id);

                                if (s == ctx->streams->end()) {
                                    ctx->n1 = 0;
                                    http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                    http_goaway.err_msg = "stream id is invalid";
                                    ctx->current = HTTP2_CALLBACK_GOAWAY;
                                    break;
                                }

                                auto const sdata = s->second->as<http_v2_stream_t>();

                                sdata->write_window += ctx->n1;
                                if (sdata->write_window == ctx->n1
                                    && (sdata->flags & ev::WRITE)
                                    && (sdata->ev_callback)) {
                                    sdata->ev_callback->operator()(s->second, ev::WRITE, nullptr, 0, nullptr);
                                    }
                            }
                            else {
                                ctx->write_window += static_cast<int>(ctx->n1);
                                if (ctx->write_window == ctx->n1) {
                                    for (const auto &s : *ctx->streams) {
                                        auto const sdata = s.second->as<http_v2_stream_t>();
                                        if ((sdata->flags & ev::WRITE)
                                            && (sdata->ev_callback)) {
                                            sdata->ev_callback->operator()(s.second, ev::WRITE, nullptr, 0, nullptr);
                                            }
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

                            if (ctx->n1 >= manapi::net::http::HTTP2_ERROR_NO_ERROR
                                && ctx->n1 <= manapi::net::http::HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                                auto s = ctx->streams->find(ctx->frame_stream_id);
                                if (s == ctx->streams->end()) {

                                }
                                else {
                                    auto const sdata = s->second->as<http_v2_stream_t>();
                                    sdata->flags |= http::HTTP2_STREAM_CLOSED|http::HTTP2_STREAM_REMOVED;

                                    if (sdata->ev_callback) {
                                        sdata->ev_callback->operator()(s->second, ev::DISCONNECT, nullptr, 0, nullptr);
                                    }

                                    s->second->cancellation.cancel();
                                }
                            }

                            ctx->n1 = 0;
                            break;
                        }
                    }
                    break;
                }
                case HTTP2_CALLBACK_PARSE_PING_DATA : {
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
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "setting is invalid";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                            if (ctx->frame_length) {
                                ctx->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_NEW_FRAME;

                                /* send ack frame */
                                if (http_v2_send_frame(ctx, HTTP2_FRAME_SETTINGS, HTTP2_FLAG_SETTINGS_ACK, 0, nullptr, 0)) {
                                    return EHTTP_V2_PROTOCOL_ERROR;
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
                     * n2 - padding size
                     */
                    auto datasize = ctx->frame_length - static_cast<ssize_t>(ctx->n2);

                    const auto cutsize = std::min(static_cast<ssize_t>(size - pos), datasize);
                    ctx->frame_buffer.append(buffer + pos, cutsize);
                    ctx->frame_length -= cutsize;
                    pos += cutsize;

                    if (!ctx->frame_length) {
                        auto s = ctx->streams->end();
                        http_v2_stream_t *sdata = nullptr;

                        if (ctx->frame_type == HTTP2_FRAME_HEADERS) {
                            s = ctx->streams->find(ctx->frame_stream_id);
                            if (s == ctx->streams->end()) {

                                auto sconn = std::make_shared<worker::connection> (new http_v2_stream_t{
                                    0,
                                    ctx->frame_stream_id,
                                    0,
                                    ctx,
                                    ctx->client->initial_window_size,
                                    ctx->server->initial_window_size,
                                    nullptr,
                                    0,
                                    nullptr,
                                    nullptr}, +[] (void *s)
                                        -> void {
                                        auto const p = static_cast<http::http_v2_stream_t *> (s);
                                        delete p;
                                    });

                                s = ctx->streams->insert({ctx->frame_stream_id, std::move(sconn)}).first;
                                ctx->concurrent_streams_size++;
                                sdata = s->second->as<http_v2_stream_t>();
                                sdata->req = std::make_unique<request_data_t>();
                                sdata->recv = std::make_unique<worker::base::connection_io_part>();
                                sdata->speed_min_delay = config->speed_check_delay;
                            }
                            else {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "CONTINUATION frame instead of HEADERS frame";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                        }

                        bool const flg = ctx->frame_flag & HTTP2_FLAG_HEADERS_END_HEADERS;

                        if (flg) {

                            if (!ctx->decoder->decode(ctx->frame_buffer)) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_INTERNAL_ERROR;
                                http_goaway.err_msg = "hpack: failed to decode headers";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }

                            ctx->frame_buffer.clear();

                            if (s == ctx->streams->end()) {
                                s = ctx->streams->find(ctx->frame_stream_id);

                                if (s == ctx->streams->end()) {
                                    http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                    http_goaway.err_msg = "stream doesn't exists";
                                    ctx->current = HTTP2_CALLBACK_GOAWAY;
                                    break;
                                }

                                sdata = s->second->as<http_v2_stream_t>();
                            }

                            auto headers = ctx->decoder->headers();
                            while (!headers.empty()) {
                                auto it = headers.begin();
                                auto value = std::move(it->second);
                                auto node = headers.extract(it);
                                auto &key = node.key();
                                for (auto &c : key) {
                                    c = static_cast<char>(std::tolower(c));
                                }
                                sdata->req->headers.insert({std::move(key), std::move(value)});
                            }

                            sdata->req->http = http::versions::HTTP_v2;
                            auto hv = sdata->req->headers.extract(":method");
                            if (hv.empty()) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = ":method is missing";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }
                            sdata->req->method = std::move(hv.mapped());

                            hv = sdata->req->headers.extract(":path");
                            if (hv.empty()) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = ":path is missing";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                break;
                            }
                            sdata->req->uri = std::move(hv.mapped());

                            auto hit = sdata->req->headers.find(http::HEADER.CONTENT_LENGTH);
                            if (hit == sdata->req->headers.end()) {
                                sdata->req->body_size = -1;
                            }
                            else {
                                try {
                                    sdata->req->body_size = std::stoll(hit->second);
                                }
                                catch (...) {
                                    sdata->req->body_size = -1;
                                }

                                if (sdata->req->body_size < 0) {
                                    http_goaway.err_code = manapi::net::http::HTTP2_ERROR_REFUSED_STREAM;
                                    http_goaway.err_msg = "invalid content length";
                                    ctx->current = HTTP2_CALLBACK_GOAWAY;
                                    break;
                                }
                            }


                            url_decode_stream url_decoder;

                            if (auto rhs = url_decoder << sdata->req->uri) {
                                return EHTTP_V2_PROTOCOL_ERROR;
                            }

                            sdata->req->path = url_decoder.result();
                            sdata->req->divided = url_decoder.divided();
                        }

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

                            return EHTTP_V2_NEW_STREAM;
                        }
                    }

                    break;
                }
                case HTTP2_CALLBACK_PARSE_BODY_DATA : {
                    auto datasize = ctx->frame_length - ctx->n2;
                    assert((datasize <= ctx->server->max_frame_size));
                    datasize = std::min(static_cast<int>(size - pos), static_cast<int>(datasize));

                    {
                        //MANAPIHTTP_LOG("RECV DATA {}", len);

                        auto s = ctx->streams->find(ctx->frame_stream_id);
                        if (s == ctx->streams->end()) {
                            http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                            http_goaway.err_msg = "stream doesn't exists";
                            ctx->current = HTTP2_CALLBACK_GOAWAY;
                            break;
                        }
                        auto const sdata = s->second->as<http_v2_stream_t>();

                        if (sdata->read_window < datasize || ctx->read_window < datasize) {
                            http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FLOW_CONTROL_ERROR;
                            http_goaway.err_msg = "read buffer overflow";
                            ctx->current = HTTP2_CALLBACK_GOAWAY;
                            break;
                        }

                        sdata->read_window -= static_cast<int> (datasize);
                        ctx->read_window -= static_cast<int> (datasize);


                        if (http_v2_process_window (s->second, sdata)) {
                            return EHTTP_V2_PROTOCOL_ERROR;
                        }

                        sdata->transfered_k += static_cast<int> (datasize);

                        if ((sdata->flags & ev::DISCONNECT)) {
                            http_goaway.err_code = manapi::net::http::HTTP2_ERROR_REFUSED_STREAM;
                            http_goaway.err_msg = "stream was closed";
                            ctx->current = HTTP2_CALLBACK_GOAWAY;
                            break;
                        }
                        else {
                            /**
                             * recv data
                             */
                            if (http_v2_flush_recv (s->second, sdata)) {
                                return EHTTP_V2_PROTOCOL_ERROR;
                            }

                            if (datasize) {
                                if ((sdata->flags & ev::READ) && sdata->ev_callback) {
                                    sdata->ev_callback->operator()(s->second, ev::READ, buffer + pos, datasize, nullptr);
                                }
                                else {
                                    if (datasize != worker::base::connection_io_send(sdata->recv.get(), buffer + pos,
                                        datasize, sdata->ctx->worker->bufferpool().get(), static_cast<int>(config->buffer_size), &sdata->recv_size, maxcnt)) {
                                        return EHTTP_V2_PROTOCOL_ERROR;
                                        }

                                    // if (sdata->recv_size >= config->max_buffer_stack) {
                                    //     ctx->worker->event_toggle(ctx->conn, false, ev::READ);
                                    // }
                                }
                            }

                            // if (sdata->recv_size < config->max_buffer_stack) {
                            //     ctx->worker->event_toggle(ctx->conn, true, ev::READ);
                            // }
                        }
                    }


                    ctx->frame_length -= datasize;
                    pos += datasize;

                    if (!ctx->frame_length) {
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
                        case HTTP2_FRAME_HEADERS: {
                            if (ctx->frame_length == 0) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                http_goaway.err_msg = "HEADER frame is empty";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }

                            if (ctx->frame_stream_id == 0) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                http_goaway.err_msg = "0 is reserved";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }

                            if (ctx->frame_stream_id <= ctx->last_stream_id || (ctx->frame_stream_id % 2 == 0)) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "unexpected stream id";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }


                            if (ctx->concurrent_streams_size >= ctx->server->max_concurret_streams) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_REFUSED_STREAM;
                                http_goaway.err_msg = std::format("max concurrent streams-{}", ctx->server->max_concurret_streams);
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }

                            ctx->last_stream_id = ctx->frame_stream_id;

                            if (ctx->frame_flag & HTTP2_FLAG_HEADERS_PADDED) {
                                ctx->n1 = 1; /* number size */
                                ctx->n2 = 0; /* return value */
                                ctx->current = HTTP2_CALLBACK_PARSE_NUMBER;
                                ctx->next = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                            }
                            else {
                                ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                            }

                            break;
                        }
                        case HTTP2_FRAME_CONTINUATION:
                            if (ctx->frame_stream_id == 0) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                http_goaway.err_msg = "0 is reserved";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }

                            if (ctx->frame_stream_id != ctx->last_stream_id) {
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_PROTOCOL_ERROR;
                                http_goaway.err_msg = "provided stream id isn't handled";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }

                            ctx->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
                        break;
                        case HTTP2_FRAME_SETTINGS:
                            // setting param size - 6 bytes
                                if (ctx->frame_length % 6 != 0) {
                                    http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                    http_goaway.err_msg = "invalid length in frame SETTINGS";
                                    ctx->current = HTTP2_CALLBACK_GOAWAY;
                                    goto finish;
                                }

                        if (ctx->frame_length == 0) {
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
                            ctx->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
                        }
                        break;
                        case HTTP2_FRAME_GOAWAY:
                            ctx->current = HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID;
                        break;
                        case HTTP2_FRAME_WINDOW_UPDATE:
                            if (ctx->frame_length != 4) {
                                /**
                                 * RFC9113 (6.9) WINDOW_UPDATE
                                 *
                                 * A WINDOW_UPDATE frame with a length other
                                 * than 4 octets MUST be treated as
                                 * a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
                                 */

                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                http_goaway.err_msg = "invalid WINDOW_UPDATE";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }
                        ctx->current = HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE;
                        break;
                        case HTTP2_FRAME_RST_STREAM:
                            ctx->current = HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION;
                        break;
                        case HTTP2_FRAME_PING:
                            if (ctx->frame_length != 8) {
                                /**
                                 * RFC9113 (6.7) PING
                                 *
                                 * Receipt of a PING frame with a length field value other than
                                 * 8 MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
                                 */
                                http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                                http_goaway.err_msg = "invalid PING";
                                ctx->current = HTTP2_CALLBACK_GOAWAY;
                                goto finish;
                            }
                        ctx->current = HTTP2_CALLBACK_PARSE_PING_DATA;
                        break;
                        case HTTP2_FRAME_DATA: {
                            if (ctx->frame_flag & HTTP2_FLAG_DATA_PADDED) {
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
                        default: {
                            /* undefined frame */
                            ctx->n1 = ctx->frame_length;
                            ctx->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
                            ctx->next = HTTP2_CALLBACK_PARSE_NEW_FRAME;
                        }
                    }

                    if (ctx->frame_length > ctx->server->max_frame_size) {
                        http_goaway.err_code = manapi::net::http::HTTP2_ERROR_FRAME_SIZE_ERROR;
                        http_goaway.err_msg = "frame length is invalid";
                        ctx->current = HTTP2_CALLBACK_GOAWAY;
                        goto finish;
                    }

                    finish: break;
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

                    if (http_v2_send_goaway (ctx, &http_goaway)) {
                        return EHTTP_V2_PROTOCOL_ERROR;
                    }

                    return EHTTP_V2_PROTOCOL_OK;
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
        return EHTTP_V2_PROTOCOL_ERROR;
    }

    return EHTTP_V2_PROTOCOL_WANT_READ;
}

ssize_t manapi::net::http::http_v2_write(http_v2_stream_t *s, const void *buffer, ssize_t size, bool finish) {
    if (s->flags & ev::DISCONNECT) {
        return -1;
    }

    if (!s->ctx->worker->is_writable(s->ctx->conn)) {
        return 0;
    }

    auto copy = std::min(
        static_cast<ssize_t>(std::min(s->write_window, s->ctx->write_window)), size);

    assert((copy >= 0));

    if (!copy) {
        return 0;
    }

    s->write_window -= static_cast<int>(copy);
    s->ctx->write_window -= static_cast<int>(copy);

    if (finish) {
        finish = size == copy;
    }

    ev::buff_t buff = {.base = (char*)buffer, .len = static_cast<size_t>(copy)};

    auto rend = buff.base + copy;
    while (rend != buff.base) {
        buff.len = std::min(static_cast<size_t>(copy),
            static_cast<size_t>(s->ctx->client->max_frame_size));

        if (http_v2_send_data_frame(s->ctx, s->id, &buff, 1, finish && (buff.base + buff.len) == rend)) {
            return -1;
        }

        buff.base += buff.len;
        copy -= static_cast<int>(buff.len);

        if (!s->ctx->worker->is_writable(s->ctx->conn)) {
            break;
        }
    }

    const auto rhs = reinterpret_cast<std::ptrdiff_t>(buff.base) - reinterpret_cast<std::ptrdiff_t>(buffer);
    s->transfered_k += static_cast<int>(rhs);

    if (finish
        && (rhs == copy)
        && !(s->flags & HTTP2_STREAM_SEND_END)) {
        /* yay */
        s->flags |= HTTP2_STREAM_SEND_END;
        s->ctx->concurrent_streams_size--;
    }

    return rhs;
}

int manapi::net::http::http_v2_rst_stream(http_v2_stream_t *s, int errcode) {
    return http_v2_rst_stream_ex (s->ctx, s->id, errcode);
}

manapi::future<ssize_t> manapi::net::http::http_v2_response(worker::base *worker, const worker::shared_conn &connection, http_v2_stream_t *s, int status, std::map<std::string, std::string> headers, bool finish) {
    if (s->flags & ev::DISCONNECT) {
        co_return -1;
    }

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
        ev::buff_t const buf = {.base = (char*)data.data() + cnt, .len = static_cast<std::size_t>(left)};
        if (http_v2_send_frame(s->ctx, ft, cflag, s->id, &buf, 1)) {
            co_return -1;
        }
        cflag = 0;
        cnt += left;
    }
    co_return 1;
}
