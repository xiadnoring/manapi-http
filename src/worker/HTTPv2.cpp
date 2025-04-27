#include "worker/HTTPv2.hpp"

#include <future>
#include <http/HTTPv2.hpp>

#include "ManapiString.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "http/HTTPv1_1.hpp"
#include "worker/OpenSSL_TLS.hpp"

#define HEADER_DEFAULT_SIZE 9

namespace manapi::net::worker {
    enum http_v2_callback_type {
        HTTP2_CALLBACK_NEXT_LINE = 1,
        HTTP2_CALLBACK_SKIP_MSG = 2,
        HTTP2_CALLBACK_PARSE_HEADER_LENGTH = 3,
        HTTP2_CALLBACK_PARSE_HEADER_TYPE = 4,
        HTTP2_CALLBACK_SKIP_NULL_OCTECTS = 5,
        HTTP2_CALLBACK_PARSE_HEADER_OCTECTS = 6,
        HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID = 7,
        HTTP2_CALLBACK_PARSE_HEADER_FLAG = 8,
        HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID = 9,
        HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE = 10,
        HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA = 11,
        HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE = 12,
        HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION = 13,
        HTTP2_CALLBACK_PARSE_PING_DATA = 14,
        HTTP2_CALLBACK_PARSE_SETTING_ID = 15,
        HTTP2_CALLBACK_PARSE_SETTING_VALUE = 16,
        HTTP2_CALLBACK_PARSE_HEADER_DATA = 17,
        HTTP2_CALLBACK_PARSE_BODY_DATA = 18,
        HTTP2_CALLBACK_PARSE_SKIP_N_BYTES = 19,
        HTTP2_CALLBACK_PARSE_FIELD_BLOCK = 20,
        HTTP2_CALLBACK_PARSE_NUMBER = 21

    };
}

struct manapi_http_2_connection_t {
    manapi::net::worker::http_v2::http_v2_thread_data_t *original;
    int flags;
    ssize_t write_cursor;
    ssize_t read_cursor;
    manapi::object_item_pool<manapi::bytebuffer, std::size_t> write_buffer;
    manapi::object_item_pool<manapi::bytebuffer, std::size_t> read_buffer;
};

std::map <int, manapi::json_mask> manapi::net::worker::http_v2::allow_settings {
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

manapi::net::worker::http_v2::http_v2(const std::shared_ptr<manapi::net::worker::TCP> &worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : base(site), worker(worker) {
    this->config = std::move(config);
    this->buffer = {};
    this->current = 0;
    this->next = 0;
    this->init_settings();
    this->init_callbacks();
    this->write_buffer_current = 0;
    this->write_buffer_cursor = 0;
    this->write_buffer_last = nullptr;
    this->read = [this](net::worker::connection & PH1, void * && PH2, ssize_t PH3)
        -> future<ssize_t> { return this->default_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3)); };
    this->write = [this](net::worker::connection & PH1, const void * && PH2, ssize_t PH3, bool && PH4)
        -> future<ssize_t> { return this->default_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3), std::forward<decltype(PH4)>(PH4)); };
}

manapi::net::worker::http_v2::~http_v2() {
    MANAPIHTTP_LOG2 (this->site.async_context(), "http2 destroyed");
}


void manapi::net::worker::http_v2::set_watcher_event(int revents) {
    if (!(this->watcher->events() & revents) && this->watcher->is_active()) {
        revents = this->watcher->events() | revents;
        this->watcher->restart(revents);
    }
}

void manapi::net::worker::http_v2::remove_watcher_event(int revents) {
    if ((this->watcher->events() & revents) && this->watcher->is_active()) {
        this->watcher->restart(this->watcher->events() ^ revents);
    }
}

manapi::future<void> manapi::net::worker::http_v2::parse_request(ssize_t j, ssize_t size) {
    this->protocol.timeout = std::max(this->protocol.timeout, this->config->speed_check_delay() * 5);

    this->write_buffer = nullptr;
    this->write_buffer_cursor = 0;
    this->write_buffer_last = nullptr;
    this->write_buffer_current = 0;
    this->write_buffer_size = 0;

    this->protocol.window.stream_window = this->buffer_size();
    this->protocol.window.conn_window = 200000;

    this->protocol.window.read = 65535;
    this->protocol.window.write = 65535;

    this->buffer->resize(this->buffer_size());

    this->current = HTTP2_CALLBACK_NEXT_LINE;
    this->next = HTTP2_CALLBACK_SKIP_MSG;

    co_await async::promise<void, std::false_type> (this->site.async_context(), [&] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject)
        -> void {
        this->parse_vars.size = size;
        this->parse_vars.j = j;

        this->http2_resolve_ = std::move(resolve);
        this->protocol.conn_type |= (CONN_IDLE);

        async::run(this->site.async_context(), this->site.async_context()->eventloop()->custom_callback([this] (event_loop *ev)
            -> void {
            this->ping_interval = this->site.async_context()->timerpool()->append_interval_sync(this->config->speed_check_delay(), [this, dep = this->new_dependency()] (manapi::timer t)
            -> void { this->timer_watcher(dep); });

            this->io_call_watcher = this->site.async_context()->eventloop()->create_watcher_async([this] (std::shared_ptr<ev::async> &w)
                -> void { io_call_callback(w); });

            this->watcher = this->worker->sync_watch_io (this->connection.get(), ev::READ, [this] (std::shared_ptr<ev::io> &w, int status, int revents)
                -> void {
                try {
                    if (revents & ev::WRITE) {
                        flush_write_buffer();
                    }

                    if (revents & ev::READ) {
                        while (true) {
                            auto rhs = this->worker->sync_read(this->connection.get(), this->buffer->data(), static_cast<ssize_t>(this->buffer->size()));
                            if (rhs < 0) {
                                if (rhs == IO_WANT_AGAIN) {
                                    /* not again */
                                    return;
                                }

                                if (rhs == IO_WANT_READ) {
                                    this->set_watcher_event(ev::READ);
                                    return;
                                }

                                if (rhs == IO_WANT_WRITE) {
                                    this->set_watcher_event(ev::WRITE);
                                    return;
                                }

                                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "read failed");
                            }

                            if (!rhs) {
                                break;
                            }

                            this->parse_vars.size = rhs;
                            this->parse_vars.j = 0;

                            handle_callback_watcher ();

                            if (!w->data()) { return; }
                        }
                    }
                }
                catch (...) {
                    if (!w->data()) { return; }
                }

                if (this->watcher) {
                    if (this->write_buffer_last) {
                        this->set_watcher_event(ev::WRITE);
                    }
                    else {
                        this->remove_watcher_event(ev::WRITE);
                    }
                }
            });

            this->send_settings ({
                {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
                {HTTP2_SETTING_ENABLE_PUSH, 0},
                {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, 100},
                {HTTP2_SETTING_MAX_HEADER_LIST_SIZE,  65000},
                {HTTP2_SETTING_INITIAL_WINDOW_SIZE, std::max(this->protocol.window.stream_window, this->protocol.server_settings.initial_window_size)}
            });

            if (this->protocol.window.read < this->protocol.window.conn_window) {
                auto size = this->protocol.window.conn_window - this->protocol.window.read;
                this->protocol.window.read += size;
                this->send_window_frame(0, size);
            }

            this->handle_callback_watcher ();
        }));
    });

    // goto skip;
    // while ((this->protocol.conn_type & (CONN_CLOSED | CONN_HALF_CLOSED)) == false) {
    //
    //     rhs = co_await this->worker->read (*this->connection, this->buffer->data(), static_cast<ssize_t>(this->buffer->size()));
    //
    //     if (rhs <= 0) {
    //         //std::cout << "HALF CLOSED BY READ\n";
    //         this->protocol.conn_type.fetch_or(CONN_HALF_CLOSED);
    //         break;
    //     }
    //
    // }


    std::cerr<<"http request has been completed\n";
}

