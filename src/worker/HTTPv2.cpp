#include "worker/HTTPv2.hpp"

#include <http/HTTPv2.hpp>

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

manapi::net::worker::http_v2::http_v2(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site) : base(site), worker(std::move(worker)) {
    this->config = std::move(config);
    this->init_settings();
    this->set_callbacks ({
        .headers = std::bind(&http_v2::default_ev_headers, this, std::placeholders::_1, std::placeholders::_2),
        .data = std::bind(&http_v2::default_ev_data, this, std::placeholders::_1),
        .goaway = std::bind(&http_v2::default_ev_goaway, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        .priority_update = std::bind(&http_v2::default_ev_priopity_update, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        .rst_stream = std::bind(&http_v2::default_ev_rst_stream, this, std::placeholders::_1, std::placeholders::_2),
        .finished = std::bind(&http_v2::default_ev_finished, this, std::placeholders::_1)
    });
    this->threads = std::make_shared<connections_storage <int, http_v2_thread_data_t>>();
    this->read = std::bind(&http_v2::default_read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    this->write = std::bind(&http_v2::default_write, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
}

manapi::net::worker::http_v2::~http_v2() {

    //std::cout << "Unmounted\n";
}

manapi::net::future<void> manapi::net::worker::http_v2::parse_request(ssize_t j, ssize_t size) {
    std::unique_lock <std::mutex> lk (finishmx);

    buffer.resize(protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first);
    current = std::bind(&http_v2::_next_line, this, std::placeholders::_1);
    next = std::bind(&http_v2::_skip_sm_msg, this, std::placeholders::_1);
    parse_vars.j = j;
    parse_vars.size = size;
    ssize_t rhs=-1;

    try {
        ping_interval = site.append_interval(std::chrono::milliseconds (protocol.ping_interval), [this] () -> void {
            auto future = send_ping_frame();
            future.get();
        });

        goto skip;
        while (!protocol.closed) {
            rhs = co_await worker->read (*connection, buffer.data(), buffer.size());

            if (rhs <= 0) {
                co_await close_connection(HTTP2_ERROR_PROTOCOL_ERROR, "timeout");
                break;
            }

            parse_vars.size = rhs;

            parse_vars.j = 0;

            skip: for (; parse_vars.j < parse_vars.size && protocol.length > 0; parse_vars.j++, protocol.length--) {
                current (buffer[parse_vars.j]);
            }

            if (protocol.length < 0) {
                protocol.length = 0;
            }
            if (protocol.length == 0) {
                if (protocol.type == HTTP2_FRAME_PING) {
                    protocol.current_timeout -= protocol.ping_interval;
                    if (protocol.current_timeout <= 0) {
                        co_await close_connection (protocol.error.errnum, protocol.error.errmsg, protocol.error.last_stream_id);
                        break;
                    }
                }
                else {
                    protocol.current_timeout = protocol.timeout;
                }

                if (protocol.initial_frame) {
                    co_await send_settings ({
                        {HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, 1},
                        {HTTP2_SETTING_ENABLE_PUSH, 0},
                        {HTTP2_SETTING_MAX_CONCURRENT_STREAMS, 100},
                        {HTTP2_SETTING_MAX_HEADER_LIST_SIZE,  65000}
                    });

                    protocol.initial_frame = false;
                }

                MANAPIHTTP_LOG("FRAME TYPE: {}", protocol.type);

                switch (protocol.type) {
                    case HTTP2_FRAME_SETTINGS: {
                        if (protocol.flag & HTTP2_FLAG_SETTINGS_ACK) {
                            // settings were accepted
                            site.remove_timer(protocol.setting_timeout.front());
                            protocol.setting_timeout.pop();
                        }
                        else {
                            co_await send_frame (HTTP2_FRAME_SETTINGS, HTTP2_FLAG_SETTINGS_ACK, 0, {});
                        }
                        break;
                    }
                    case HTTP2_FRAME_HEADERS:
                    case HTTP2_FRAME_CONTINUATION: {
                        auto session = sessions.find(protocol.stream_id);
                        if (session == sessions.end()) { break; }
                        if (session->second.type == HTTP2_CONN_HALF_CLOSED_REMOTE) {
                            generate_error(HTTP2_ERROR_STREAM_CLOSED, "Invalid frame was received to half-closed(remote)", protocol.stream_id);
                        }
                        if (protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
                            session->second.body = ! (protocol.flag & HTTP2_FLAG_HEADERS_END_STREAM);
                            callbacks.headers(session->second.id, std::move(session->second.headers));
                            session->second.type = HTTP2_CONN_HALF_CLOSED_REMOTE;

                            if (protocol.flag & HTTP2_FLAG_HEADERS_END_STREAM) {
                                callbacks.finished(session->second.id);
                                sessions.erase(session);
                                session->second.type = HTTP2_CONN_CLOSED;
                            }
                            else {
                                session->second.body = true;
                                callbacks.data (session->second.id);
                            }
                        }

                        break;
                    }
                    case HTTP2_FRAME_DATA: {
                        auto session = sessions.find(protocol.stream_id);
                        if (session == sessions.end()) { break; }

                        if (protocol.flag & HTTP2_FLAG_DATA_END_STREAM) {
                            callbacks.finished (session->second.id);
                            sessions.erase(session);
                            session->second.type = HTTP2_CONN_CLOSED;
                        }
                        break;
                    }
                    case HTTP2_FRAME_GOAWAY: {
                        callbacks.goaway (protocol.error.last_stream_id, protocol.error.errnum, std::move(protocol.error.errmsg));
                        protocol.error.errnum = 0;
                        protocol.error.last_stream_id = 0;
                        protocol.closed = true;
                        break;
                    }
                    case HTTP2_FRAME_RST_STREAM: {
                        auto thread = threads->get(protocol.stream_id);
                        if (thread == nullptr) { break; }
                        callbacks.rst_stream (thread->id, protocol.value);
                        protocol.value = 0;
                        break;
                    }
                    case HTTP2_FRAME_WINDOW_UPDATE: {
                        if (protocol.stream_id == 0) {
                            // global
                            std::lock_guard<std::mutex> lkwt (protocol.window.writemx);
                            protocol.window.write += protocol.value;
                            break;
                        }
                        auto thread = threads->get(protocol.stream_id);
                        if (thread == nullptr) { break; }
                        //MANAPIHTTP_LOG ("window frame пришел ура праздник 🎉🎉🎉 {} {} {}", protocol.value, protocol.stream_id, protocol.flag);
                        thread->write.add_allow_to_sent(protocol.value);
                        protocol.value = 0;
                        break;
                    }
                    default: {
                        //MANAPIHTTP_LOG("frame type: {}", protocol.type);
                        //protocol.closed = true;
                    }
                }

                if (!protocol.closed) {
                    current = std::bind(&http_v2::_parse_header_length, this, std::placeholders::_1);
                    protocol.length = HEADER_DEFAULT_SIZE;
                    parse_vars.i = 0;
                    parse_vars.buffint = 0;
                    //goto skip;
                    if (parse_vars.j < parse_vars.size) {
                        goto skip;
                    }
                }
            }
        }
    }
    catch (manapi::net::utils::exception const &e) {
        MANAPIHTTP_LOG("[{}]: HTTP2 Exception: {}", static_cast<size_t>(e.get_err_num()), e.what());

    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("HTTP2 Exception: {}", e.what());
    }

    protocol.closed = true;
    protocol.window.writecv.notify_all();

    reset_all_streams();
    site.remove_timer(ping_interval);
    //std::cout << "Preparing for close\n";
    finishcv.wait(lk, [this] () -> bool { return threads->empty(); });
    //std::cout << "Closing...\n";
}

void manapi::net::worker::http_v2::init_settings() {
    protocol.settings[HTTP2_SETTING_HEADER_TABLE_SIZE] = {4096, [this] (int value) -> void {
        setting_value_valid (HTTP2_SETTING_HEADER_TABLE_SIZE, value);
        protocol.settings[HTTP2_SETTING_HEADER_TABLE_SIZE].first = value;
        this->protocol.decoder.m_dynamic_max(value);
        this->protocol.decoder.m_dynamic_max(value);
    }};
    protocol.settings[HTTP2_SETTING_ENABLE_PUSH] = {1, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_ENABLE_PUSH, value);
    }};
    protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS] = {INT_MAX, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_ENABLE_PUSH, value);
    }};
    protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE] = {65535, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_INITIAL_WINDOW_SIZE, value);
        protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first = value;
    }};
    protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE] = {16384, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_MAX_FRAME_SIZE, value);
        protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first = value;
        buffer.resize(value);
    }};
    protocol.settings[HTTP2_SETTING_MAX_HEADER_LIST_SIZE] = {INT_MAX, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_MAX_HEADER_LIST_SIZE, value);
    }};
    protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL] = {0, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL, value);
    }};
    protocol.settings[HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES] = {0, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES, value);
    }};
    protocol.settings[HTTP2_SETTING_TLS_RENEG_PERMITTED] = {0x00, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_TLS_RENEG_PERMITTED, value);
    }};
    protocol.settings[HTTP2_SETTING_SETTINGS_ENABLE_METADATA] = {0, [this] (int value) -> void {
        setting_value_valid(HTTP2_SETTING_SETTINGS_ENABLE_METADATA, value);
    }};
}

