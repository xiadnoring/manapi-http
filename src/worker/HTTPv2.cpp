#include "worker/HTTPv2.hpp"

#include <future>
#include <http/HTTPv2.hpp>

#include "ManapiString.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "http/HTTPv1_1.hpp"
#include "worker/OpenSSL_TLS.hpp"

#define HEADER_DEFAULT_SIZE 9

std::map <int, manapi::json_mask> manapi::net::worker::http_v2::allow_settings = {
    {HTTP2_SETTING_RESERVED, {"{null}"}},
    {HTTP2_SETTING_ENABLE_PUSH, {"{integer(>=0 <=1)}"}},
    {HTTP2_SETTING_MAX_FRAME_SIZE, {"{integer(>=16000 <=20000)}"}},
    {HTTP2_SETTING_HEADER_TABLE_SIZE, {"{integer(>=2048 <=65536)}"}},
    {HTTP2_SETTING_INITIAL_WINDOW_SIZE, {"{integer(>=1024)}"}},
    {HTTP2_SETTING_MAX_HEADER_LIST_SIZE, {"{integer(>=1024 <=1048576)}"}},
    {HTTP2_SETTING_TLS_RENEG_PERMITTED, {"{integer(0)}"}},
    {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, {"{integer(>=1 <=5)}"}},
    {HTTP2_SETTING_SETTINGS_ENABLE_METADATA, {"{integer(>=0 <=1)}"}},
    {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, {"{integer(>=0 <=1)}"}},
    {HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL, {"{integer(>=0 <=1)}"}}
};

manapi::net::worker::http_v2::http_v2(const std::shared_ptr<manapi::net::worker::base> &worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : base(site), protocol{worker->site.async_context()}, finishcv(worker->site.async_context()), worker(worker), threads_mutex(worker->site.async_context()) {
    this->config = std::move(config);
    this->init_settings();
    this->init_callbacks();
    this->protocol.setting_param_acks_mx = std::make_shared<async::mutex>(this->site.async_context());
    this->read = [this](net::worker::connection & PH1, void * && PH2, const size_t & PH3) -> future<ssize_t> {
        return this->default_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
    };
    this->write = [this](net::worker::connection & PH1, const void * && PH2, const size_t & PH3, bool && PH4) -> future<ssize_t> {
        return this->default_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3), std::forward<decltype(PH4)>(PH4));
    };
}

manapi::net::worker::http_v2::~http_v2() {

    std::cout << "Unmounted\n";
}