void manapi::net::worker::http_v2::handle_callback_watcher() {
    try {
        while (1) {
            {
                auto ptr = this->buffer->data();
                for (; this->parse_vars.j < this->parse_vars.size && this->protocol.length > 0; this->parse_vars.j++, this->protocol.length--) {
                    exec_callback(ptr[this->parse_vars.j]);
                }
            }

            assert((this->protocol.length >= 0 && "length is negative"));
            if (!this->protocol.length) {
                /* clean up */
                auto buffer = std::move(this->parse_vars.buffer);

                if (this->protocol.type == HTTP2_FRAME_SETTINGS) {
                    if (this->protocol.flag & HTTP2_FLAG_SETTINGS_ACK) {
                        // settings were accepted
                        if (false == (this->protocol.conn_type & CONN_CLOSED)) {
                            if (this->protocol.setting_timeout.empty()) {
                                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "unexpected frame");
                            }
                            this->protocol.setting_timeout.front().sync_stop(this->site.async_context());
                            this->protocol.setting_timeout.pop();
                        }
                        if (this->protocol.conn_type & CONN_IDLE) {
                            this->protocol.conn_type ^= (CONN_IDLE);
                        }
                    }
                }
                else if (this->protocol.type == HTTP2_FRAME_PING) {
                    if (this->protocol.flag & HTTP2_FLAG_PING_ACK) {
                        auto it = this->protocol.pings.find(buffer);
                        // The endpoint MUST NOT respond to PING frames with ACK if not exists in protocol.pings
                        if (it != this->protocol.pings.end()) {
                            this->protocol.pings.erase(it);
                        }
                    }
                    else {
                        if (!this->send_ping_frame(std::move(buffer))) {
                            return;
                        }
                    }
                    buffer.clear();
                }
                else {
                    // if (this->protocol.type != HTTP2_FRAME_WINDOW_UPDATE && !this->protocol.setting_timeout.empty()) {
                    //     // We expected to get only the SETTINGS frame
                    //     co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "SETTINGS frame was expected");
                    // }

                    // if (this->protocol.initial_frame) {
                    //     co_await send_settings ({
                    //         {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
                    //         {HTTP2_SETTING_ENABLE_PUSH, 0},
                    //         {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, 100},
                    //         {HTTP2_SETTING_MAX_HEADER_LIST_SIZE,  65000}
                    //     });
                    //
                    //     this->protocol.window.read = 3000000;
                    //     co_await send_window_frame(0, static_cast<int> (this->protocol.window.read));
                    //
                    //     this->protocol.initial_frame = false;
                    // }
                    switch (this->protocol.type) {
                        case HTTP2_FRAME_PRIORITY: {
                            auto thread = this->threads.find(this->protocol.stream_id);
                            if (thread == this->threads.end()) {
                                if (this->protocol.last_stream_id < this->protocol.stream_id) {
                                    this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                                }
                                break;
                            }
                            // pass
                            break;
                        }
                        case HTTP2_FRAME_HEADERS:
                        case HTTP2_FRAME_CONTINUATION: {
                            auto session = this->threads.find(this->protocol.stream_id);
                            if (session == this->threads.end()) {
                                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                            }

                            if (this->protocol.type == HTTP2_FRAME_HEADERS) {
                                if ((session->second->type == HTTP2_CONN_HALF_CLOSED_REMOTE || session->second->type == HTTP2_CONN_CLOSED)) {
                                    this->generate_error(HTTP2_ERROR_STREAM_CLOSED, "Invalid frame was received to half-closed(remote)", this->protocol.stream_id);
                                }

                                if (!(this->protocol.flag & HTTP2_FLAG_HEADERS_END_STREAM)) {
                                    session->second->conn_flags |= HTTP2_THREAD_HAS_BODY;
                                }
                            }

                            session->second->type = HTTP2_CONN_HALF_CLOSED_REMOTE;

                            if (this->protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
                                auto id = session->second->id;
                                this->callbacks.headers(session->second->id);

                                if (!this->threads.contains(id)) {
                                    /* already destroyed */
                                    break;
                                }

                                if (session->second->conn_flags & HTTP2_THREAD_HAS_BODY) {
                                    this->callbacks.data (session->second->id);
                                }
                                else {
                                    this->callbacks.finished(session->second->id);
                                    session->second->type = HTTP2_CONN_CLOSED;
                                    //sessions.erase(session);
                                }
                            }

                            break;
                        }
                        case HTTP2_FRAME_DATA: {
                            auto thread = this->threads.find(this->protocol.stream_id);
                            if (thread == this->threads.end()) {
                                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                            }

                            if (this->protocol.flag & HTTP2_FLAG_DATA_END_STREAM) {
                                thread->second->conn_flags |= (HTTP2_THREAD_RECV_EOS);
                                thread->second->type = HTTP2_CONN_CLOSED;
                                this->callbacks.finished (thread->second->id);
                            }

                            this->flush_io_stream(thread);

                            break;
                        }
                        case HTTP2_FRAME_GOAWAY: {
                            auto error = std::move(this->protocol.error.value());
                            this->protocol.error.reset();

                            this->callbacks.goaway(error.last_stream_id, error.errnum, std::move(error.errmsg));
                            if (error.errnum >= HTTP2_ERROR_NO_ERROR && error.errnum <= HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                                error.last_stream_id = 0;
                                this->protocol.conn_type |= CONN_HALF_CLOSED;
                                this->generate_error(HTTP2_ERROR_NO_ERROR, std::format("received {} {}", error.errnum, error.errmsg), error.last_stream_id);
                            }
                            else {
                                // GOAWAY frame with unknown error code
                                // The endpoint MUST NOT trigger any special behavior.
                                this->protocol.error.reset();
                            }
                            break;
                        }
                        case HTTP2_FRAME_RST_STREAM: {
                            // protocol.value - err code
                            const int &err_code = this->protocol.value;

                            if (err_code >= HTTP2_ERROR_NO_ERROR && err_code <= HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                                auto thread = this->threads.find(this->protocol.stream_id);
                                if (thread == this->threads.end()) {
                                    if (this->protocol.last_stream_id < this->protocol.stream_id) {
                                        this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("stream {} doesn't exists", this->protocol.stream_id));
                                    }
                                    /* already */
                                    break;
                                }

                                if ((this->protocol.rst_cnt += 1) >= this->config->max_rst_cnt()) {
                                    this->generate_error(HTTP2_ERROR_ENHANCE_YOUR_CALM, "enchance your calm", this->protocol.stream_id);
                                }

                                this->callbacks.rst_stream (thread, err_code);
                            }

                            this->protocol.value = 0;
                            break;
                        }
                        case HTTP2_FRAME_WINDOW_UPDATE: {
                            //MANAPIHTTP_LOG ("window frame пришел ура праздник 🎉🎉🎉 {} {} {}", protocol.value, protocol.stream_id, protocol.flag);
                            if (this->protocol.stream_id == 0) {
                                // global
                                this->protocol.window.write += (this->protocol.value);
                                this->protocol.value = 0;

                                // flush streams
                                this->flush_io_streams();
                                break;
                            }

                            auto thread = this->threads.find(this->protocol.stream_id);
                            if (thread == this->threads.end()) {
                                if (this->protocol.last_stream_id < this->protocol.stream_id) {
                                    this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                                }
                                /* not exists */
                                break;
                            }
                            thread->second->write_window += this->protocol.value;
                            this->protocol.value = 0;

                            // flush
                            this->flush_io_stream(thread);

                            break;
                        }
                        default: {
                            //MANAPIHTTP_LOG("frame type: {}", protocol.type);
                            //protocol.closed = true;
                        }
                    }
                }

                if ((this->protocol.conn_type & (CONN_CLOSED)) == false) {
                    this->current = HTTP2_CALLBACK_PARSE_HEADER_LENGTH;
                    //goto skip;
                    this->protocol.length = HEADER_DEFAULT_SIZE;

                    if (this->parse_vars.j < this->parse_vars.size) {
                        continue;
                    }
                }
            }

            break;
        }

        return;
    }
    catch (manapi::exception const &e) {
        MANAPIHTTP_LOG(this->site.async_context(), "[{}]: HTTP2 Exception: {}", static_cast<size_t>(e.err_num()), e.what());
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG(this->site.async_context(), "HTTP2 Exception: {}", e.what());
    }

    if ((this->protocol.conn_type & (CONN_CLOSED)) == false) {
        reset_all_streams();
    }
    else {
        this->protocol.window.write = std::numeric_limits<ssize_t>::max();
        unlimit_all_streams();
    }

    this->close_http2_connection (HTTP2_ERROR_NO_ERROR, "shutdown");
}

void manapi::net::worker::http_v2::update_setting(http2_setting_type type, int value, bool self) {
    auto settings = self ? &this->protocol.server_settings : &this->protocol.client_settings;
    this->setting_value_valid(type, value);
    switch (type) {
        case HTTP2_SETTING_HEADER_TABLE_SIZE:
            settings->header_table_size = value;
            if (self) {
                this->protocol.encoder.max_table_size(value);
            }
            else {
                this->protocol.decoder.m_dynamic_max(value);
            }
        break;
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
            break;
    }

    this->setting_param_was_ack(self);
}