void manapi::net::worker::http_v2::set_callbacks(const http_v2_callbacks_t &callbacks) {
    this->callbacks = callbacks;
}

manapi::net::future<ssize_t> manapi::net::worker::http_v2::response(worker::connection &connection, http_response &resp, bool finish) {
    manapi::net::utils::compress::hpack::encoder_t  encoder;
    encoder.add (utils::compress::hpack::header_t(":status", std::to_string(resp.get_status_code())));
    for (const auto &header: resp.get_headers()) {
        encoder.add (utils::compress::hpack::header_t(header.first, header.second));
    }
    uint8_t cflag = 0x0;
    const auto data = encoder.data();
    size_t cnt = 0;
    auto frameSize = static_cast<size_t>(protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first);
    http2_frame_type ft = HTTP2_FRAME_HEADERS;
    goto skip;
    while (cnt < data.size()) {
        ft = HTTP2_FRAME_CONTINUATION;
        skip:
        auto left = std::min(frameSize, data.size() - cnt);
        if (left != frameSize) {
            cflag |= HTTP2_FLAG_HEADERS_END_HEADERS;
            if (finish) { cflag |= HTTP2_FLAG_HEADERS_END_STREAM; }
        }
        co_await send_frame(ft, cflag, connection.as<uint32_t>(), std::string_view(data.data() + cnt, left));
        cnt += left;
    }
    co_return static_cast<ssize_t>(data.size());
}