manapi::future<void> manapi::net::worker::http_v2::parse_request(ssize_t j, ssize_t size) {
    this->worker->disable_watcher_for_status(*this->connection, CONN_READ);

    this->buffer.resize(protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first);
    this->current = [this](char & PH1) { this->_next_line(std::forward<decltype(PH1)>(PH1)); };
    this->next = [this](char & PH1) { this->_skip_sm_msg(std::forward<decltype(PH1)>(PH1)); };
    this->parse_vars.j = j;
    this->parse_vars.size = size;

    this->protocol.window.write_cv = std::make_shared<async::condition_variable>(this->site.async_context());

    ssize_t rhs=-1;

    try {
        this->protocol.conn_type.fetch_or(CONN_IDLE);

        this->ping_interval.store(co_await this->site.async_context()->timerpool()->async_append_interval_async(std::chrono::milliseconds (this->protocol.timer_interval), [this, dep = new_dependency()] () -> future<void> {
            co_await this->timer_watcher();
        }));

        co_await send_settings ({
            {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
            {HTTP2_SETTING_ENABLE_PUSH, 0},
            {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, 100},
            {HTTP2_SETTING_MAX_HEADER_LIST_SIZE,  65000}
        });

        goto skip;
        while ((this->protocol.conn_type & (CONN_CLOSED | CONN_HALF_CLOSED)) == false) {
            rhs = co_await this->worker->read (*this->connection, this->buffer.data(), this->buffer.size());

            if (rhs <= 0) {
                std::cout << "HALF CLOSED BY READ\n";
                this->protocol.conn_type.fetch_or(CONN_HALF_CLOSED);
                break;
            }

            this->parse_vars.size = rhs;

            this->parse_vars.j = 0;

            skip: for (; this->parse_vars.j < this->parse_vars.size && this->protocol.length > 0; this->parse_vars.j++, this->protocol.length--) {
                this->current (this->buffer[this->parse_vars.j]);
                if (this->protocol.parse_exception != nullptr) {
                    co_await this->protocol.parse_exception;
                }
            }

            if (this->protocol.length < 0) {
                this->protocol.length = 0;
            }
            if (this->protocol.length == 0) {
                if (this->protocol.type == HTTP2_FRAME_PING) {
                    if (this->protocol.flag & HTTP2_FLAG_PING_ACK) {
                        auto lk = co_await this->protocol.mx.lock_guard();
                        auto it = this->protocol.pings.find(this->parse_vars.buffer);
                        // The endpoint MUST NOT respond to PING frames with ACK if not exists in protocol.pings
                        if (it != this->protocol.pings.end()) {
                            this->protocol.pings.erase(it);
                        }
                    }
                    else {
                        co_await this->send_ping_frame(this->parse_vars.buffer);
                    }
                    this->parse_vars.buffer.clear();
                }
                else {
                    this->protocol.current_timeout.store(protocol.timeout);
                }

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
                        auto session = this->sessions.find(this->protocol.stream_id);
                        if (session == this->sessions.end()) {
                            co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                        }
                        // pass
                        break;
                    }
                    case HTTP2_FRAME_SETTINGS: {
                        if (this->protocol.flag & HTTP2_FLAG_SETTINGS_ACK) {
                            // settings were accepted
                            if (this->protocol.setting_timeout.empty()) {
                                co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "unexpected frame");
                            }
                            co_await this->site.async_context()->timerpool()->async_remove_timer(this->protocol.setting_timeout.front());
                            this->protocol.setting_timeout.pop();
                            if (this->protocol.conn_type & CONN_IDLE) {
                                this->protocol.conn_type.fetch_xor(CONN_IDLE);
                            }
                        }
                        break;
                    }
                    case HTTP2_FRAME_HEADERS:
                    case HTTP2_FRAME_CONTINUATION: {
                        auto session = this->sessions.find(this->protocol.stream_id);
                        if (session == this->sessions.end()) {
                            co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                        }
                        if (session->second.type == HTTP2_CONN_HALF_CLOSED_REMOTE || session->second.type == HTTP2_CONN_CLOSED) {
                            co_await this->generate_error(HTTP2_ERROR_STREAM_CLOSED, "Invalid frame was received to half-closed(remote)", this->protocol.stream_id);
                        }
                        if (this->protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
                            session->second.body = ! (this->protocol.flag & HTTP2_FLAG_HEADERS_END_STREAM);
                            co_await this->callbacks.headers(session->second.id, std::move(session->second.headers));
                            session->second.type = HTTP2_CONN_HALF_CLOSED_REMOTE;

                            if (this->protocol.flag & HTTP2_FLAG_HEADERS_END_STREAM) {
                                this->callbacks.finished(session->second.id);
                                session->second.type = HTTP2_CONN_CLOSED;
                                //sessions.erase(session);
                            }
                            else {
                                session->second.body = true;
                                this->callbacks.data (session->second.id);
                            }
                        }

                        break;
                    }
                    case HTTP2_FRAME_DATA: {
                        {
                            std::shared_ptr<smart_r_buffer> read;
                            auto len = static_cast<ssize_t>(this->parse_vars.buffer.size());
                            {
                                auto lk = co_await this->threads_mutex.lock_guard();
                                auto thread = this->threads.find(this->protocol.stream_id);
                                if (thread == this->threads.end()) {
                                    lk.call();
                                    this->parse_vars.buffer.clear();
                                    co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("stream {} doesn't exists", this->protocol.stream_id));
                                }
                                read = thread->second.read;
                            }
                            if (read && len != co_await read->add(this->parse_vars.buffer.data(), len, this->protocol.flag & HTTP2_FLAG_DATA_END_STREAM)) {
                                this->parse_vars.buffer.clear();
                                co_await this->generate_error(HTTP2_ERROR_FLOW_CONTROL_ERROR, std::format("read buffer overflow"), this->protocol.stream_id);
                            }
                            this->parse_vars.buffer.clear();
                        }

                        {
                            auto session = this->sessions.find(this->protocol.stream_id);
                            if (session == this->sessions.end()) { break; }

                            if (this->protocol.flag & HTTP2_FLAG_DATA_END_STREAM) {
                                this->callbacks.finished (session->second.id);
                                //sessions.erase(session);
                                session->second.type = HTTP2_CONN_CLOSED;
                            }
                        }

                        break;
                    }
                    case HTTP2_FRAME_GOAWAY: {
                        this->callbacks.goaway(this->protocol.error.last_stream_id, this->protocol.error.errnum, std::move(this->protocol.error.errmsg));
                        if (this->protocol.error.errnum >= HTTP2_ERROR_NO_ERROR && this->protocol.error.errnum <= HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                            this->protocol.error.last_stream_id = 0;
                            co_await this->generate_error(HTTP2_ERROR_NO_ERROR, {}, this->protocol.error.last_stream_id);
                        }
                        else {
                            // GOAWAY frame with unknown error code
                            // The endpoint MUST NOT trigger any special behavior.
                            this->protocol.error.errnum = 0;
                            this->protocol.error.last_stream_id = 0;
                            this->protocol.error.errmsg.clear();
                        }
                        break;
                    }
                    case HTTP2_FRAME_RST_STREAM: {
                        // protocol.value - err code
                        const int &err_code = this->protocol.value;

                        if (err_code >= HTTP2_ERROR_NO_ERROR && err_code <= HTTP2_ERROR_HTTP_1_1_REQUIRED) {
                            auto lk = co_await this->threads_mutex.lock_guard();
                            auto thread = this->threads.find(this->protocol.stream_id);
                            if (thread == this->threads.end()) {
                                lk.call();
                                co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("stream {} doesn't exists", this->protocol.stream_id));
                            }
                            this->callbacks.rst_stream (thread->first, err_code);
                        }

                        this->protocol.value = 0;
                        break;
                    }
                    case HTTP2_FRAME_WINDOW_UPDATE: {
                        //MANAPIHTTP_LOG ("window frame пришел ура праздник 🎉🎉🎉 {} {} {}", protocol.value, protocol.stream_id, protocol.flag);
                        if (protocol.stream_id == 0) {
                            // global
                            protocol.window.write.fetch_add(protocol.value);
                            co_await protocol.window.write_cv->notify_all();
                            protocol.value = 0;
                            break;
                        }

                        if (this->sessions.find(this->protocol.stream_id) == this->sessions.end()) {
                            co_await this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "stream doesn't exists");
                        }

                        auto lk = co_await this->threads_mutex.lock_guard();
                        auto thread = this->threads.find(this->protocol.stream_id);
                        if (thread == this->threads.end()) {
                            break;
                        }
                        co_await thread->second.write->add_allow_to_sent(this->protocol.value);
                        this->protocol.value = 0;
                        break;
                    }
                    default: {
                        //MANAPIHTTP_LOG("frame type: {}", protocol.type);
                        //protocol.closed = true;
                    }
                }

                if ((this->protocol.conn_type & (CONN_CLOSED | CONN_HALF_CLOSED)) == false) {
                    this->current = [this](char & PH1) { _parse_header_length(PH1); };
                    this->protocol.length = HEADER_DEFAULT_SIZE;
                    this->parse_vars.i = 0;
                    this->parse_vars.buffint = 0;
                    //goto skip;
                    if (this->parse_vars.j < this->parse_vars.size) {
                        goto skip;
                    }
                }
            }
        }
    }
    catch (manapi::exception const &e) {
        MANAPIHTTP_LOG("[{}]: HTTP2 Exception: {}", static_cast<size_t>(e.get_err_num()), e.what());
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("HTTP2 Exception: {}", e.what());
    }


    this->protocol.window.write.store(std::numeric_limits<int>::max());
    co_await this->protocol.window.write_cv->notify_all();

    if ((this->protocol.conn_type & (CONN_CLOSED|CONN_HALF_CLOSED)) == false) {
        // CONN_CLOSED...
        co_await reset_all_streams();
    }

    std::cout << "Preparing for close\n";
    auto lk = co_await this->threads_mutex.lock_guard();
    co_await this->finishcv.wait(this->threads_mutex,
        [this] () -> bool {
        std::cout << "BEEN NOTIFY " << this->threads.empty() << " " << this->deps << "\n";
        return this->threads.empty() && this->deps == 0;
    });
    co_await this->site.async_context()->timerpool()->async_remove_timer(this->ping_interval.exchange(0));
    if ((this->protocol.conn_type & CONN_CLOSED) == false ) {
        lk.call();
        co_await this->close_connection (HTTP2_ERROR_NO_ERROR, "shutdown", 0);
    }
    std::cout << this->threads.empty() << " Closing...\n";
    this->new_dependency = nullptr;
}