void manapi::net::worker::http_v2::init_settings() {
    this->protocol.server_settings.header_table_size = 4096;
    this->protocol.server_settings.enable_push = 1;
    this->protocol.server_settings.max_concurret_streams = std::numeric_limits<int>::max();
    this->protocol.server_settings.initial_window_size = 65535;
    this->protocol.server_settings.max_frame_size = 16384;
    this->protocol.server_settings.max_header_list_size = std::numeric_limits<int>::max();
    this->protocol.server_settings.settings_no_rfc7540_priorities = 0;
    this->protocol.server_settings.settings_enable_metadata = 0;
    this->protocol.server_settings.settings_enable_connect_protocol = 0;
    this->protocol.server_settings.tls_reneg_permitted = 0;

    this->protocol.client_settings = this->protocol.server_settings;
}

void manapi::net::worker::http_v2::init_callbacks() {
    this->set_callbacks({
        .headers = [this](int PH1) -> void {
            this->default_ev_headers(PH1);
        },
        .data = [this](int PH1) {
            this->default_ev_data(std::forward<decltype(PH1)>(PH1));
        },
        .goaway = [this](int PH1, int && PH2, std::string PH3) {
            this->default_ev_goaway(PH1, PH2, std::move(PH3));
        },
        .priority_update = [this](int PH1, int PH2, std::string PH3) {
            this->default_ev_priopity_update(PH1, PH2, std::move(PH3));
        },
        .rst_stream = [this](std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator &&PH1, int PH2) {
            this->default_ev_rst_stream(std::forward<decltype(PH1)>(PH1), PH2);
        },
        .finished = [this](int PH1) {
            this->default_ev_finished(PH1);
        }
    });
}

void manapi::net::worker::http_v2::set_callbacks(const http_v2_callbacks_t &callbacks) {
    this->callbacks = callbacks;
}

ssize_t manapi::net::worker::http_v2::sync_read(worker::connection *conn, void *buff, ssize_t size) {
    /* TODO: sync http2 read */
    perror("http2 sync_read error");
    return -1;
}

ssize_t manapi::net::worker::http_v2::sync_write(worker::connection *conn, const void *buff, ssize_t size) {
    /* TODO: sync http2 write */
    perror("http2 sync_write error");
    return -1;
}

void manapi::net::worker::http_v2::stop() {}

manapi::future<bool> manapi::net::worker::http_v2::configure_connection(std::shared_ptr<worker::connection> conn) {
    co_return true;
}

void manapi::net::worker::http_v2::connection_close(std::shared_ptr<worker::connection> conn, bool clean_disconnect) {
    /* nothing to do */
}

void manapi::net::worker::http_v2::init() {

}

bool manapi::net::worker::http_v2::is_valid_connection(worker::connection &connection) {
    /* verify connection */
    return true;
}

void manapi::net::worker::http_v2::onrecv(std::shared_ptr<ev::io> &watcher, int status, int revents) {
    /* nothing to do */
    return;
}


manapi::future<ssize_t> manapi::net::worker::http_v2::response(worker::connection &connection, http::response &resp, bool finish) {
    auto &conn = connection.as<manapi_http_2_connection_t>();

    co_await conn.original->mx.lock();
    conn.original->headers = std::move(resp.headers());
    conn.original->headers[":status"] = std::to_string(resp.status_code());
    if (finish) { conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_SEND_EOS); }
    conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_HEADERS);
    this->io_call_watcher->send();

    /* it's okay */
    co_return 1;
}

ssize_t manapi::net::worker::http_v2::buffer_size() {
    return this->config->buffer_size().load();
}

void manapi::net::worker::http_v2::empty_setting_timeouts() {
    // auto lk = co_await this->protocol.mx.lock_guard(); must be called before
    while (!this->protocol.setting_timeout.empty()) {
        auto timertask = std::move(this->protocol.setting_timeout.back());
        this->protocol.setting_timeout.pop();
        timertask.sync_stop(this->site.async_context());
    }
}

void manapi::net::worker::http_v2::generate_error(http2_error_type errnum, std::string errmsg, int last_stream_id) noexcept(false) {
    if (false == (this->protocol.conn_type & CONN_CLOSED)) {
        this->close_http2_connection(errnum, errmsg);
    }

    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, errmsg);
}

void manapi::net::worker::http_v2::_skip_sm_msg(char &c) {
    this->parse_vars.buffer += c;
    if (this->parse_vars.buffer.size() == 4) {
        if (this->parse_vars.buffer != "SM\r\n") {
            this->generate_error (HTTP2_ERROR_PROTOCOL_ERROR, "SM label is invalid");
            return;
        }
        this->parse_vars.buffer.clear();
        this->next = HTTP2_CALLBACK_PARSE_HEADER_OCTECTS;
        this->current = HTTP2_CALLBACK_NEXT_LINE;
    }
}

void manapi::net::worker::http_v2::_next_line(char &c) {
    
    if (this->parse_vars.next_line_state) {
        this->parse_vars.next_line_state = false;
        if (c == '\n') {
            this->current = this->next;
            return;
        }
    }
    else {
        if (c == '\r') {
            this->parse_vars.next_line_state = true;
            return;
        }
    }

    this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid next line: \\r\\n");
}

void manapi::net::worker::http_v2::_skip_null_octet(char &c) {
    if (c == '\0') {
        this->current = this->next;
        return;
    }
    this->current = this->next;
    this->exec_callback(c);
    MANAPIHTTP_LOG2(this->site.async_context(), "Invalid symbol");
}

void manapi::net::worker::http_v2::_parse_header_octets(char &c) {
    
    this->parse_vars.i = 0;
    this->current = HTTP2_CALLBACK_PARSE_HEADER_LENGTH;
    this->exec_callback(c);
}

void manapi::net::worker::http_v2::_parse_header_length(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    // 0xXXXXXX
    if (this->parse_vars.i == 3) {
        this->protocol.length += this->parse_vars.buffint;
        this->parse_vars.buffint = 0;
        this->current = HTTP2_CALLBACK_PARSE_HEADER_TYPE;
    }
}

void manapi::net::worker::http_v2::_parse_header_type(char &c) {
    
    this->parse_vars.i++;
    this->protocol.type = static_cast<int>(c);
    this->current = HTTP2_CALLBACK_PARSE_HEADER_FLAG;
}

void manapi::net::worker::http_v2::_parse_header_stream_id(char &c) {
    
    this->parse_vars.i ++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    if (this->parse_vars.i == 9) {
        // STREAM ID

        // Reserved (1),
        // Stream Identifier (31)

        this->protocol.stream_id = static_cast<int>(this->parse_vars.buffint) & 0x7FFFFFFF; // 31
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        if (this->protocol.flag & HTTP2_FLAG_HEADERS_PRIORITY) {
            //deprecated
            this->protocol.padding = 5; // skip 5 bytes

            this->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
            this->next = HTTP2_CALLBACK_PARSE_FIELD_BLOCK;
        }
        else {
            this->_parse_field_block (c);
        }
    }
}

void manapi::net::worker::http_v2::_parse_goaway_last_stream_id(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.error.value().last_stream_id = static_cast<int>(this->parse_vars.buffint) & 0x7FFFFFFF;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE;
    }
}

void manapi::net::worker::http_v2::_parse_goaway_error_code(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.error.value().errnum = static_cast<int>(this->parse_vars.buffint);
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA;
    }
}

void manapi::net::worker::http_v2::_parse_goaway_additional_debug_data(char &c) {
    this->protocol.error.value().errmsg += c;
}

void manapi::net::worker::http_v2::_parse_window_update_value(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.value = static_cast<int>(this->parse_vars.buffint);

        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = 0;
    }
}

void manapi::net::worker::http_v2::_parse_rst_stream_action(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.value = static_cast<int>(this->parse_vars.buffint);

        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = 0;
    }
}

void manapi::net::worker::http_v2::_parse_ping_data(char &c) {
    
    this->parse_vars.i++;
    this->parse_vars.buffer += c;
    if (this->parse_vars.i == 8) {
        this->parse_vars.i = 0;
    }
}