void manapi::net::worker::http_v2::generate_error(http2_error_type errnum, std::string errmsg, int last_stream_id) noexcept(false) {
    protocol.error.errnum = errnum;
    protocol.error.errmsg = std::move(errmsg);
    protocol.error.last_stream_id = last_stream_id;
    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, protocol.error.errmsg);
}

void manapi::net::worker::http_v2::_skip_sm_msg(char &c) {
    parse_vars.buffer += c;
    if (parse_vars.buffer.size() == 4) {
        if (parse_vars.buffer != "SM\r\n") {
            generate_error (HTTP2_ERROR_PROTOCOL_ERROR, "SM label is invalid");
        }
        parse_vars.buffer.clear();
        next = std::bind(&http_v2::_parse_header_octets, this, std::placeholders::_1);
        current = std::bind(&http_v2::_next_line, this, std::placeholders::_1);
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

    generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid next line: \\r\\n");
}

void manapi::net::worker::http_v2::_skip_null_octet(char &c) {
    if (c == '\0') {
        current = next;
        return;
    }
    current = next;
    current(c);
    MANAPIHTTP_LOG2("Invalid symbol");
}

void manapi::net::worker::http_v2::_parse_header_octets(char &c) {
    parse_vars.i = 0;
    current = std::bind(&http_v2::_parse_header_length, this, std::placeholders::_1);
    current(c);
}

void manapi::net::worker::http_v2::_parse_header_length(char &c) {
    parse_vars.i++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 0xXXXXXX
    if (parse_vars.i == 3) {
        protocol.length += parse_vars.buffint;
        parse_vars.buffint = 0;
        current = std::bind(&http_v2::_parse_header_type, this, std::placeholders::_1);
    }
}

void manapi::net::worker::http_v2::_parse_header_type(char &c) {
    parse_vars.i++;
    protocol.type = static_cast<int>(c);
    current = std::bind(&http_v2::_parse_header_flag , this, std::placeholders::_1);
}

void manapi::net::worker::http_v2::_parse_header_stream_id(char &c) {
    parse_vars.i ++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    if (parse_vars.i == 9) {
        // STREAM ID

        // Reserved (1),
        // Stream Identifier (31)

        protocol.stream_id = parse_vars.buffint & 0x7FFFFFFF; // 31
        parse_vars.buffint = 0;
        parse_vars.i = 0;

        if (protocol.flag & HTTP2_FLAG_HEADERS_PRIORITY) {
            //deprecated
            parse_vars.i = 5; // skip 5 bytes

            current = std::bind(&http_v2::_parse_skip_n_bytes, this, std::placeholders::_1, parse_vars.i);
            next = std::bind(&http_v2::_parse_field_block, this, std::placeholders::_1);
        }
        else {
            _parse_field_block (c);
        }
    }
}

void manapi::net::worker::http_v2::_parse_goaway_last_stream_id(char &c) {
    parse_vars.i++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (parse_vars.i == 4) {
        protocol.error.last_stream_id = parse_vars.buffint & 0x7FFFFFFF;
        parse_vars.buffint = 0;
        parse_vars.i = 0;
        current = std::bind(&http_v2::_parse_goaway_error_code, this, std::placeholders::_1);
    }
}

void manapi::net::worker::http_v2::_parse_goaway_error_code(char &c) {
    parse_vars.i++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (parse_vars.i == 4) {
        protocol.error.errnum = parse_vars.buffint;
        parse_vars.buffint = 0;
        parse_vars.i = 0;
        current = std::bind(&http_v2::_parse_goaway_additional_debug_data, this, std::placeholders::_1);
    }
}

void manapi::net::worker::http_v2::_parse_goaway_additional_debug_data(char &c) {
    protocol.error.errmsg += c;
}

void manapi::net::worker::http_v2::_parse_window_update_value(char &c) {
    parse_vars.i++;
    parse_vars.buffint = static_cast<size_t>((parse_vars.buffint << 8) | static_cast<unsigned char>(c));

    // 4 bytes
    if (parse_vars.i == 4) {
        protocol.value = static_cast<int>(parse_vars.buffint);

        parse_vars.buffint = 0;
        parse_vars.i = 0;
        current = nullptr;
    }
}

void manapi::net::worker::http_v2::_parse_rst_stream_action(char &c) {
    parse_vars.i++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    // 4 bytes
    if (parse_vars.i == 4) {
        protocol.value = parse_vars.buffint;

        parse_vars.buffint = 0;
        parse_vars.i = 0;
        current = nullptr;
    }
}

void manapi::net::worker::http_v2::_parse_ping_data(char &c) {
    parse_vars.i++;
    parse_vars.buffer += c;
    if (parse_vars.i == 8) {
        auto it = protocol.pings.find(parse_vars.buffer);
        if (it == protocol.pings.end()) {
            generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "ping frame: ping data is invalid");
        }
        protocol.pings.erase(it);

        parse_vars.buffer.clear();
        parse_vars.i = 0;
    }
}

