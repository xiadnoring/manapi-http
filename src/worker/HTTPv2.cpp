#include "worker/HTTPv2.hpp"
#include "http/HTTPv2.hpp"

#include "ManapiHttpResponse.hpp"

manapi::net::worker::http_v2::http_v2(net::http::site site, bufferpool_t bufferpool, std::shared_ptr<worker::worker_config_t> worker_data, http::config *config) : base(std::move(site), std::move(bufferpool), std::move(worker_data), config) {}

manapi::net::worker::http_v2::~http_v2() = default;

void manapi::net::worker::http_v2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto const data = conn->as<http::http_v2_stream_t>();
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size);
    }
}

void manapi::net::worker::http_v2::close_connection(shared_conn conn, bool clean_disconnect) {
    auto data = conn->as<http::http_v2_stream_t>();
    if (data->flags & http::HTTP2_STREAM_REMOVED) {
        return;
    }

    data->flags |= http::HTTP2_STREAM_CLOSED|http::HTTP2_STREAM_REMOVED;
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0);
    }
}

void manapi::net::worker::http_v2::configure_connection(const shared_conn &conn, oncont_cb cb) {
    cb.call(true);
}

manapi::future<ssize_t> manapi::net::worker::http_v2::response(const shared_conn &connection, http::response *resp, bool finish) {
    auto const data = connection->as<http::http_v2_stream_t>();
    auto headers = std::move(resp->headers());
    co_return co_await http::http_v2_response(this, connection, data, resp->status_code(), std::move(headers), finish);
}

    int manapi::net::worker::http_v2::event_flags(const shared_conn & conn) {
    auto const data = conn->as<http::http_v2_stream_t>();
    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
    return data->flags & (ev::READ|ev::WRITE|ev::DISCONNECT);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<http::http_v2_stream_t>();
    if (flags & ev::WRITE) {
        data->ctx->worker->event_toggle(conn, true, ev::WRITE);
    }
    return std::exchange(data->flags, ((data->flags >> 2) << 2) | flags);;
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::http_v2::event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<http::http_v2_stream_t>();
    auto n = std::exchange(conn_data->ev_callback, std::move(callback));
    if (conn_data->ev_callback && (conn_data->flags & http::HTTP2_STREAM_CLOSED)) {
        conn_data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0);
    }
    return std::move(n);
}

void manapi::net::worker::http_v2::init() {

}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v2::ipdata(worker::connection *conn) {
    auto const data = conn->as<http::http_v2_stream_t>();
    return data->ctx->conn->ipdata.get();
}

bool manapi::net::worker::http_v2::is_writable(const shared_conn &conn) {
    auto const s = conn->as<http::http_v2_stream_t>();
    return s->write_window && s->ctx->write_window;
}

bool manapi::net::worker::http_v2::is_valid_connection(worker::connection *connection) {
    return true;
}

void manapi::net::worker::http_v2::stop() {

}

ssize_t manapi::net::worker::http_v2::sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    return sync_write_ex (conn, buff, size, finish, static_cast<int>(this->config_->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v2::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto const data = conn->as<http::http_v2_stream_t>();
    return http::http_v2_write(data, buff, size, finish);
}


// manapi::future<ssize_t> manapi::net::worker::http_v2::response(worker::connection &connection, http::response &resp, bool finish) {
//     auto &conn = connection.as<manapi_http_2_connection_t>();
//
//     co_await conn.original->mx.lock();
//     conn.original->headers = std::move(resp.headers());
//     conn.original->headers[":status"] = std::to_string(resp.status_code());
//     if (finish) { conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_SEND_EOS); }
//     conn.original->atomic_flags.fetch_or(HTTP2_THREAD_ATOMIC_HEADERS);
//     this->io_call_watcher->send();
//
//     /* it's okay */
//     co_return 1;
// }
// void manapi::net::worker::http_v2::session_worker(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it) {
//     if (it == this->threads.end()) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Failed to find session thread data by id");  }
//     auto worker = this->new_dependency();
//     auto id = it->first;
//     auto client = std::make_shared<http::http_v2> (worker, this->config, this->site);
//     client->connection = std::make_shared<worker::connection>(new manapi_http_2_connection_t (it->second.get(), 0, 0, 0),
//         [] (void *ptr) -> void { delete static_cast<manapi_http_2_connection_t *> (ptr); });
//     client->connection->version = manapi::net::http::versions::HTTP_v2;
//
//
// void manapi::net::worker::http_v2::send_headers(http_v2_thread_data_t &stream) {
//     manapi::compress::hpack::encoder_t encoder;
//     for (auto &header: stream.headers) {
//         encoder.add (compress::hpack::header_t(header.first, std::move(header.second)));
//     }
//     uint8_t cflag = 0x0;
//     const auto data = encoder.data();
//     size_t cnt = 0;
//     auto frameSize = static_cast<size_t>(this->protocol.client_settings.max_frame_size);
//     http2_frame_type ft = HTTP2_FRAME_HEADERS;
//     if (stream.atomic_flags & HTTP2_THREAD_ATOMIC_SEND_EOS) { cflag |= HTTP2_FLAG_HEADERS_END_STREAM; }
//     goto skip;
//     while (cnt < data.size()) {
//         ft = HTTP2_FRAME_CONTINUATION;
//         skip:
//         auto left = std::min(frameSize, data.size() - cnt);
//         if (cnt + left == data.size()) {
//             cflag |= HTTP2_FLAG_HEADERS_END_HEADERS;
//         }
//         send_frame(ft, cflag, stream.id, std::string_view(data.data() + cnt, left));
//         cflag = 0;
//         cnt += left;
//     }
// }