void manapi::net::worker::http_v2::_parse_setting_id(char &c) {
    
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));
    this->parse_vars.i ++;

    if (this->parse_vars.i == 2) {
        this->parse_vars.nkey = this->parse_vars.buffint;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        this->current = HTTP2_CALLBACK_PARSE_SETTING_VALUE;
    }
}

void manapi::net::worker::http_v2::_parse_setting_value(char &c) {
    
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint << 8) | static_cast<unsigned char>(c)));
    this->parse_vars.i ++;

    if (this->parse_vars.i == 4) {
        this->update_setting(static_cast<http2_setting_type>(this->parse_vars.nkey), this->parse_vars.buffint, false);

        this->parse_vars.nkey = 0;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        this->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
    }
}

void manapi::net::worker::http_v2::_parse_header_data(char &c) {
    
    // NULL (8)

    // LENGTH (8)
    // HEADER KEY CHAR (8) * LENGTH

    // LENGTH (8)
    // HEADER VALUE CHAR (8) * LENGTH

    // next = std::bind(&http_v2::_parse_header_data_key_len , this, std::placeholders::_1);
    // current = std::bind(&http_v2::_skip_null_octet , this, std::placeholders::_1);
    // current(c);
    auto datasize = this->protocol.length - static_cast<ssize_t>(this->protocol.padding);
    const auto cutsize = std::min(static_cast<ssize_t>(this->parse_vars.size - this->parse_vars.j), datasize);
    this->headerbuffer.append(this->buffer->data() + this->parse_vars.j, cutsize);
    // +1  bcz in loop
    this->protocol.length = protocol.length - cutsize + 1;
    this->parse_vars.j += cutsize - 1;
    datasize = protocol.length - static_cast<ssize_t>(this->protocol.padding);

    if (datasize == 1) {
        if (this->protocol.type == HTTP2_FRAME_HEADERS) {
            auto thread = this->threads.find(this->protocol.stream_id);
            if (thread == this->threads.end()) {
                thread = this->threads.insert({this->protocol.stream_id, std::make_unique<http_v2_thread_data_t>(
                    this->protocol.stream_id,
                    0,
                    0,
                    HTTP2_CONN_IDLE,
                    this->protocol.client_settings.initial_window_size,
                    this->protocol.server_settings.initial_window_size,
                    0,
                    (decltype(http_v2_thread_data_t::headers)){},
                    manapi::object_item_pool<bytebuffer, std::size_t>{},
                    manapi::object_item_pool<bytebuffer, std::size_t>{},
                    nullptr,
                    0,
                    nullptr,
                    0,
                    this->site.async_context()
                )}).first;

                this->resolve_timeout_timer();
                this->protocol.last_stream_id = thread->first;
            }
            else {
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "CONTINUATION frame instead of HEADERS Frame");
            }
            if (thread->second->type == HTTP2_CONN_IDLE) {
                thread->second->id = this->protocol.stream_id;
                thread->second->type = HTTP2_CONN_OPEN;
            }
        }
        if (this->protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
            if (!this->protocol.decoder.decode(this->headerbuffer)) {
                this->headerbuffer = {};
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "hpack: failed to decode headers");
                return;
            }
            this->headerbuffer = {};

            auto thread = this->threads.find(this->protocol.stream_id);
            if (thread == this->threads.end()) {
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
            }

            auto headers = this->protocol.decoder.headers();
            while (!headers.empty()) {
                auto it = headers.begin();
                auto value = std::move(it->second);
                auto node = headers.extract(it);
                thread->second->headers.insert({std::move(node.key()), std::move(value)});
            }
        }

        if (this->protocol.padding > 0) {
            this->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
        }
    }

    else if (datasize < 1) {
        this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
    }
}

void manapi::net::worker::http_v2::_parse_body_data(char &c) {
    
    auto datasize = this->protocol.length - this->protocol.padding;
    //const auto cutsize = std::min(static_cast<ssize_t>(parse_vars.size - parse_vars.j), datasize);
    const auto cutsize = std::min(static_cast<ssize_t>(this->parse_vars.size - this->parse_vars.j), datasize);
    auto buffer = std::string_view(this->buffer->data() + this->parse_vars.j, cutsize);

    {
        auto len = static_cast<ssize_t>(buffer.size());

        //MANAPIHTTP_LOG("RECV DATA {}", len);

        auto thread = this->threads.find(this->protocol.stream_id);
        if (thread == this->threads.end()) {
            this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("stream {} doesn't exists", this->protocol.stream_id));
        }

        if (thread->second->read_window < len || this->protocol.window.read < len) {
            this->generate_error(HTTP2_ERROR_FLOW_CONTROL_ERROR, "read buffer overflow", this->protocol.stream_id);
        }

        thread->second->read_window -= len;
        this->protocol.window.read -= len;

        if (!thread->second->read_storage_buffer) {
            thread->second->read_storage_buffer = std::make_unique<http_v2_write_buffers>(this->site.bufferpool()->get(), nullptr);
            thread->second->read_storage_buffer->buffer->resize(this->buffer_size());
            thread->second->read_storage_size = 0;
            thread->second->read_storage_last = thread->second->read_storage_buffer.get();
        }

        ssize_t rhs = 0;
        while (rhs != len) {
            if (thread->second->read_storage_size == thread->second->read_storage_last->buffer->size()) {
                thread->second->read_storage_last->next = std::make_unique<http_v2_write_buffers>(this->site.bufferpool()->get(), nullptr);
                thread->second->read_storage_last->next->buffer->resize(this->buffer_size());
                thread->second->read_storage_size = 0;
                thread->second->read_storage_last = thread->second->read_storage_last->next.get();
            }

            auto copy = std::min(static_cast<ssize_t>(thread->second->read_storage_last->buffer->size()) - thread->second->read_storage_size,
                len - rhs);

            memcpy (thread->second->read_storage_last->buffer->data() + thread->second->read_storage_size, buffer.data() + rhs, copy);

            rhs += copy;
            thread->second->read_storage_size += copy;
        }
    }

    // +1  bcz in loop
    this->protocol.length = protocol.length - cutsize + 1;
    this->parse_vars.j += cutsize - 1;
    //MANAPIHTTP_LOG("body length: {} cutsize: {} j: {} size: {}", this->protocol.length, cutsize, this->parse_vars.j, this->parse_vars.size);
    datasize = this->protocol.length - static_cast<ssize_t>(this->protocol.padding);
    if (datasize == 1) {
        return;
    }

    if (datasize < 1) {
        this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
        return;
    }
}

void manapi::net::worker::http_v2::_parse_skip_n_bytes(char &c) {
    if (this->protocol.padding <= 1) {
        this->protocol.padding = 0;
        this->current = this->next;
        this->exec_callback (c);
        return;
    }
    this->protocol.padding--;
}