void manapi::net::worker::http_v2::_parse_setting_id(char &c) {
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);
    parse_vars.i ++;

    if (parse_vars.i == 2) {
        parse_vars.nkey = parse_vars.buffint;
        parse_vars.buffint = 0;
        parse_vars.i = 0;

        current = std::bind(&http_v2::_parse_setting_value , this, std::placeholders::_1);
    }
}

void manapi::net::worker::http_v2::_parse_setting_value(char &c) {
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);
    parse_vars.i ++;

    if (parse_vars.i == 4) {
        auto param = protocol.settings.find(parse_vars.nkey);
        if (param != protocol.settings.end()) {
            auto &update = param->second.second;
            if (update != nullptr) { update(parse_vars.buffint); }
        }

        parse_vars.nkey = 0;
        parse_vars.buffint = 0;
        parse_vars.i = 0;

        current = std::bind(&http_v2::_parse_setting_id , this, std::placeholders::_1);
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
    auto datasize = protocol.length - static_cast<ssize_t>(protocol.padding);
    const auto cutsize = std::min(static_cast<ssize_t>(buffer.size() - parse_vars.j), datasize);
    parse_vars.buffer += std::string_view(buffer.data() + parse_vars.j, cutsize);
    // +1  bcz in loop
    protocol.length = protocol.length - cutsize + 1;
    parse_vars.j += cutsize - 1;
    datasize = protocol.length - static_cast<ssize_t>(protocol.padding);
    if (datasize == 1 && protocol.flag & HTTP2_FLAG_HEADERS_END_HEADERS) {
        if (!protocol.decoder.decode(parse_vars.buffer)) {
            generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "hpack: failed to decode headers");
        }
        parse_vars.buffer.clear();
        if (protocol.stream_id >= protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first) {
            generate_error(HTTP2_ERROR_REFUSED_STREAM, std::format("max stream id must be less that {}", protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first));
        }
        auto &session = sessions[protocol.stream_id];
        if (session.type == HTTP2_CONN_IDLE) {
            session.id = protocol.stream_id;
            session.type = HTTP2_CONN_OPEN;
        }
        else {
            generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "CONTINUATION frame instead of HEADERS Frame");
        }

        auto headers = protocol.decoder.headers();
        while (!headers.empty()) {
            auto it = headers.begin();
            auto value = std::move(it->second);
            auto node = headers.extract(it);
            session.headers.insert({std::move(node.key()), std::move(value)});
        }

        if (protocol.padding > 0) {
            current = std::bind(&http_v2::_parse_skip_n_bytes, this, std::placeholders::_1, protocol.padding);
        }
    }

    else if (datasize < 1) {
        generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
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
        auto thread = threads->get(protocol.stream_id);
        if (thread == nullptr) {
            generate_error(HTTP2_ERROR_REFUSED_STREAM, std::format("stream {} doesn't exists", protocol.stream_id));
        }
        thread->read.add(parse_vars.buffer.data(), parse_vars.buffer.size(), protocol.flag & HTTP2_FLAG_DATA_END_STREAM);
        parse_vars.buffer.clear();
    }

    else if (datasize < 1) {
        generate_error(HTTP2_ERROR_PROTOCOL_ERROR, std::format("protocol.length - protocol.padding < 0. ({})", datasize));
    }
}