void manapi::net::worker::http_v2::init_settings() {
    this->protocol.settings[HTTP2_SETTING_HEADER_TABLE_SIZE].first.store(4096);
    this->protocol.settings[HTTP2_SETTING_HEADER_TABLE_SIZE].second = [this] (int value, bool self) -> void {
        this->setting_value_valid (HTTP2_SETTING_HEADER_TABLE_SIZE, value);
        this->protocol.settings[HTTP2_SETTING_HEADER_TABLE_SIZE].first.store(value);
        this->protocol.decoder.m_dynamic_max(value);
        this->protocol.decoder.m_dynamic_max(value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_ENABLE_PUSH].first.store(1);
    this->protocol.settings[HTTP2_SETTING_ENABLE_PUSH].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_ENABLE_PUSH, value);
        this->setting_param_was_ack(self);
    };

    this->protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first.store(std::numeric_limits<int>::max());
    this->protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_ENABLE_PUSH, value);
        this->protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first.store(value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first.store(65535);
    this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_INITIAL_WINDOW_SIZE, value);
        this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first.store(value);

        this->protocol.window.write.store(value);

        async::run(this->site.async_context(), [self, dp = new_dependency()] () -> future<void> {
            co_await dp->protocol.window.write_cv->notify_all();
            auto lk = co_await dp->threads_mutex.lock_guard();
            for (auto &thread : dp->threads) {
                co_await thread.second.write->resize(dp->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first.load());
                co_await thread.second.read->resize(dp->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first.load());
            }
            dp->setting_param_was_ack(self);
        });
    };
    this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first.store(16384);
    this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_MAX_FRAME_SIZE, value);
        this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first.store(value);
        this->buffer.resize(value);
        this->setting_param_was_ack(self);
    };

    this->protocol.settings[HTTP2_SETTING_MAX_HEADER_LIST_SIZE].first.store(INT_MAX);
    this->protocol.settings[HTTP2_SETTING_MAX_HEADER_LIST_SIZE].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_MAX_HEADER_LIST_SIZE, value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL].first.store(0);
    this->protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL, value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES].first.store(0);
    this->protocol.settings[HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_TLS_RENEG_PERMITTED].first.store(0);
    this->protocol.settings[HTTP2_SETTING_TLS_RENEG_PERMITTED].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_TLS_RENEG_PERMITTED, value);
        this->setting_param_was_ack(self);
    };
    this->protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_METADATA].first.store(0);
    this->protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_METADATA].second = [this] (int value, bool self) -> void {
        this->setting_value_valid(HTTP2_SETTING_SETTINGS_ENABLE_METADATA, value);
        this->setting_param_was_ack(self);
    };
}