void manapi::net::worker::http_v2::_parse_field_block(char &c) {
    
    // if protocol.length==1, then it means that the protocol data frame is empty
    const auto length = this->protocol.length - 1;
    if (length < 0) {
        this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "The DATA frame has a negative length");
        return;
    }

    switch (this->protocol.type) {
        case HTTP2_FRAME_HEADERS: {
            if (length == 0) {
                this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "HEADERS frame is empty");
                return;
            }

            if (this->protocol.stream_id == 0) {
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "0x0 reserved");
                return;
            }

            if ((!this->threads.empty() && this->threads.rbegin()->first >= this->protocol.stream_id) || (this->protocol.stream_id % 2 == 0)) {
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "unexpected stream id");
                return;
            }

            if (this->threads.size() >= this->protocol.server_settings.max_concurret_streams) {
                this->generate_error(HTTP2_ERROR_REFUSED_STREAM, std::format("max concurrent streams-{}", this->protocol.server_settings.max_concurret_streams));
                return;
            }

            if (this->protocol.flag & HTTP2_FLAG_HEADERS_PADDED) {
                this->parse_vars.i = 1;
                this->protocol.padding = 0;
                this->parse_vars.tmp = &this->protocol.padding;
                this->current = HTTP2_CALLBACK_PARSE_NUMBER;
                this->next = HTTP2_CALLBACK_PARSE_HEADER_DATA;
            }
            else {
                this->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
            }
            break;
        }
        case HTTP2_FRAME_CONTINUATION:
            if (this->protocol.stream_id == 0) {
                this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "0x0 reserved");
                return;
            }

            this->current = HTTP2_CALLBACK_PARSE_HEADER_DATA;
        break;
        case HTTP2_FRAME_SETTINGS:
            // setting param size - 6 bytes
            if (length % 6 != 0) {
                this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "invalid len");
                return;
            }
            if (length > 0) {
                if (this->protocol.setting_param_acks > 0) {
                    this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "processing past settings so far");
                    return;
                }
                this->protocol.setting_param_acks = (length / 6);
            }

            this->current = HTTP2_CALLBACK_PARSE_SETTING_ID;
        break;
        case HTTP2_FRAME_GOAWAY:
            this->protocol.error = protocol_http2_error_t{0,0,{}};
            this->current = HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID;
        break;
        case HTTP2_FRAME_WINDOW_UPDATE:
            this->current = HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE;
        break;
        case HTTP2_FRAME_RST_STREAM:
            this->current = HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION;
        break;
        case HTTP2_FRAME_PING:
            if (length != 8) {
                this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "frame ping: invalid payload length");
                return;
            }
            this->current = HTTP2_CALLBACK_PARSE_PING_DATA;
        break;
        case HTTP2_FRAME_DATA: {
            if (this->protocol.flag & HTTP2_FLAG_DATA_PADDED) {
                this->parse_vars.i = 1;
                this->protocol.padding = 0;
                this->parse_vars.tmp = &this->protocol.padding;
                this->current = HTTP2_CALLBACK_PARSE_NUMBER;
                this->next = HTTP2_CALLBACK_PARSE_BODY_DATA;
            }
            else {
                this->current = HTTP2_CALLBACK_PARSE_BODY_DATA;
            }
            break;
        }
        default:
            MANAPIHTTP_LOG(this->site.async_context(), "Undefined frame type: {}", this->protocol.type);
    }

    //MANAPIHTTP_LOG("length: {} {} {} {}", this->protocol.length, this->protocol.flag, this->parse_vars.j, this->parse_vars.size);
    if (this->protocol.length > this->protocol.server_settings.max_frame_size+1) {
        generate_error (HTTP2_ERROR_FLOW_CONTROL_ERROR, std::format("The frame size > {}", this->protocol.server_settings.max_frame_size), this->protocol.stream_id);
        return;
    }
    if (this->protocol.type < HTTP2_FRAME_DATA || this->protocol.type > HTTP2_FRAME_PRIORITY_UPDATE) {
        this->protocol.padding = this->protocol.length;
        this->current = HTTP2_CALLBACK_PARSE_SKIP_N_BYTES;
    }
}

void manapi::net::worker::http_v2::_parse_number(char &c) {
    if (this->parse_vars.i == 0) {
        this->current = this->next;
        this->exec_callback (c);
        return;
    }
    *this->parse_vars.tmp = static_cast<ssize_t>((static_cast<size_t>(*this->parse_vars.tmp) << 8) | static_cast<unsigned char> (c));
    this->parse_vars.i--;
}