void manapi::net::worker::http_v2::_parse_skip_n_bytes(char &c, size_t &n) {
    if (n <= 1) {
        n = 0;
        current = next;
        current (c);
        return;
    }
    n--;
}

void manapi::net::worker::http_v2::_parse_field_block(char &c) {
    // if protocol.length==1, then it says that protocol frame data is empty
    const auto length = protocol.length - 1;

    switch (protocol.type) {
        case HTTP2_FRAME_HEADERS: {
            if (length <= 0) {
                generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "HEADERS Frame is empty");
            }
            if (protocol.flag & HTTP2_FLAG_HEADERS_PADDED) {
                parse_vars.i = 1;
                protocol.padding = 0;
                current = [this](auto && PH1) { _parse_number(std::forward<decltype(PH1)>(PH1), protocol.padding, parse_vars.i); };
                next = [this](auto && PH1) { _parse_header_data(std::forward<decltype(PH1)>(PH1)); };
            }
            else {
                current = [this](auto && PH1) { _parse_header_data(std::forward<decltype(PH1)>(PH1)); };
            }
            break;
        }
        case HTTP2_FRAME_CONTINUATION:
            current = [this](auto && PH1) { _parse_header_data(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_SETTINGS:
            if (length<=0) { break; }
            current = [this](auto && PH1) { _parse_setting_id(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_GOAWAY:
            current = [this](auto && PH1) { _parse_goaway_last_stream_id(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_WINDOW_UPDATE:
            current = [this](auto && PH1) { _parse_window_update_value(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_RST_STREAM:
            current = [this](auto && PH1) { _parse_rst_stream_action(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_PING:
            if (length != 8) { generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "frame ping: invalid payload length"); }
            current = [this](auto && PH1) { _parse_ping_data(std::forward<decltype(PH1)>(PH1)); };
        break;
        case HTTP2_FRAME_DATA: {
            if (length<=0) {
                generate_error(HTTP2_ERROR_FRAME_SIZE_ERROR, "DATA Frame is empty");
            }
            if (protocol.flag & HTTP2_FLAG_HEADERS_PADDED) {
                parse_vars.i = 1;
                protocol.padding = 0;
                current = [this](auto && PH1) { _parse_number(std::forward<decltype(PH1)>(PH1), protocol.padding,  parse_vars.i); };
                next = [this](auto && PH1) { _parse_body_data(std::forward<decltype(PH1)>(PH1)); };
            }
            else {
                current = [this](auto && PH1) { _parse_body_data(std::forward<decltype(PH1)>(PH1)); };
            }
            break;
        }
        default:
            MANAPIHTTP_LOG("Undefined frame type: {}", protocol.type);
    }

    if (this->protocol.length > protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first + 1) {
        generate_error (HTTP2_ERROR_FLOW_CONTROL_ERROR, std::format("The frame size > {}", protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first));
    }
    if (this->protocol.type < HTTP2_FRAME_DATA || this->protocol.type > HTTP2_FRAME_PRIORITY_UPDATE) {
        generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "The frame type is invalid");
    }
}

void manapi::net::worker::http_v2::_parse_number(char &c, size_t &num, size_t &length) {
    if (length == 0) {
        current = next;
        current (c);
        return;
    }
    num = (num << 8) | static_cast<unsigned char> (c);
    length--;
}

manapi::net::future<void> manapi::net::worker::http_v2::send_frame(http2_frame_type frame, uint8_t flag, uint32_t stream_id, std::string_view data) {
    if (frame != HTTP2_FRAME_PING) { protocol.current_timeout = protocol.timeout; }
    const std::string id = stringify_stream_id(stream_id);
    const std::string len = stringify_number <int> (data.size());
    std::string response ({len[1], len[2], len[3], static_cast<char>(frame), static_cast<char> (flag), id[0], id[1], id[2], id[3]});
    response += data;
    co_await worker->write(*connection, response.data(), response.size(), false);
}

manapi::net::future<void> manapi::net::worker::http_v2::send_empty_frame(http2_frame_type frame, char flag, int stream_id) {
    co_await send_frame (frame, flag, stream_id, "");
}

manapi::net::future<void> manapi::net::worker::http_v2::send_ping_frame(std::string data) {
    char flag = 0x00;
    if (data.size()==8) {
        flag |= HTTP2_FLAG_PING_ACK;
        co_await send_frame(HTTP2_FRAME_PING, flag, 0, data);
    }
    else {
        if (protocol.pings.size() > 3) {
            // timeout
            protocol.closed = true;
        }

        do {
            data = utils::random_string(8);
        } while (protocol.pings.contains(data));

        const auto it = protocol.pings.insert(std::move(data)).first;
        co_await send_frame(HTTP2_FRAME_PING, flag, 0, *it);
    }
}

manapi::net::future<void> manapi::net::worker::http_v2::close_connection(int errnum, std::string additional_data, int last_stream_id ) {
    {
        auto lk = threads->large_request();
        for (auto thread = threads->begin(); thread != threads->end(); ++thread) {
            thread->second.rst = true;
            thread->second.read.disable();
            thread->second.write.disable();
        }
    }

    std::string data;
    data += stringify_number<int> (last_stream_id) + stringify_number<int>(errnum) + additional_data;
    co_await send_frame(HTTP2_FRAME_GOAWAY, 0x00, 0, data);
}

manapi::net::future<void> manapi::net::worker::http_v2::send_settings(const std::vector<std::pair<short, int>> &options) {
    protocol.setting_timeout.push(site.append_timer(std::chrono::milliseconds(1000), [worker = std::move(new_dependency ())] () -> future<void> {
        std::lock_guard<std::mutex> lk (worker->protocol.mx);
        // timeout
        if (worker->protocol.closed) { co_return; }
        worker->protocol.closed = true;
        co_await worker->close_connection(HTTP2_ERROR_SETTINGS_TIMEOUT, "recv settings timeout");
    }));
    std::string data;
    for (const auto &option: options) {
        auto &update = protocol.settings[option.first].second;
        if (update != nullptr) { update(option.second); }
        data+=stringify_number<short>(option.first)+stringify_number<int>(option.second);
    }
    co_await send_frame (HTTP2_FRAME_SETTINGS, 0x0, 0, data);
}

manapi::net::future<ssize_t> manapi::net::worker::http_v2::send_data(int stream_id, const void *buf, ssize_t size, bool finish) {
    if (size == 0) {
        co_return size;
    }

    std::unique_lock<std::mutex> lk (protocol.window.writemx);
    protocol.window.writecv.wait(lk, [this, &size] () -> bool { return protocol.window.write >= size || protocol.closed; });
    protocol.window.write -= static_cast<int> (size);
    char cflag = 0x00;
    if (finish) { cflag |= HTTP2_FLAG_DATA_END_STREAM; }
    co_await send_frame(HTTP2_FRAME_DATA, cflag, stream_id, std::string_view(static_cast<const char *> (buf), size));
    co_return size;
}

manapi::net::future<void> manapi::net::worker::http_v2::send_window_frame(int stream_id, int size) {
    std::lock_guard<std::mutex> lk (protocol.window.readmx);
    if (protocol.window.read < size) {
        protocol.window.read += 1e6;
        auto nsize = stringify_number <int> (protocol.window.read);
        co_await send_frame(HTTP2_FRAME_WINDOW_UPDATE, 0x00, 0, nsize);
    }
    protocol.window.read -= size;
    auto nsize = stringify_number <int> (size);
    co_await send_frame (HTTP2_FRAME_WINDOW_UPDATE, 0x00, stream_id, nsize);
}

void manapi::net::worker::http_v2::default_ev_headers(int id, std::map<std::string, std::string> headers) {
    // think about it
    threads->insert({id, {
        .id = id,
        .headers = std::move(headers),
        .rst = false,
        .write = {[this, id](auto PH1, auto PH2, auto PH3) -> future<ssize_t> { const auto rhs = co_await send_data(id, PH1, PH2, PH3); co_return rhs; },
            static_cast<size_t>(protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first), protocol.settings[HTTP2_SETTING_MAX_FRAME_SIZE].first},
        .read = {[this, id](auto PH1) -> future<void> { co_await send_window_frame(id, PH1); co_return; }, 100000}
    }});

    site.taskspool->append_task(std::make_unique<manapi::net::function_task>([headers = std::move(headers), id = id, body = sessions[id].body, &site = site, config = config, worker = new_dependency()] () -> void {
        worker::http_v2::session_worker(id, body, site, config, worker);
    }));
}

void manapi::net::worker::http_v2::default_ev_data(int id) {
    std::cout << "Data\n";
}

void manapi::net::worker::http_v2::default_ev_goaway(int last_stream_id, int errnum, std::string errmsg) {
    MANAPIHTTP_LOG("Client sent GOAWAY Frame:\n > Error Code: {}\n > Error Msg: {}\nLast-Stream-Id:{}",
        errnum, errmsg, protocol.error.last_stream_id);
}

void manapi::net::worker::http_v2::default_ev_finished(int id) {

}

void manapi::net::worker::http_v2::default_ev_priopity_update(int id, int prioritized_id, std::string prioritized_value) {

}

void manapi::net::worker::http_v2::default_ev_rst_stream(int id, int errnum) {
    auto thread = threads->find(id);
    if(thread==threads->end()) { generate_error(HTTP2_ERROR_REFUSED_STREAM, "Stream not found"); }
    thread->second.rst = true;
    thread->second.write.disable();
    thread->second.read.disable();
}

void manapi::net::worker::http_v2::reset_all_streams() {
    auto lk = threads->large_request();
    for (auto thread = threads->begin(); thread != threads->end(); ++thread) {
        default_ev_rst_stream(thread->first, HTTP2_ERROR_NO_ERROR);
    }
}

void manapi::net::worker::http_v2::session_worker(int id, bool body, net::site &site, std::shared_ptr<http::config> config, std::shared_ptr<worker::http_v2> worker) {
    try {
        http::http_v2 client (worker, config, site);
        client.connection = std::make_shared<worker::connection>(new size_t (id),
            [] (void *ptr) -> void { delete static_cast<size_t *> (ptr); });

        {
            auto it = worker->threads->find(id);
            if (it == worker->threads->end()) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Failed to find session thread data by id"); }
            client.request_data.headers = std::move(it->second.headers);
            it->second.write.resize (65535);
        }
        client.request_data.body_index = 0;
        client.request_data.uri = client.request_data.headers[":path"];
        client.request_data.headers_size = 0;
        client.request_data.divided = -1;
        client.request_data.http = "HTTP/2.0";
        client.request_data.has_body = body;
        client.request_data.body_left = 0;
        client.request_data.buffer.resize(*config->get_socket_block_size());
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

        auto a = client.parse_request(0,0);
        auto b = client.execute_handler();
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("session_worker(...): {}", e.what());
    }

    {
        if (worker->threads->contains(id)) {
            worker->threads->erase(id);
            worker->finishcv.notify_all();
        }
        else {
            MANAPIHTTP_LOG2("Failed to find session thread data by id");
        }
    }
}

manapi::net::future<ssize_t> manapi::net::worker::http_v2::default_read(worker::connection &connection, void *buff, const size_t &size) {
    smart_r_buffer *worker;
    {
        auto session = threads->get(connection.as<size_t>());
        if (session == nullptr) { co_return -1; }
        if (session->rst) { co_return -1; }
        worker = &session->read;
    }
    const auto rhs = co_await worker->read(buff, size);
    co_return rhs;
}

manapi::net::future<ssize_t> manapi::net::worker::http_v2::default_write(worker::connection &connection, const void *buff, const size_t &size, bool flag) {
    smart_w_buffer *worker;
    {
        auto session = threads->get(connection.as<size_t>());
        if (session == nullptr) { co_return -1; }
        if (session->rst) { co_return -1; }
        worker = &session->write;
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

void manapi::net::worker::http_v2::settings_update_initial_window_size(int value) {
    protocol.settings[HTTP2_SETTING_INITIAL_WINDOW_SIZE].first = value;
}

void manapi::net::worker::http_v2::settings_update_max_concurrent_streams(int value) {
    protocol.settings[HTTP2_SETTING_MAX_CONCURRENT_STREAMS].first = value;
}

void manapi::net::worker::http_v2::setting_value_valid(const http2_setting_type &type, const int &value) noexcept(false) {
    if (allow_settings[type].valid(value)) {
        return;
    }

    generate_error(HTTP2_ERROR_PROTOCOL_ERROR, "Invalid http2 setting param");
}

void manapi::net::worker::http_v2::_parse_header_flag (char &c) {
    parse_vars.i ++;
    parse_vars.buffint = (parse_vars.buffint << 8) | static_cast<unsigned char>(c);

    if (parse_vars.i == 5) {
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

        protocol.flag = parse_vars.buffint;
        parse_vars.buffint = 0;

        current = std::bind(&http_v2::_parse_header_stream_id, this, std::placeholders::_1);
    }
}