void manapi::net::worker::http_v2::init_callbacks() {
    this->set_callbacks({
        .headers = [this](int PH1, std::map <std::string, std::string> PH2) -> future<void> {
            return this->default_ev_headers(PH1, std::move(PH2));
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
        .rst_stream = [this](int PH1, int PH2) {
            this->default_ev_rst_stream(PH1, PH2);
        },
        .finished = [this](int PH1) {
            this->default_ev_finished(PH1);
        }
    });
}

void manapi::net::worker::http_v2::set_callbacks(const http_v2_callbacks_t &callbacks) {
    this->callbacks = callbacks;
}

manapi::future<ssize_t> manapi::net::worker::http_v2::response(worker::connection &connection, http_response &resp, bool finish) {
    manapi::compress::hpack::encoder_t  encoder;
    encoder.add (compress::hpack::header_t(":status", std::to_string(resp.get_status_code())));
    for (const auto &header: resp.get_headers()) {
        encoder.add (compress::hpack::header_t(header.first, header.second));
    }
    uint8_t cflag = 0x0;
    const auto data = encoder.data();
    size_t cnt = 0;
    auto frameSize = static_cast<size_t>(protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first);
    http2_frame_type ft = HTTP2_FRAME_HEADERS;
    if (this->protocol.conn_type & CONN_CLOSED) {
        co_return -1;
    }
    goto skip;
    while (cnt < data.size() && (this->protocol.conn_type & CONN_CLOSED) == false) {
        ft = HTTP2_FRAME_CONTINUATION;
        skip:
        auto left = std::min(frameSize, data.size() - cnt);
        if (left != frameSize) {
            cflag |= HTTP2_FLAG_HEADERS_END_HEADERS;
            if (finish) { cflag |= HTTP2_FLAG_HEADERS_END_STREAM; }
        }
        co_await send_frame(ft, cflag, connection.as<int>(), std::string_view(data.data() + cnt, left));
        cnt += left;
    }
    co_return static_cast<ssize_t>(data.size());
}

void manapi::net::worker::http_v2::_deps_decrease() {
    this->deps.fetch_sub(1);
}

manapi::future<void> manapi::net::worker::http_v2::empty_setting_timeouts() {
    // auto lk = co_await this->protocol.mx.lock_guard(); must be called before
    while (!this->protocol.setting_timeout.empty()) {
        auto id = this->protocol.setting_timeout.back();
        this->protocol.setting_timeout.pop();
        co_await this->site.async_context()->timerpool()->async_remove_timer(id);
    }
    co_return;
}

manapi::future<void> manapi::net::worker::http_v2::generate_error(http2_error_type errnum, std::string errmsg, int last_stream_id) noexcept(false) {
    auto lk = co_await this->protocol.mx.lock_guard();
    if (false == (this->protocol.conn_type & CONN_CLOSED)) {
        co_await this->close_connection(errnum, errmsg, last_stream_id);
    }
    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, errmsg);
}

void manapi::net::worker::http_v2::_skip_sm_msg(char &c) {
    parse_vars.buffer += c;
    if (parse_vars.buffer.size() == 4) {
        if (parse_vars.buffer != "SM\r\n") {
            this->protocol.parse_exception = this->generate_error (HTTP2_ERROR_PROTOCOL_ERROR, "SM label is invalid");
            return;
        }
        parse_vars.buffer.clear();
        next = [this](char & PH1) { this->_parse_header_octets(std::forward<decltype(PH1)>(PH1)); };
        current = [this](char & PH1) { this->_next_line(std::forward<decltype(PH1)>(PH1)); };
    }
}

void manapi::net::worker::http_v2::_next_line(char &c) {
    if (parse_vars.next_line_state) {
        parse_vars.next_line_state = false;
        if (c == '\n') {
            current = next;
            return;
        }
    }
    else {
        if (c == '\r') {
            parse_vars.next_line_state = true;
            return;
        }
    }

    this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid next line: \\r\\n");
}

void manapi::net::worker::http_v2::_skip_null_octet(char &c) {
    if (c == '\0') {
        this->current = this->next;
        return;
    }
    this->current = this->next;
    this->current(c);
    MANAPIHTTP_LOG2("Invalid symbol");
}

void manapi::net::worker::http_v2::_parse_header_octets(char &c) {
    this->parse_vars.i = 0;
    this->current = [this](char & PH1) { this->_parse_header_length(std::forward<decltype(PH1)>(PH1)); };
    this->current(c);
}

void manapi::net::worker::http_v2::_parse_header_length(char &c) {
    this->parse_vars.i++;
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 0xXXXXXX
    if (this->parse_vars.i == 3) {
        this->protocol.length += static_cast<ssize_t>(this->parse_vars.buffint);
        this->parse_vars.buffint = 0;
        this->current = [this](char & PH1) { this->_parse_header_type(std::forward<decltype(PH1)>(PH1)); };
    }
}

void manapi::net::worker::http_v2::_parse_header_type(char &c) {
    this->parse_vars.i++;
    this->protocol.type = static_cast<int>(c);
    this->current = [this](char & PH1) { this->_parse_header_flag(std::forward<decltype(PH1)>(PH1)); };
}

void manapi::net::worker::http_v2::_parse_header_stream_id(char &c) {
    this->parse_vars.i ++;
    this->parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    if (this->parse_vars.i == 9) {
        // STREAM ID

        // Reserved (1),
        // Stream Identifier (31)

        this->protocol.stream_id = static_cast<int>(parse_vars.buffint) & 0x7FFFFFFF; // 31
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        if (this->protocol.flag & HTTP2_FLAG_HEADERS_PRIORITY) {
            //deprecated
            this->parse_vars.i = 5; // skip 5 bytes

            this->current = [this, &capture0 = parse_vars.i](char & PH1) { this->_parse_skip_n_bytes(std::forward<decltype(PH1)>(PH1), capture0); };
            this->next = [this](char & PH1) { this->_parse_field_block(std::forward<decltype(PH1)>(PH1)); };
        }
        else {
            this->_parse_field_block (c);
        }
    }
}

void manapi::net::worker::http_v2::_parse_goaway_last_stream_id(char &c) {
    this->parse_vars.i++;
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.error.last_stream_id = static_cast<int>(this->parse_vars.buffint) & 0x7FFFFFFF;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = [this](char & PH1) { _parse_goaway_error_code(std::forward<decltype(PH1)>(PH1)); };
    }
}

void manapi::net::worker::http_v2::_parse_goaway_error_code(char &c) {
    this->parse_vars.i++;
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.error.errnum = static_cast<int>(this->parse_vars.buffint);
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = [this](char & PH1) { _parse_goaway_additional_debug_data(std::forward<decltype(PH1)>(PH1)); };
    }
}

void manapi::net::worker::http_v2::_parse_goaway_additional_debug_data(char &c) {
    this->protocol.error.errmsg += c;
}

void manapi::net::worker::http_v2::_parse_window_update_value(char &c) {
    this->parse_vars.i++;
    this->parse_vars.buffint = static_cast<size_t>((this->parse_vars.buffint << 8) | static_cast<unsigned char>(c));

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.value = static_cast<int>(this->parse_vars.buffint);

        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = nullptr;
    }
}