void manapi::net::worker::http_v2::exec_callback(char &c) {
    switch(this->current) {
        case 0:
            this->generate_error(HTTP2_ERROR_INTERNAL_ERROR, "unresolved callback");
            break;
        case HTTP2_CALLBACK_NEXT_LINE:
            this->_next_line(c);
            break;
        case HTTP2_CALLBACK_SKIP_MSG:
            this->_skip_sm_msg(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_LENGTH:
            this->_parse_header_length(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_TYPE:
            this->_parse_header_type(c);
            break;
        case HTTP2_CALLBACK_SKIP_NULL_OCTECTS:
            this->_skip_null_octet(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_OCTECTS:
            this->_parse_header_octets(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID:
            this->_parse_header_stream_id(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_FLAG:
            this->_parse_header_flag(c);
            break;
        case HTTP2_CALLBACK_PARSE_GOAWAY_LAST_STREAM_ID:
            this->_parse_goaway_last_stream_id(c);
            break;
        case HTTP2_CALLBACK_PARSE_GOAWAY_ERROR_CODE :
            this->_parse_goaway_error_code(c);
            break;
        case HTTP2_CALLBACK_PARSE_GOAWAY_ADDITIONAL_DATA:
            this->_parse_goaway_additional_debug_data(c);
            break;
        case HTTP2_CALLBACK_PARSE_WINDOW_UPDATE_VALUE:
            this->_parse_window_update_value(c);
            break;
        case HTTP2_CALLBACK_PARSE_RST_STREAM_ACTION:
            this->_parse_rst_stream_action(c);
            break;
        case HTTP2_CALLBACK_PARSE_PING_DATA :
            this->_parse_ping_data(c);
            break;
        case HTTP2_CALLBACK_PARSE_SETTING_ID:
            this->_parse_setting_id(c);
            break;
        case HTTP2_CALLBACK_PARSE_SETTING_VALUE:
            this->_parse_setting_value(c);
            break;
        case HTTP2_CALLBACK_PARSE_HEADER_DATA :
            this->_parse_header_data(c);
            break;
        case HTTP2_CALLBACK_PARSE_BODY_DATA :
            this->_parse_body_data(c);
            break;
        case HTTP2_CALLBACK_PARSE_SKIP_N_BYTES:
            this->_parse_skip_n_bytes(c);
            break;
        case HTTP2_CALLBACK_PARSE_FIELD_BLOCK:
            this->_parse_field_block(c);
            break;
        case HTTP2_CALLBACK_PARSE_NUMBER:
            this->_parse_number(c);
            break;
        default:
            generate_error(HTTP2_ERROR_INTERNAL_ERROR, "unresolved callback");
    }
}

void manapi::net::worker::http_v2::flush_write_buffer() {
    /* send write buffers */
    while (this->write_buffer_last) {
        auto size = static_cast<ssize_t>(this->write_buffer->buffer->size());
        if (!this->write_buffer->next) { size = std::min(size, this->write_buffer_cursor); }

        auto copy = size - this->write_buffer_current;
        if (copy) {
            auto rhs = this->worker->sync_write(this->connection.get(),
                this->write_buffer->buffer->data() + this->write_buffer_current, this->write_buffer_size ? this->write_buffer_size : copy);
            if (rhs < 0) {
                if (rhs == IO_WANT_AGAIN) {

                }
                else if (rhs == IO_WANT_WRITE) {
                    this->write_buffer_size = copy;
                    this->set_watcher_event(ev::WRITE);
                }
                else if (rhs == IO_WANT_READ) {
                    this->set_watcher_event(ev::READ);
                }
                else {
                    generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "write failed");
                }

                rhs = 0;
            }
            if (rhs == 0) { break; }

            /* great ! */
            this->write_buffer_size = 0;
            this->write_buffer_current += rhs;
        }

        if (this->write_buffer_current == size) {
            this->write_buffer_current = 0;
            /* end of buffer */
            if (!this->write_buffer->next) {
                this->write_buffer = nullptr;
                this->write_buffer_last = nullptr;
                this->write_buffer_cursor = 0;
                break;
            }

            auto b = std::move(this->write_buffer->next);
            this->write_buffer = std::move(b);
        }
    }
}

void manapi::net::worker::http_v2::init_write_buffer() {
    if (!this->write_buffer_last || this->write_buffer_last->buffer->size() == this->write_buffer_cursor) {
        /* buffer size must be greater than the max frame size! */
        auto bwrite = this->site.bufferpool()->get();
        bwrite->resize(this->buffer_size());
        if (this->write_buffer_last) {
            this->write_buffer_last->next = std::make_unique<http_v2_write_buffers>(std::move(bwrite), nullptr);
            this->write_buffer_last = this->write_buffer_last->next.get();
        }
        else {
            if (!this->write_buffer) {
                this->write_buffer = std::make_unique<http_v2_write_buffers>(std::move(bwrite), nullptr);
            }
            this->write_buffer_last = this->write_buffer.get();
            this->set_watcher_event(ev::WRITE);
        }
        this->write_buffer_cursor = 0;
    }
}

void manapi::net::worker::http_v2::send_frame(http2_frame_type frame, uint8_t flag, int stream_id, std::string_view data) {
    static char response[9];
    stringify_stream_id(stream_id, response + 5);
    stringify_number <int> (static_cast<int>(data.size()), response, 3);
    response[3] = static_cast<char>(frame);
    response[4] = static_cast<char>(flag);

    //std::cout << "SND: " << frame << " len=" << data.size() <<" flg="<<((int)flag)  <<  "\n";

    ssize_t rhs;
    if (this->write_buffer_last) {
        rhs = 0;
    }
    else {
        rhs = 0;
        //rhs = this->worker->sync_write(this->connection.get(), response, sizeof (response));
    }

    if (rhs < 0) {
        if (rhs == IO_WANT_AGAIN) {

        }
        else if (rhs == IO_WANT_WRITE) {
            this->write_buffer_size = sizeof (response);
            this->set_watcher_event(ev::WRITE);
        }
        else if (rhs == IO_WANT_READ) {
            this->set_watcher_event(ev::READ);
        }
        else {
            goto err;
        }

        rhs = 0;
    }
    while (rhs != sizeof (response)) {
        init_write_buffer();
        const auto copy = std::min(static_cast<ssize_t>(this->write_buffer_last->buffer->size() - this->write_buffer_cursor), static_cast<ssize_t>(sizeof (response)) - rhs);
        memcpy(this->write_buffer_last->buffer->data() + this->write_buffer_cursor, response + rhs, copy);
        this->write_buffer_cursor += copy;
        rhs += copy;
    }

    if (!data.empty()) {
        bool flg = false;
        if (this->write_buffer_last) {
            rhs = 0;
        }
        else {
            flg = true;
            rhs = 0;
            //rhs = this->worker->sync_write(this->connection.get(), data.data(), static_cast<ssize_t>(data.size()));
        }

        if (rhs < 0) {
            if (rhs == IO_WANT_AGAIN) {

            }
            else if (rhs == IO_WANT_WRITE) {
                this->write_buffer_size = static_cast<ssize_t>(data.size());
                this->set_watcher_event(ev::WRITE);
            }
            else if (rhs == IO_WANT_READ) {
                this->set_watcher_event(ev::READ);
            }
            else {
                goto err;
            }

            rhs = 0;
        }
        while (rhs != data.size()) {
            init_write_buffer();
            const auto copy = std::min(static_cast<ssize_t>(this->write_buffer_last->buffer->size() - this->write_buffer_cursor), static_cast<ssize_t>(data.size()) - rhs);
            memcpy (this->write_buffer_last->buffer->data() + this->write_buffer_cursor, data.data() + rhs, copy);
            this->write_buffer_cursor += copy;
            rhs += copy;
        }

        if (flg) {
            flush_write_buffer();
        }
    }

    return;

    err:
    if (stream_id) {
        auto thread = this->threads.find(stream_id);
        if (thread != this->threads.end()) {
            thread->second->atomic_flags.store(HTTP2_THREAD_ATOMIC_RST);
            thread->second->mx.unlock();
        }
    }
    this->generate_error(HTTP2_ERROR_STREAM_CLOSED, std::format("stream {} closed", stream_id), static_cast<int>(stream_id));
}

void manapi::net::worker::http_v2::send_empty_frame(http2_frame_type frame, char flag, int stream_id) {
    send_frame (frame, flag, stream_id, "");
}

void manapi::net::worker::http_v2::timer_watcher(const std::shared_ptr<manapi::net::worker::base> &dep) {
    if (this->threads.empty()) {
        this->protocol.current_timeout -= (this->config->speed_check_delay());
        if (this->protocol.current_timeout <= 0) {
            //MANAPIHTTP_LOG2("TIMEOUT HTTP2");
            this->ping_interval.sync_stop(this->site.async_context());
            //MANAPIHTTP_LOG2("TIMEOUT HTTP2 2");
            this->close_http2_connection(HTTP2_ERROR_STREAM_CLOSED, "timeout");
            //MANAPIHTTP_LOG2("TIMEOUT HTTP2 3");
            return;
        }

        if (!this->protocol.conn_type
            && std::chrono::system_clock::now() > this->protocol.prev_ping_time_point + this->protocol.ping_delay) {
            /**
             * PING frames prevent curl from working properly.
             * since curl doesn't accept packets, it has a
             * rather small window size.
             *
             * There is no space for PING frames in the write window.
             */
            // if (!send_ping_frame()) {
            //     return;
            // }
            this->protocol.prev_ping_time_point = std::chrono::system_clock::now();
        }
    }
    else {
        for (const auto &thread : this->threads) {
            if (thread.second->conn_flags & HTTP2_THREAD_IO) {
                if (thread.second->transfered_last_delay < this->config->speed_check_bytes()) {
                    thread.second->conn_flags ^= HTTP2_THREAD_IO;
                    thread.second->atomic_flags.store(HTTP2_THREAD_ATOMIC_RST);
                    this->reset_stream(thread.second->id, HTTP2_ERROR_CANCEL);
                    thread.second->mx.unlock();
                }

                thread.second->transfered_last_delay = 0;
            }
        }
    }
}

bool manapi::net::worker::http_v2::send_ping_frame(std::string data) {
    char flag = 0x00;
    if (data.size()==8) {
        flag |= HTTP2_FLAG_PING_ACK;
        send_frame(HTTP2_FRAME_PING, flag, 0, data);
    }
    else {
        {
            if (this->protocol.pings.size() > 3) {
                // timeout
                MANAPIHTTP_LOG2(this->site.async_context(), "PING IGNORE -> close connection");
                /* close connection quietly */
                this->close_http2_connection(HTTP2_ERROR_NO_ERROR, "");
                return false;
            }

            do {
                data = string::random(8);
            }
            while (this->protocol.pings.contains(data));

            this->protocol.pings.insert(data);
        }

        send_frame(HTTP2_FRAME_PING, flag, 0, data);
    }
    return true;
}

void manapi::net::worker::http_v2::close_http2_connection(int errnum, std::string additional_data) {
    if ((this->protocol.conn_type) & CONN_CLOSED) { return; }
    this->protocol.conn_type |= CONN_CLOSED;

    this->reset_all_streams();
    this->empty_setting_timeouts();

    if (this->ping_interval) {
        this->ping_interval.sync_stop(this->site.async_context());
        this->ping_interval = nullptr;
    }

    this->site.async_context()->eventloop()->stop_watcher(this->io_call_watcher);

    if (errnum >= 0) {
        /* from now on, ping_interval will be timeout i/o */
        this->ping_interval = this->site.async_context()->timerpool()->append_timer_sync(1000, [this] (manapi::timer t)
            -> void {
            this->site.async_context()->eventloop()->stop_watcher(std::move(this->watcher));
            this->http2_resolve_();
        });

        // this->site.async_context()->eventloop()->stop_watcher(this->watcher);
        this->site.async_context()->eventloop()->callback_io_watcher (this->watcher, [this] (std::shared_ptr<ev::io> &w, int status, int revents)
            -> void {
            if (this->watcher->is_active()) {
                this->watcher->restart(ev::WRITE);
            }

            try {
                this->flush_write_buffer();
            }
            catch (...) {
                /* failure */
                this->write_buffer = nullptr;
                this->write_buffer_last = nullptr;
            }

            if (!this->write_buffer_last) {
                auto this2 = this;

                this2->ping_interval.sync_stop(this2->site.async_context());
                this2->site.async_context()->eventloop()->stop_watcher(this2->watcher);
                this2->http2_resolve_();
            }
        });

        if (this->watcher->is_active()) {
            this->watcher->restart(ev::READ|ev::WRITE);
        }

        try {
            std::string buffer;
            buffer.resize(8 + additional_data.size());

            stringify_number<int> (this->protocol.last_stream_id, buffer.data());
            stringify_number<int>(errnum, buffer.data() + 4);
            memcpy(buffer.data() + 8, additional_data.data(), additional_data.size());

            //std::cout << "SEND GOAWAY\n";
            this->send_frame(HTTP2_FRAME_GOAWAY, 0x00, 0, buffer);
            //std::cout << "SEND GOAWAY - SUCCESS\n";
        }
        catch (...) {
            /* skip error messages */

        }
    }
    else {
        /* close it quietly */
        std::cout << "close watcher\n";
        this->site.async_context()->eventloop()->stop_watcher(this->watcher);
        this->http2_resolve_();
    }
}

void manapi::net::worker::http_v2::send_settings(const std::vector<std::pair<short, int>> &options) {
    auto handle = [this] (std::shared_ptr<http_v2> worker) -> future<void> {
        // timeout
        if (worker->protocol.conn_type & CONN_CLOSED) { co_return; }
        generate_error(HTTP2_ERROR_SETTINGS_TIMEOUT, "recv settings timeout", 0);
    };

    this->protocol.setting_timeout.push(this->site.async_context()->timerpool()->append_timer_async(3000, [handle = std::move(handle), worker = std::move(this->new_dependency ())] (manapi::timer t) mutable -> future<void> {
        co_await handle(std::move(worker));
    }));
    char buffer[options.size() * 6];
    int index = 0;
    for (const auto &option: options) {
        this->update_setting(static_cast<http2_setting_type>(option.first), option.second, true);
        stringify_number<short>(option.first, buffer + index * 6);
        stringify_number<int>(option.second, buffer + index * 6 + 2);
        index++;
    }
    send_frame (HTTP2_FRAME_SETTINGS, 0x0, 0, {buffer, options.size() * 6});
}

ssize_t manapi::net::worker::http_v2::send_data(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator stream, const void *buf, ssize_t size, bool finish) {
    if (!size && !finish) { return size; }

    /* connection window */
    auto send = std::min(size, static_cast<ssize_t>(this->protocol.window.write));
    send = std::min(send, stream->second->write_window);

    this->protocol.window.write -= send;
    stream->second->write_window -= send;

    if (finish && send != size) { finish = false; }

    ssize_t frame_size = this->protocol.client_settings.max_frame_size;
    ssize_t rhs = 0;
    while (rhs != send) {
        auto real_size = std::min(send - rhs, frame_size);
        char cflag = 0x00;
        if (finish && rhs + real_size == send) {
            /* eos */
            cflag |= HTTP2_FLAG_DATA_END_STREAM;
        }
        this->send_frame(HTTP2_FRAME_DATA, cflag, stream->first, std::string_view(static_cast<const char *> (buf) + rhs, real_size));
        rhs += real_size;
    }
    return rhs;
}

void manapi::net::worker::http_v2::send_window_frame(int stream_id, int size) {
    static char buffer[4];
    stringify_number <int> (size, buffer);
    send_frame (HTTP2_FRAME_WINDOW_UPDATE, 0x00, stream_id, {buffer, sizeof (buffer)});
}

void manapi::net::worker::http_v2::resolve_timeout_timer() {
    this->protocol.current_timeout = (this->protocol.timeout);
}

void manapi::net::worker::http_v2::default_ev_headers(int id) {
    worker::http_v2::session_worker(this->threads.find(id));
}

void manapi::net::worker::http_v2::default_ev_data(int id) {
    std::cout << "Data\n";
}

void manapi::net::worker::http_v2::default_ev_goaway(int last_stream_id, int errnum, std::string errmsg) {
    // MANAPIHTTP_LOG("Client sent GOAWAY Frame:\n > Error Code: {}\n > Error Msg: {}\nLast-Stream-Id:{}",
    //     errnum, errmsg, last_stream_id);
}

void manapi::net::worker::http_v2::default_ev_finished(int id) {

}

void manapi::net::worker::http_v2::default_ev_priopity_update(int id, int prioritized_id, std::string prioritized_value) {

}

void manapi::net::worker::http_v2::default_ev_rst_stream(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it, int errnum) {
    if(it == this->threads.end()) { return; }

    it->second->atomic_flags.store(HTTP2_THREAD_ATOMIC_RST);
    it->second->mx.unlock();
}

void manapi::net::worker::http_v2::unlimit_all_streams() {
    for (auto & thread : this->threads) {
        thread.second->write_window = std::numeric_limits<ssize_t>::max();
    }

    this->flush_io_streams();
}

void manapi::net::worker::http_v2::reset_all_streams() {
    for (auto it = this->threads.begin(); it != this->threads.end(); ++it) {
        this->callbacks.rst_stream(it, HTTP2_ERROR_NO_ERROR);
    }

    this->flush_io_streams();
}

void manapi::net::worker::http_v2::delete_stream_id(int id) {
    this->threads.erase(id);
}

void manapi::net::worker::http_v2::reset_stream(int id, int errnum) {
    static char buffer[4];
    stringify_number<int> (errnum, buffer);
    this->send_frame(HTTP2_FRAME_RST_STREAM, 0x0, id, {buffer, sizeof (buffer)});
}

void manapi::net::worker::http_v2::session_worker(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it) {
    if (it == this->threads.end()) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Failed to find session thread data by id");  }
    auto worker = this->new_dependency();
    auto id = it->first;
    auto client = std::make_shared<http::http_v2> (worker, this->config, this->site);
    client->connection = std::make_shared<worker::connection>(new manapi_http_2_connection_t (it->second.get(), 0, 0, 0),
        [] (void *ptr) -> void { delete static_cast<manapi_http_2_connection_t *> (ptr); });
    client->connection->version = manapi::net::http::versions::HTTP_v2;

    client->request_data.headers = std::move(it->second->headers);
    client->request_data.body_index = 0;
    client->request_data.uri = client->request_data.headers[":path"];
    client->request_data.headers_size = 0;
    client->request_data.divided = -1;
    client->request_data.http = http::versions::HTTP_v2;
    client->request_data.has_body = it->second->conn_flags & HTTP2_THREAD_HAS_BODY;
    client->request_data.body_left = 0;
    if (client->request_data.has_body) {
        auto contentlength = client->request_data.headers.find(http::HEADER.CONTENT_LENGTH);
        client->request_data.headers_part = 0;
        client->request_data.body_part = 0;
        client->request_data.body_size = contentlength != client->request_data.headers.end() ? std::stoll(contentlength->second) : -1 /* the size isn't fixed */;
    }
    else {
        client->request_data.body_size = 0;
        client->request_data.body_part = 0;
    }
    client->request_data.body_left = client->request_data.body_size;
    client->request_data.method = client->request_data.headers[":method"];

    async::run(this->site.async_context(), [id, client = std::move(client), worker = std::move(worker)] () mutable -> manapi::future<> {
        int stream_errnum = HTTP2_ERROR_NO_ERROR;

        try {
            if (co_await client->parse_request(0,0)) {
                co_await client->execute_handler();
            }

        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG(worker->site.async_context(), "session_worker(...): {}", e.what());
            stream_errnum = HTTP2_ERROR_INTERNAL_ERROR;
        }

        co_await worker->site.async_context()->eventloop()->custom_callback([worker, id, stream_errnum] (event_loop *ev)
            -> void {
            if (stream_errnum != HTTP2_ERROR_NO_ERROR) {
                // an error occurreds
                worker->reset_stream (id, stream_errnum);
            }

            auto it = worker->threads.find(id);
            if (it == worker->threads.end()) { return; }

            it->second->conn_flags |= HTTP2_THREAD_EOS;
            worker->flush_io_stream(it);
        });
    });
}

std::map<int, std::unique_ptr<manapi::net::worker::http_v2::http_v2_thread_data_t>>::iterator manapi::net::worker::http_v2::flush_io_stream(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it) {
    int flag = it->second->atomic_flags;
    if (flag & HTTP2_THREAD_ATOMIC_HEADERS) {
        /* send headers */
        this->send_headers(*it->second);
        it->second->atomic_flags.fetch_xor(HTTP2_THREAD_ATOMIC_HEADERS);
        it->second->mx.unlock();
    }
    if (flag & HTTP2_THREAD_ATOMIC_WANT_READ) {
        ssize_t freed = 0;
        if (it->second->read_storage_buffer) {
            it->second->read_buffer = std::move(it->second->read_storage_buffer->buffer);
            it->second->read_storage_buffer = std::move(it->second->read_storage_buffer->next);

            if (!it->second->read_storage_buffer) {
                it->second->read_buffer->resize(it->second->read_storage_size);
                it->second->read_storage_size = 0;
                it->second->read_storage_last = nullptr;
            }

            freed += it->second->read_buffer->size();
        }

        it->second->transfered_last_delay += freed;

        if ((it->second->conn_flags & HTTP2_THREAD_RECV_EOS) && (!it->second->read_storage_last)) {
            this->disable_status_io(*it->second);
            it->second->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_RECV_EOS);
            it->second->atomic_flags.fetch_xor(HTTP2_THREAD_ATOMIC_WANT_READ);

            it->second->read_freed += freed;
            it->second->mx.unlock();
        }
        else if (freed) {
            this->disable_status_io(*it->second);
            it->second->atomic_flags.fetch_xor(HTTP2_THREAD_ATOMIC_WANT_READ);
            it->second->read_freed += freed;
            it->second->mx.unlock();

            /* conn window frame */
            if (this->protocol.window.read < this->protocol.window.conn_window
                && this->protocol.window.read + this->protocol.window.read <= this->protocol.window.conn_window) {
                const auto size = this->protocol.window.conn_window - this->protocol.window.read;
                this->protocol.window.read += size;
                this->send_window_frame(0, static_cast<int>(size));
                }

            /* stream window frame */
            if (it->second->read_freed >= it->second->read_window) {
                const auto size = this->protocol.window.stream_window - it->second->read_window;
                if (size) {
                    it->second->read_window += size;
                    it->second->read_freed = 0;
                    this->send_window_frame(it->first, static_cast<int>(size));
                }
            }
        }
        else {
            this->enable_status_io(*it->second);
        }
    }
    if (flag & HTTP2_THREAD_ATOMIC_WANT_WRITE) {
        const auto rhs = this->send_data(it, it->second->write_buffer->data() + it->second->write_cursor,
            static_cast<ssize_t>(it->second->write_buffer->size()) - it->second->write_cursor, flag & HTTP2_THREAD_ATOMIC_SEND_EOS);

        it->second->transfered_last_delay += rhs;

        if (rhs + it->second->write_cursor == it->second->write_buffer->size()) {
            it->second->write_buffer = {};
            it->second->write_cursor = 0;

            this->disable_status_io(*it->second);
            it->second->atomic_flags.fetch_xor(HTTP2_THREAD_ATOMIC_WANT_WRITE);
            it->second->mx.unlock();
        }
        else {
            this->enable_status_io(*it->second);
            it->second->write_cursor += rhs;
        }
    }

    if (it->second->conn_flags & HTTP2_THREAD_EOS) {
        if (0 == (flag & (HTTP2_THREAD_ATOMIC_HEADERS | HTTP2_THREAD_ATOMIC_WANT_READ | HTTP2_THREAD_ATOMIC_WANT_WRITE))) {
            return this->threads.erase(it);
        }
    }

    return it;
}

void manapi::net::worker::http_v2::flush_io_streams() {
    /* stream flush */
    for (auto it = this->threads.begin(); it != this->threads.end();) {
        try {
            auto next = flush_io_stream(it);
            if (next == it) {
                ++it;
            }
            else {
                it = next;
            }
        }
        catch (...) {
            if (!this->watcher) {
                return;
            }
        }
    }

    if (this->watcher) {
        if (this->write_buffer) {
            this->set_watcher_event(ev::WRITE);
        }
    }
}

void manapi::net::worker::http_v2::io_call_callback(std::shared_ptr<ev::async> &w) {
    this->flush_io_streams();
}

void manapi::net::worker::http_v2::send_headers(http_v2_thread_data_t &stream) {
    manapi::compress::hpack::encoder_t encoder;
    for (auto &header: stream.headers) {
        encoder.add (compress::hpack::header_t(header.first, std::move(header.second)));
    }
    uint8_t cflag = 0x0;
    const auto data = encoder.data();
    size_t cnt = 0;
    auto frameSize = static_cast<size_t>(this->protocol.client_settings.max_frame_size);
    http2_frame_type ft = HTTP2_FRAME_HEADERS;
    if (stream.atomic_flags & HTTP2_THREAD_ATOMIC_SEND_EOS) { cflag |= HTTP2_FLAG_HEADERS_END_STREAM; }
    goto skip;
    while (cnt < data.size()) {
        ft = HTTP2_FRAME_CONTINUATION;
        skip:
        auto left = std::min(frameSize, data.size() - cnt);
        if (cnt + left == data.size()) {
            cflag |= HTTP2_FLAG_HEADERS_END_HEADERS;
        }
        send_frame(ft, cflag, stream.id, std::string_view(data.data() + cnt, left));
        cflag = 0;
        cnt += left;
    }
}

void manapi::net::worker::http_v2::enable_status_io(http_v2_thread_data_t &stream) {
    stream.conn_flags |= HTTP2_THREAD_IO;
}

void manapi::net::worker::http_v2::disable_status_io(http_v2_thread_data_t &stream) {
    if (stream.conn_flags & HTTP2_THREAD_IO) {
        stream.conn_flags ^= HTTP2_THREAD_IO;
    }
}

manapi::future<ssize_t> manapi::net::worker::http_v2::default_read(worker::connection &connection, void *buff, ssize_t size) {
    auto &conn = connection.as<manapi_http_2_connection_t>();
    ssize_t rhs = 0;

    while (true) {
        if (conn.original->atomic_flags & HTTP2_THREAD_ATOMIC_RST) {
            co_return -1;
        }

        if ((!conn.read_buffer || conn.read_buffer->size() == conn.read_cursor)) {
            if (conn.flags & HTTP2_THREAD_RECV_EOS) { /* eos was reached */ break; }

            co_await conn.original->mx.lock();

            conn.read_buffer = std::move(conn.original->read_buffer);
            conn.read_cursor = 0;

            if (conn.original->atomic_flags & HTTP2_THREAD_ATOMIC_RECV_EOS) {
                conn.flags |= HTTP2_THREAD_RECV_EOS;
                conn.original->mx.unlock();
            }
            else {
                conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_WANT_READ);
                this->io_call_watcher->send();
            }

            continue;
        }

        auto copy = std::min(size, static_cast<ssize_t>(conn.read_buffer->size() - conn.read_cursor));
        memcpy (buff, conn.read_buffer->data(), copy);

        conn.read_cursor += copy;
        rhs += copy;

        break;
    }

    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::http_v2::default_write(worker::connection &connection, const void *buff, ssize_t size, bool flag) {
    auto &conn = connection.as<manapi_http_2_connection_t>();
    ssize_t rhs = 0;

    while (rhs != size) {
        if (conn.original->atomic_flags & HTTP2_THREAD_ATOMIC_RST) {
            co_return -1;
        }

        if (!conn.write_buffer) { conn.write_buffer = this->site.bufferpool()->get(); conn.write_buffer->resize(this->buffer_size()); }

        auto copy = std::min(size - rhs, static_cast<ssize_t>(conn.write_buffer->size()) - conn.write_cursor);
        memcpy (conn.write_buffer->data() + conn.write_cursor, static_cast<const char *>(buff) + rhs, copy);

        conn.write_cursor += copy;
        rhs += copy;

        const bool t_flag = (flag && rhs == size);
        if (conn.write_cursor == conn.write_buffer->size() || t_flag) {
            co_await conn.original->mx.lock();

            if (t_flag) {
                conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_SEND_EOS);
            }

            conn.write_buffer->resize(conn.write_cursor);
            conn.original->write_buffer = std::move(conn.write_buffer);
            conn.write_cursor = 0;

            conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_WANT_WRITE);
            this->io_call_watcher->send();

            continue;
        }

        break;
    }

    co_return rhs;
}

void manapi::net::worker::http_v2::stringify_stream_id(int stream_id, char *buffer) {
    int index = 0;
    buffer[index++] = static_cast<char> ((stream_id >> (8 * 3)) & 0x7F); // 127
    for (int i = 2; i >= 0; i--) {
        buffer[index++] = static_cast<char> ((stream_id >> i * 8) & 0xFF); // 256
    }
}

void manapi::net::worker::http_v2::setting_param_was_ack(const bool &self) {
    if (self || this->protocol.setting_param_acks == 0) {
        return;
    }

    if ((this->protocol.setting_param_acks -= (1)) == 1) {
        this->send_frame (HTTP2_FRAME_SETTINGS, HTTP2_FLAG_SETTINGS_ACK, 0, {});
    }
}

void manapi::net::worker::http_v2::setting_value_valid(const http2_setting_type &type, const int &value) noexcept(false) {
    if (manapi::net::worker::http_v2::allow_settings[type].valid(value)) {
        return;
    }

    generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid http2 setting param");
}

void manapi::net::worker::http_v2::_parse_header_flag (char &c) {
    
    this->parse_vars.i ++;
    this->parse_vars.buffint = static_cast<ssize_t>((static_cast<size_t>(this->parse_vars.buffint) << 8) | static_cast<unsigned char>(c));

    if (this->parse_vars.i == 5) {
        // FLAGS

        // > HEADERS
        // Unused Flags (5),
        // END_HEADERS Flag (1),
        // Unused Flags (1)
        // ENS_STREAM Flag (1)

        // > SETTINGS | > PING
        // Unused Flags (7),
        // ACK Flag (1)

        // > DATA
        // Unused Flags (4),
        // PADDED Flag (1),
        // Unused Flags (2),
        // END_STREAM Flag (1)

        this->protocol.flag = this->parse_vars.buffint;
        this->parse_vars.buffint = 0;

        this->current = HTTP2_CALLBACK_PARSE_HEADER_STREAM_ID;
    }
}