void manapi::net::worker::http_v2::_parse_rst_stream_action(char &c) {
    this->parse_vars.i++;
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (this->parse_vars.i == 4) {
        this->protocol.value = static_cast<int>(this->parse_vars.buffint);

        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;
        this->current = nullptr;
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
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);
    this->parse_vars.i ++;

    if (this->parse_vars.i == 2) {
        this->parse_vars.nkey = this->parse_vars.buffint;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        this->current = [this](char & PH1) { this->_parse_setting_value(std::forward<decltype(PH1)>(PH1)); };
    }
}

void manapi::net::worker::http_v2::_parse_setting_value(char &c) {
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);
    this->parse_vars.i ++;

    if (this->parse_vars.i == 4) {
        auto param = this->protocol.settings.find(static_cast<int>(this->parse_vars.nkey));
        if (param == this->protocol.settings.end()) {
            this->setting_param_was_ack(false);
        }
        else {
            auto &update = param->second.second;
            if (update != nullptr) { update(static_cast<int>(this->parse_vars.buffint), false); }
        }

        this->parse_vars.nkey = 0;
        this->parse_vars.buffint = 0;
        this->parse_vars.i = 0;

        this->current = [this](char & PH1) { this->_parse_setting_id(std::forward<decltype(PH1)>(PH1)); };
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
    const auto cutsize = std::min(static_cast<ssize_t>(this->buffer.size() - this->parse_vars.j), datasize);
    this->parse_vars.buffer += std::string_view(this->buffer.data() + this->parse_vars.j, cutsize);
    // +1  bcz in loop
    this->protocol.length = protocol.length - cutsize + 1;
    this->parse_vars.j += cutsize - 1;
    datasize = protocol.length - static_cast<ssize_t>(this->protocol.padding);

    if (datasize == 1 && protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
        if (!this->protocol.decoder.decode(this->parse_vars.buffer)) {
            this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "hpack: failed to decode headers");
            return;
        }
        this->parse_vars.buffer.clear();

        auto &session = this->sessions[this->protocol.stream_id];
        if (session.type == HTTP2_CONN_IDLE) {
            session.id = this->protocol.stream_id;
            session.type = HTTP2_CONN_OPEN;
        }
        else {
            this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "CONTINUATION frame instead of HEADERS Frame");
            return;
        }

        auto headers = this->protocol.decoder.headers();
        while (!headers.empty()) {
            auto it = headers.begin();
            auto value = std::move(it->second);
            auto node = headers.extract(it);
            session.headers.insert({std::move(node.key()), std::move(value)});
        }

        if (this->protocol.padding > 0) {
            this->current = [this, &capture0 = this->protocol.padding](char & PH1) { this->_parse_skip_n_bytes(std::forward<decltype(PH1)>(PH1), capture0); };
        }
    }

    else if (datasize < 1) {
        this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
        return;
    }
}

void manapi::net::worker::http_v2::_parse_body_data(char &c) {
    auto datasize = protocol.length - static_cast<ssize_t>(protocol.padding);
    //const auto cutsize = std::min(static_cast<ssize_t>(parse_vars.size - parse_vars.j), datasize);
    const auto cutsize = std::min(static_cast<ssize_t>(buffer.size() - parse_vars.j), datasize);
    parse_vars.buffer += std::string_view(buffer.data() + parse_vars.j, cutsize);
    // +1  bcz in loop
    protocol.length = protocol.length - cutsize + 1;
    parse_vars.j += cutsize - 1;

    datasize = protocol.length - static_cast<ssize_t>(protocol.padding);
    if (datasize == 1) {
        return;
    }

    if (datasize < 1) {
        this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
        return;
    }
}

void manapi::net::worker::http_v2::_parse_skip_n_bytes(char &c, size_t &n) {
    if (n <= 1) {
        n = 0;
        this->current = this->next;
        this->current (c);
        return;
    }
    n--;
}

void manapi::net::worker::http_v2::_parse_field_block(char &c) {
    // if protocol.length==1, then it means that the protocol data frame is empty
    const auto length = this->protocol.length - 1;
    switch (this->protocol.type) {
        case HTTP2_FRAME_HEADERS: {
            if (length <= 0) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "HEADERS Frame is empty");
                return;
            }

            if (protocol.stream_id == 0) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "0x0 reserved");
                return;
            }

            if ((!this->sessions.empty() && this->sessions.rbegin()->first >= this->protocol.stream_id) || (this->protocol.stream_id % 2 == 0)) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "unexpected stream id");
                return;
            }

            if (this->thread_cnt.load() >= this->protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_REFUSED_STREAM, std::format("max concurrent streams-{}", protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first.load()));
                return;
            }

            auto &session = this->sessions[protocol.stream_id];

            if (this->protocol.flag & HTTP2_FLAG_HEADERS_PADDED) {
                this->parse_vars.i = 1;
                this->protocol.padding = 0;
                this->current = [this](char & PH1) { this->_parse_number(std::forward<decltype(PH1)>(PH1), protocol.padding, parse_vars.i); };
                this->next = [this](char & PH1) { this->_parse_header_data(std::forward<decltype(PH1)>(PH1)); };
            }
            else {
                this->current = [this](char & PH1) { this->_parse_header_data(std::forward<decltype(PH1)>(PH1)); };
            }
            break;
        }
        case HTTP2_FRAME_CONTINUATION:
            if (this->protocol.stream_id == 0) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "0x0 reserved");
                return;
            }

            this->current = [this](char & PH1) { this->_parse_header_data(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_SETTINGS:
            // setting param size - 6 bytes
            if (length % 6 != 0) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "invalid len");
                return;
            }
            if (length > 0) {
                if (this->protocol.setting_param_acks > 0) {
                    this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "processing past settings so far");
                    return;
                }
                this->protocol.setting_param_acks.store(length / 6);
            }

            this->current = [this](char & PH1) { this->_parse_setting_id(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_GOAWAY:
            this->current = [this](char & PH1) { this->_parse_goaway_last_stream_id(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_WINDOW_UPDATE:
            this->current = [this](char & PH1) { this->_parse_window_update_value(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_RST_STREAM:
            this->current = [this](char & PH1) { this->_parse_rst_stream_action(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_PING:
            if (length != 8) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "frame ping: invalid payload length");
                return;
            }
            this->current = [this](char & PH1) { this->_parse_ping_data(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_DATA: {
            if (length<=0) {
                this->protocol.parse_exception = this->generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "DATA Frame is empty");
                return;
            }
            if (this->protocol.flag & HTTP2_FLAG_HEADERS_PADDED) {
                this->parse_vars.i = 1;
                this->protocol.padding = 0;
                this->current = [this](char & PH1) { this->_parse_number(std::forward<decltype(PH1)>(PH1), protocol.padding,  parse_vars.i); };
                this->next = [this](char & PH1) { this->_parse_body_data(std::forward<decltype(PH1)>(PH1)); };
            }
            else {
                this->current = [this](char & PH1) { this->_parse_body_data(std::forward<decltype(PH1)>(PH1)); };
            }
            break;
        }
        default:
            MANAPIHTTP_LOG("Undefined frame type: {}", this->protocol.type);
    }

    if (this->protocol.length > this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first + 1) {
        this->protocol.parse_exception = generate_error (HTTP2_ERROR_FLOW_CONTROL_ERROR, std::format("The frame size > {}", this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first.load()), this->protocol.stream_id);
        return;
    }
    if (this->protocol.type < HTTP2_FRAME_DATA || this->protocol.type > HTTP2_FRAME_PRIORITY_UPDATE) {
        this->parse_vars.i = this->protocol.length;
        this->current = [this](char &PH1) { this->_parse_skip_n_bytes(std::forward<decltype(PH1)>(PH1), this->parse_vars.i); };
    }
}

void manapi::net::worker::http_v2::_parse_number(char &c, size_t &num, size_t &length) {
    if (length == 0) {
        this->current = this->next;
        this->current (c);
        return;
    }
    num = (num << 8) | static_cast<unsigned char> (c);
    length--;
}

manapi::future<void> manapi::net::worker::http_v2::send_frame(http2_frame_type frame, uint8_t flag, int stream_id, std::string_view data) {
    //MANAPIHTTP_LOG("SEND FRAME: {}", (int)frame);
    if (frame != HTTP2_FRAME_PING) { this->protocol.current_timeout.store(this->protocol.timeout); }
    const std::string id = stringify_stream_id(stream_id);
    const std::string len = stringify_number <int> (static_cast<int>(data.size()));
    std::string response ({len[1], len[2], len[3], static_cast<char>(frame), static_cast<char> (flag), id[0], id[1], id[2], id[3]});
    response += data;
    if (frame == HTTP2_FRAME_GOAWAY) {
        std::cout << data.substr(8) << "\n";
    }
    if (co_await this->worker->write(*this->connection, response.data(), response.size(), false) <= 0) {
        co_await this->generate_error(HTTP2_ERROR_STREAM_CLOSED, "stream closed", static_cast<int>(stream_id));
    }
}

manapi::future<void> manapi::net::worker::http_v2::send_empty_frame(http2_frame_type frame, char flag, int stream_id) {
    co_await send_frame (frame, flag, stream_id, "");
}

manapi::future<> manapi::net::worker::http_v2::timer_watcher() {
    if (this->thread_cnt == 0 && (this->protocol.conn_type == 0) && std::chrono::system_clock::now() > this->protocol.prev_ping_time_point + this->protocol.ping_delay) {
        co_await send_ping_frame();
        this->protocol.prev_ping_time_point = std::chrono::system_clock::now();
    }

    protocol.current_timeout.fetch_sub(protocol.timer_interval);
    if (protocol.current_timeout <= 0) {
        MANAPIHTTP_LOG2("TIMEOUT HTTP2");
        co_await this->site.async_context()->timerpool()->async_remove_timer(this->ping_interval.exchange(0));
        MANAPIHTTP_LOG2("TIMEOUT HTTP2 2");
        co_await this->close_connection(HTTP2_ERROR_STREAM_CLOSED, "timeout", 0);
        MANAPIHTTP_LOG2("TIMEOUT HTTP2 3");
    }
}

manapi::future<void> manapi::net::worker::http_v2::send_ping_frame(std::string data) {
    char flag = 0x00;
    if (data.size()==8) {
        flag |= HTTP2_FLAG_PING_ACK;
        co_await send_frame(HTTP2_FRAME_PING, flag, 0, data);
    }
    else {
        {
            auto lk = co_await this->protocol.mx.lock_guard();
            if (this->protocol.pings.size() > 3) {
                // timeout
                lk.call();
                MANAPIHTTP_LOG2("PING IGNORE -> close connection");
                co_await this->worker->connection_close(this->connection, false);
                co_return;
            }

            do {
                data = string::random(8);
            } while (protocol.pings.contains(data));

            protocol.pings.insert(data);
        }
        co_await send_frame(HTTP2_FRAME_PING, flag, 0, data);
    }
}

manapi::future<void> manapi::net::worker::http_v2::close_connection(int errnum, std::string additional_data, int last_stream_id ) {
    if (this->protocol.conn_type & CONN_CLOSED) {
        co_return;
    }

    this->protocol.conn_type.fetch_or(CONN_CLOSED);
    co_await reset_all_streams();
    co_await this->empty_setting_timeouts();
    std::string data;
    data += stringify_number<int> (last_stream_id) + stringify_number<int>(errnum) + additional_data;
    co_await this->send_frame(HTTP2_FRAME_GOAWAY, 0x00, 0, data);
    co_await this->worker->connection_close(this->connection, true);
}

manapi::future<void> manapi::net::worker::http_v2::send_settings(const std::vector<std::pair<short, int>> &options) {
    auto handle = [this] (std::shared_ptr<http_v2> worker) -> future<void> {
        // timeout
        if (worker->protocol.conn_type & CONN_CLOSED) { co_return; }
        co_await generate_error(HTTP2_ERROR_SETTINGS_TIMEOUT, "recv settings timeout", 0);
    };

    this->protocol.setting_timeout.push(co_await this->site.async_context()->timerpool()->async_append_timer_async(std::chrono::milliseconds(1000), [handle = std::move(handle), worker = std::move(this->new_dependency ())] () mutable -> future<void> {
        co_await handle(std::move(worker));
    }));
    std::string data;
    for (const auto &option: options) {
        auto &update = protocol.settings[option.first].second;
        if (update != nullptr) { update(option.second, true); }
        data+=stringify_number<short>(option.first)+stringify_number<int>(option.second);
    }
    co_await send_frame (HTTP2_FRAME_SETTINGS, 0x0, 0, data);
}

manapi::future<ssize_t> manapi::net::worker::http_v2::send_data(int stream_id, const void *buf, ssize_t size, bool finish) {
    if (size == 0) {
        co_return size;
    }

    co_await this->protocol.window.write_cv->wait([this] () -> bool { return this->protocol.window.write.load() > 0 || this->protocol.conn_type & CONN_CLOSED; });
    auto sent = std::min(size, this->protocol.window.write.load());
    this->protocol.window.write.fetch_sub(sent);
    char cflag = 0x00;
    if (finish && sent == size) {
        cflag |= HTTP2_FLAG_DATA_END_STREAM;
    }
    co_await this->send_frame(HTTP2_FRAME_DATA, cflag, stream_id, std::string_view(static_cast<const char *> (buf), sent));
    if (finish && sent == size) {
        auto end = std::chrono::steady_clock::now();
        //printf("\nOperation took %ld milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    }
    co_return sent;
}

manapi::future<void> manapi::net::worker::http_v2::send_window_frame(int stream_id, int size) {
    if (this->protocol.window.read < size) {
        this->protocol.window.read.fetch_add(1e4);
        auto nsize = stringify_number <int> (size);
        co_await send_frame(HTTP2_FRAME_WINDOW_UPDATE, 0x00, 0, nsize);
    }
    this->protocol.window.read.fetch_sub(size);
    auto nsize = stringify_number <int> (size);
    co_await send_frame (HTTP2_FRAME_WINDOW_UPDATE, 0x00, stream_id, nsize);
    co_return;
}

manapi::future<void> manapi::net::worker::http_v2::default_ev_headers(int id, std::map<std::string, std::string> headers) {
    // think about it
    {
        auto lk = co_await this->threads_mutex.lock_guard();
        this->thread_cnt.fetch_add(1);
        this->threads.insert({id, {
            .id = id,
            .headers = std::move(headers),
            .rst = false,
            .write = std::make_shared<smart_w_buffer>(this->site.async_context()->taskpool(), [this, id](const void * buff, ssize_t size, bool finish) -> future<ssize_t> {
                const auto rhs = co_await this->send_data(id, buff, size, finish);
                co_return rhs;
            }, static_cast<size_t>(this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first), this->protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first),
            .read = std::make_shared<smart_r_buffer>(this->site.async_context()->taskpool(), [this, id](int size) -> future<void> {

                try {
                    co_await this->send_window_frame(id, size);
                }
                catch (std::exception const &e) {
                    MANAPIHTTP_LOG2(e.what());
                }
                co_return;
            }, this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first)
        }});
    }

    async::run(this->site.async_context(), worker::http_v2::session_worker(id, this->sessions[id].body, this->site, this->config, this->new_dependency()));

    co_return;
}

void manapi::net::worker::http_v2::default_ev_data(int id) {
    std::cout << "Data\n";
}

void manapi::net::worker::http_v2::default_ev_goaway(int last_stream_id, int errnum, std::string errmsg) {
    MANAPIHTTP_LOG("Client sent GOAWAY Frame:\n > Error Code: {}\n > Error Msg: {}\nLast-Stream-Id:{}",
        errnum, errmsg, this->protocol.error.last_stream_id);
}

void manapi::net::worker::http_v2::default_ev_finished(int id) {

}

void manapi::net::worker::http_v2::default_ev_priopity_update(int id, int prioritized_id, std::string prioritized_value) {

}

void manapi::net::worker::http_v2::default_ev_rst_stream(int id, int errnum) {
    // auto lk = co_await threads_mutex.lock_guard(); must be called before
    auto thread = this->threads.find(id);
    if(thread==this->threads.end()) {
        this->protocol.parse_exception = generate_error(HTTP2_ERROR_REFUSED_STREAM, "Stream not found");
        return;
    }
    thread->second.rst = true;
}

manapi::future<void> manapi::net::worker::http_v2::reset_all_streams() {
    auto lk = co_await this->threads_mutex.lock_guard();
    for (auto & thread : this->threads) {
        //co_await send_frame(HTTP2_FRAME_RST_STREAM, thread->first, this->protocol.error.errnum, this->protocol.error.errmsg);
        this->callbacks.rst_stream(thread.first, HTTP2_ERROR_NO_ERROR);
        co_await thread.second.write->disable();
        co_await thread.second.read->disable();
    }
    co_return;
}

manapi::future<> manapi::net::worker::http_v2::delete_stream_id(const int &id) {
    auto lk = co_await this->threads_mutex.lock_guard();
    if (this->threads.contains(id)) {
        this->threads.erase(id);
        this->thread_cnt.fetch_sub(1);
    }
    // unbind
    lk.call();

    co_await this->finishcv.notify_all();
}

manapi::future<> manapi::net::worker::http_v2::reset_stream(int id, int errnum) {
    co_await this->send_frame(HTTP2_FRAME_RST_STREAM, 0x0, id, stringify_number<int> (errnum));
}

manapi::future<void> manapi::net::worker::http_v2::session_worker(int id, bool body, net::site &site, std::shared_ptr<http::config> config, std::shared_ptr<worker::http_v2> worker) {

    int stream_errnum = HTTP2_ERROR_NO_ERROR;

    try {
        http::http_v2 client (worker, config, site);
        client.connection = std::make_shared<worker::connection>(new int (id),
            [] (void *ptr) -> void { delete static_cast<int *> (ptr); });
        client.connection->version = manapi::net::http::versions::HTTP_v2;

        {
            auto lk = co_await worker->threads_mutex.lock_guard();
            auto it = worker->threads.find(id);
            if (it == worker->threads.end()) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Failed to find session thread data by id"); }
            client.request_data.headers = std::move(it->second.headers);
            co_await it->second.write->resize (65535);
        }
        client.request_data.body_index = 0;
        client.request_data.uri = client.request_data.headers[":path"];
        client.request_data.headers_size = 0;
        client.request_data.divided = -1;
        client.request_data.http = "HTTP/2";
        client.request_data.has_body = body;
        client.request_data.body_left = 0;
        client.request_data.buffer.resize(config->get_socket_block_size());
        if (body) {
            auto contentlength = client.request_data.headers.find(HTTP_HEADER.CONTENT_LENGTH);
            client.request_data.headers_part = 0;
            client.request_data.body_part = 0;
            client.request_data.body_size = contentlength != client.request_data.headers.end() ? std::stoll(contentlength->second) : 0;
            client.request_data.body_ptr = client.request_data.buffer.data();
        }
        else {
            client.request_data.body_size = 0;
            client.request_data.body_part = 0;
            client.request_data.body_ptr = nullptr;
        }
        client.request_data.body_left = client.request_data.body_size;
        client.request_data.method = client.request_data.headers[":method"];

        co_await client.parse_request(0,0);
        co_await client.execute_handler();

    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("session_worker(...): {}", e.what());
        stream_errnum = HTTP2_ERROR_INTERNAL_ERROR;
    }

    if (stream_errnum != HTTP2_ERROR_NO_ERROR) {
        // an error occurred
        co_await worker->reset_stream (id, stream_errnum);
    }

    co_await worker->delete_stream_id (id);

    worker.reset();
}

manapi::future<ssize_t> manapi::net::worker::http_v2::default_read(worker::connection &connection, void *buff, const size_t &size) {
    std::shared_ptr<smart_r_buffer> worker;

    {
        auto lk = co_await threads_mutex.lock_guard();
        auto session = threads.find(connection.as<int>());
        if (session == threads.end()) { co_return -1; }
        if (session->second.rst || (this->protocol.conn_type & CONN_CLOSED)) { co_return -1; }

        worker = session->second.read;
    }

    const auto rhs = co_await worker->read(buff, size);
    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::http_v2::default_write(worker::connection &connection, const void *buff, const size_t &size, bool flag) {
    std::shared_ptr<smart_w_buffer> worker;
    {
        auto lk = co_await threads_mutex.lock_guard();
        auto session = threads.find(connection.as<int>());
        if (session == threads.end()) { co_return -1; }
        if (session->second.rst || (this->protocol.conn_type & CONN_CLOSED)) { co_return -1; }
        worker = session->second.write;
    }

    co_return static_cast<ssize_t>(co_await worker->add(buff, size, flag));
}

std::string manapi::net::worker::http_v2::stringify_stream_id(int stream_id) {
    std::string id;
    id += static_cast<char> ((stream_id >> (8 * 3)) & 0x7F); // 127
    for (int i = 2; i >= 0; i--) {
        id += static_cast<char> ((stream_id >> i * 8) & 0xFF); // 256
    }
    return std::move(id);
}

void manapi::net::worker::http_v2::setting_param_was_ack(const bool &self) {
    if (self && this->protocol.setting_param_acks == 0) {
        return;
    }

    async::run(this->site.async_context(), [worker = this->new_dependency()] () mutable -> future<> {
        auto lk = co_await worker->protocol.setting_param_acks_mx->lock_guard();
        if (worker->protocol.setting_param_acks == 0) {
            co_return;
        }

        if ((worker->protocol.setting_param_acks.fetch_sub(1)) == 1) {
            co_await worker->send_frame (HTTP2_FRAME_SETTINGS, HTTP2_FLAG_SETTINGS_ACK, 0, {});
        }
    });
}

void manapi::net::worker::http_v2::settings_update_initial_window_size(int value) {
    this->protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first.store(value);
}

void manapi::net::worker::http_v2::settings_update_max_concurrent_streams(int value) {
    this->protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first.store(value);
}

void manapi::net::worker::http_v2::setting_value_valid(const http2_setting_type &type, const int &value) noexcept(false) {
    if (manapi::net::worker::http_v2::allow_settings[type].valid(value)) {
        return;
    }

    this->protocol.parse_exception = generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid http2 setting param");
}

void manapi::net::worker::http_v2::_parse_header_flag (char &c) {
    this->parse_vars.i ++;
    this->parse_vars.buffint = (this->parse_vars.buffint << 8) | static_cast<unsigned char>(c);

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

        this->current = [this](char & PH1) { _parse_header_stream_id(std::forward<decltype(PH1)>(PH1)); };
    }
}
