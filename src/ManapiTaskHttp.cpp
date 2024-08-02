#include <cstdio>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <cctype>
#include <functional>
#include <format>
#include <unordered_map>
#include <fstream>
#include <quiche.h>

#include "ManapiFetch.h"
#include "ManapiTaskHttp.h"
#include "ManapiCompress.h"
#include "ManapiFilesystem.h"

#define MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(_x) if (_x == size) {                 \
                                request_data.headers_size += _x;                        \
                                if (!manapi::net::http_task::read_next_part (maxsize, _x, this, &request_data)) \
                                    return;                                             \
                                }

#define MANAPI_QUIC_CONNECTION_ID_LEN 16

// TODO: Client may create 100+ threads task

// HEADER CLASS

manapi::net::http_task::~http_task() {
    delete buff;

    if (SSL_in_init(ssl)) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
}

manapi::net::http_task::http_task(int _fd, const sockaddr_storage &_client, const socklen_t &_client_len, manapi::net::http *new_http_server): conn_fd(_fd), client(_client), http_server(new_http_server) {
    client_len = _client_len;

    buff = static_cast<uint8_t *> (malloc(http_server->get_socket_block_size()));

    buff_size = 0;
    conn_type = MANAPI_NET_TYPE_TCP;
}

manapi::net::http_task::http_task(int _fd,char *_buff, const ssize_t &_size, const sockaddr_storage &_client, const socklen_t &_client_len, ev::io *udp_io, manapi::net::http *new_http_server): client(_client), http_server(new_http_server) {
    conn_fd = _fd;
    client_len = _client_len;

    buff = static_cast<uint8_t *>(malloc(http_server->get_socket_block_size()));
    memcpy(buff, _buff, _size);

    buff_size = _size;

    conn_type = MANAPI_NET_TYPE_UDP;
}

// DO ITS

void manapi::net::http_task::doit() {
    switch (conn_type) {
        case MANAPI_NET_TYPE_TCP:
            tcp_doit();
            break;
        case MANAPI_NET_TYPE_UDP:
            udp_doit();
            break;
        default:
            THROW_MANAPI_EXCEPTION("invalid connection protocol: conn_type = {}", conn_type);
    }
}

// udp doit (single connections)
void manapi::net::http_task::udp_doit()
{
    static uint8_t out[4096];

    uint8_t     type;
    uint32_t    version;

    uint8_t     s_cid[QUICHE_MAX_CONN_ID_LEN];
    size_t      s_cid_len = sizeof (s_cid);

    uint8_t     d_cid[QUICHE_MAX_CONN_ID_LEN];
    size_t      d_cid_len = sizeof (s_cid);

    uint8_t     od_cid[QUICHE_MAX_CONN_ID_LEN];
    size_t      od_cid_len = sizeof (s_cid);

    uint8_t     token[quic_token_max_len];
    size_t      token_len = sizeof(token);

    int read = quiche_header_info(buff, buff_size, MANAPI_QUIC_CONNECTION_ID_LEN, &version, &type, s_cid, &s_cid_len, d_cid, &d_cid_len, token, &token_len);

    if (read < 0)
    {
        THROW_MANAPI_EXCEPTION("{}", "failed to parse quic headers. quiche_header_info(...) < 0");
    }

    const std::string dcid_str ((const char *) (d_cid), d_cid_len);

    conn_io = q_map_conns->contains(dcid_str) ? q_map_conns->at(dcid_str) : nullptr;

    if (conn_io == nullptr)
    {
        // no connections in the history
        if (!quiche_version_is_supported(version)) {
            MANAPI_LOG("version negotiation: {}", version);

            ssize_t written = quiche_negotiate_version(s_cid, s_cid_len,
                                                       d_cid, d_cid_len,
                                                       out, sizeof(out));

            if (written < 0)
            {
                THROW_MANAPI_EXCEPTION("failed to create vneg packet: {}",
                        written);
                return;
            }

            ssize_t sent = sendto(conn_fd, out, written, 0,
                                  (struct sockaddr *) &client,
                                  client_len);

            if (sent != written)
            {
                THROW_MANAPI_EXCEPTION("{}", "failed to register new connection (sent != written)");

                return;
            }

            // fprintf(stderr, "sent %zd bytes\n", sent);
            return;
        }

        if (token_len == 0)
        {
            mint_token(d_cid, d_cid_len, &client, client_len,
                       token, &token_len);

            uint8_t new_cid[MANAPI_QUIC_CONNECTION_ID_LEN];

            if (gen_cid(new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) == nullptr)
            {
                return;
            }

            // std::cout << "new s_cid -> " << std::string((char *)new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) << "\n";

            ssize_t written = quiche_retry(s_cid, s_cid_len,
                                           d_cid, d_cid_len,
                                           new_cid, MANAPI_QUIC_CONNECTION_ID_LEN,
                                           token, token_len,
                                           version, out, sizeof(out));

            if (written < 0)
            {
                THROW_MANAPI_EXCEPTION("failed to create retry packet: {}", written);

                return;
            }

            ssize_t sent = sendto(conn_fd, out, written, 0,
                                  (struct sockaddr *) &client,
                                  client_len);
            if (sent != written)
            {
                THROW_MANAPI_EXCEPTION("{}", "failed to send: sendto(...) != written");
                return;
            }

           // -printf(" -> sent %zd bytes\n", sent);
            return;
        }

        if (!validate_token(token, token_len, &client, client_len,
                            od_cid, &od_cid_len)) {
            THROW_MANAPI_EXCEPTION ("{}", "invalid address validation token");
            return;
        }


        conn_io = quic_create_connection(d_cid, d_cid_len, od_cid, od_cid_len);

        if (conn_io == nullptr)
            return;
    }

    std::lock_guard <std::mutex> lock (conn_io->mutex);

    quiche_recv_info recv_info = {
            (struct sockaddr *)&client,
            client_len,
            http_server->server_addr,
            http_server->server_len
    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buff, buff_size, &recv_info);

    if (done < 0)
    {
        printf("failed to process packet: %zd\n", done);
        return;
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        if (conn_io->http3 == nullptr) {
            conn_io->http3 = quiche_h3_conn_new_with_transport(conn_io->conn, http_server->http3_config);

            if (conn_io->http3 == nullptr) {
                THROW_MANAPI_EXCEPTION("{}", "failed to create HTTP/3 connection: quiche_h3_conn_new_with_transport(...) = nullptr");
                return;
            }
        }

        // do next step
        if (!conn_io->tasks.empty()) {
            quiche_stream_iter *stream = quiche_conn_writable(conn_io->conn);

            while (quiche_stream_iter_next(stream, (uint64_t *)&s)) {
                if (conn_io->tasks.contains(s)) {
                    auto task = reinterpret_cast <http_task *> (conn_io->tasks.at(s));

                    if (task->quic_v_body || task->to_delete)
                    {
                        continue;
                    }

                    //quiche_conn_stream_shutdown(conn_io->conn, s, QUICHE_SHUTDOWN_READ, 0);

                    {
                        std::lock_guard <std::mutex> lk (task->quic_m_worker);

                        task->s = s;

                        // notify: we got write stream
                        task->quic_m_write.unlock();

                        if (!task->to_delete)
                        {
                            // wait next request
                            task->quic_m_worker.lock();
                        }
                    }
                }
            }
        }

        // set wrappers for I/O
        mask_read = [this] (const char *part_buff, const size_t &part_buff_size) -> ssize_t {
            repeat:

            if (!quic_v_read) {
                quic_v_read = true;

                std::lock_guard <std::timed_mutex> lk(quic_m_read);

                if (to_delete)
                {
                    return -1;
                }

                // ASK NEW STREAM

                // notify: we need read stream!
                quic_m_worker.unlock();

                // wait worker response
                const auto res = quic_m_read.try_lock_for(std::chrono::seconds(2));

                if (!res)
                {
                    MANAPI_LOG("{}", "quic read timeout");
                    // timeout / lock failed
                    to_delete = true;
                }

               // -printf("QUIC CV READ START!\n");
            }

            if (to_delete)
            {
                return -1;
            }

            ssize_t read = quiche_h3_recv_body(conn_io->http3, conn_io->conn, s, (uint8_t *) part_buff, part_buff_size);

            if (read < 0)
            {
                quic_v_read = false;

                goto repeat;
            }


           // -printf("READ: %zu\n", read);

            return read;
        };

        mask_write = [this] (const char *part_buff, const size_t &part_buff_size) -> ssize_t {
            ssize_t capacity = quiche_conn_stream_capacity(conn_io->conn, s);

            if (quic_v_body)
            {
                quic_v_body = false;
            }

            repeat:

            while (capacity <= 0)
            {
                {
                    std::lock_guard <std::timed_mutex> lk(quic_m_write);

                    if (to_delete)
                    {
                        return (ssize_t) (-1);
                    }

                    // ASK NEW STREAM

                   // -printf("QUIC W NOTIFY\n");

                    // notify worker to get write streams
                    quic_m_worker.unlock();

                   // -printf("QUIC CV write WAIT!\n");

                    // wait a worker response
                    const auto res = quic_m_write.try_lock_for(std::chrono::seconds(2));

                    if (!res)
                    {
                        MANAPI_LOG("{}", "quic write timeout");
                        // timeout / lock failed
                        to_delete = true;
                    }

                   // -printf("QUIC CV WRITE START!\n");
                }

                capacity = quiche_conn_stream_capacity(conn_io->conn, s);

                // TODO: capacity can be always less than zero

                // if (capacity < 0)
                // {
                //     to_delete = true;
                // }

                if (to_delete)
                {
                    return -1;
                }
            }

            const bool final = capacity <= part_buff_size;
            ssize_t written = quiche_h3_send_body(conn_io->http3, conn_io->conn, s, (uint8_t *) part_buff, part_buff_size, final);

            // MANAPI_LOG ("Sent: {}, Capacity: {}, Buff Size: {}, Final {}", written, capacity, part_buff_size, final);

            if (written < 0) {
                capacity = -1;

                goto repeat;
            }

            return written;
        };

        mask_response = [this] (http_response &res) -> ssize_t {
            size_t index = 1;

            const size_t headers_len = index + res.get_headers().size();
            quiche_h3_header headers[headers_len];

            std::string status_code = std::to_string(res.get_status_code());

            headers[0] = {
                    .name = (const uint8_t *) ":status",
                    .name_len = sizeof(":status") - 1,

                    .value = (const uint8_t *) status_code.data(),
                    .value_len = status_code.size()
            };

            for (const auto &header: res.get_headers()) {
                headers[index] = {
                        .name = (uint8_t *) header.first.data(),
                        .name_len = header.first.size(),
                        .value = (uint8_t *) header.second.data(),
                        .value_len = header.second.size()
                };
                index++;
            }

            return quiche_h3_send_response(conn_io->http3, conn_io->conn, s, headers, headers_len, false);
        };

        quiche_h3_event *ev;

        while (true) {
            s = quiche_h3_conn_poll(conn_io->http3,conn_io->conn, &ev);

            if (s < 0)
            {
                break;
            }

            switch (quiche_h3_event_type(ev)) {
                case QUICHE_H3_EVENT_FINISHED: {

                    break;
                }

                case QUICHE_H3_EVENT_HEADERS: {
                    int rc = quiche_h3_event_for_each_header(ev, quic_get_header, &request_data);

                    if (rc != 0)
                    {
                        quiche_h3_event_free(ev);
                        THROW_MANAPI_EXCEPTION("{}", "failed to process headers: quiche_h3_event_for_each_header(...) != 0");
                    }

                    // auto unlock worker
                    std::lock_guard <std::mutex> lk(quic_m_worker);

                    request_data.has_body = quiche_h3_event_headers_has_body (ev);

                    std::thread quic_thr ([this] () {
                        to_delete = false;
                        const auto handler = http_server->get_handler(request_data);

                        if (request_data.has_body)
                        {
                            if (!request_data.headers.contains(http_header.CONTENT_LENGTH))
                            {
                                THROW_MANAPI_EXCEPTION("{}", "content-length not exists");
                            }

                            // we accept peer body
                            quic_v_body = true;

                            // data not contains headers
                            request_data.headers_part = 0;
                            request_data.body_size = std::stoull(request_data.headers[http_header.CONTENT_LENGTH]);
                            request_data.body_left = request_data.body_size;
                            request_data.body_ptr = (char *) buff;

                            request_data.body_part = mask_read (
                                    (char *) buff,
                                    std::min (request_data.body_size, http_server->get_socket_block_size())
                            ) - request_data.headers_part;

                            request_data.body_index = 0;


                        }
                        else
                        {
                            request_data.body_ptr = nullptr;
                            request_data.body_size = 0;
                            request_data.body_part = 0;
                            request_data.headers_part = 0;

                            // shutdown the stream
                            quiche_conn_stream_shutdown(conn_io->conn, s, QUICHE_SHUTDOWN_READ, 0);
                        }

                        handle_request(&handler);

                        quiche_h3_send_body(conn_io->http3, conn_io->conn, s, nullptr, 0, true);

                        to_delete = true;

                        // unlock read/write
                        quic_m_write.unlock();
                        quic_m_read.unlock();

                        // unlock locker
                        quic_m_worker.unlock();

                        if (id != -1)
                        {
                            const auto task = reinterpret_cast <http_task *> (conn_io->tasks.at(id));
                            conn_io->tasks.erase(id);

                            delete task;
                        }
                    });

                    quic_thr.detach();

                    // wait when the stream run out of capacity of the streams or just end
                    quic_m_worker.lock();

                   // -printf("NEXT STEP\n");

                    // clean up
                    if (!to_delete)
                    {
                        id = s;
                        conn_io->tasks.insert({s, this});
                    }

                    break;
                }

                case QUICHE_H3_EVENT_DATA: {
                    if (conn_io->tasks.contains(s))
                    {

                        auto task = reinterpret_cast <http_task *> (conn_io->tasks.at(s));

                        if (!task->quic_v_body || task->to_delete)
                        {
                            break;
                        }


                        {
                            // auto lock/unlock
                            std::lock_guard <std::mutex> lk (task->quic_m_worker);

                            task->s = s;

                            // unlock read function
                            task->quic_m_read.unlock();

                            // printf("READ HTTP WAIT\n");

                            if (!task->to_delete)
                            {
                                // await read function response
                                task->quic_m_worker.lock();
                            }

                            // printf("END HTTP DATA\n");
                        }
                    }

                    break;
                }

                case QUICHE_H3_EVENT_RESET:
                    MANAPI_LOG("{}", "RESET");
                    break;

                case QUICHE_H3_EVENT_PRIORITY_UPDATE:
                    MANAPI_LOG("{}", "PRIORITY_UPDATE");
                    break;

                case QUICHE_H3_EVENT_GOAWAY: {
                    MANAPI_LOG("{}", "got GOAWAY");
                    break;
                }

                default:
                   MANAPI_LOG("{}", "INVALID EVENT");
                   break;
            }

            quiche_h3_event_free(ev);
        }
    }
}

// tcp doit (pool connections)
void manapi::net::http_task::tcp_doit() {
    struct timeval timeout{};

    FD_ZERO (&read_fds);
    FD_SET  (conn_fd, &read_fds);

    timeout.tv_sec = http_server->get_keep_alive(); // keep alive in n sec
    timeout.tv_usec = 0;

    int ready = select(conn_fd + 1, &read_fds, nullptr, nullptr, &timeout);

    if (ready < 0)
    {
        MANAPI_LOG ("unknow socket status (select() < 0): {}", conn_fd);
    }

    else if (ready == 0)
    {
        MANAPI_LOG("The waiting time of {} seconds has been exceeded", http_server->get_keep_alive());
    }

    else
    {
        if (FD_ISSET(conn_fd, &read_fds))
        {
            try
            {
                if (ssl != nullptr)
                {
                    SSL_set_fd(ssl, conn_fd);

                    mask_write  = [this](auto && PH1, auto && PH2) -> ssize_t { return openssl_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };
                    mask_read   = [this](auto && PH1, auto && PH2) -> ssize_t {

                        return openssl_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };

                    const auto res = SSL_accept(ssl);

                    if (!res)
                    {
                        THROW_MANAPI_EXCEPTION("couldnt SSL accept: SSL_accept(ssl) = {}", res);
                    }
                }
                else
                {
                    mask_write  = [this](auto && PH1, auto && PH2) -> ssize_t { return socket_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };
                    mask_read   = [this](auto && PH1, auto && PH2) -> ssize_t {
                        timeval timeout{};

                        timeout.tv_sec  = http_server->get_keep_alive(); // keep alive in n sec
                        timeout.tv_usec = 0;

                        int ready = select (conn_fd + 1, &read_fds, nullptr, nullptr, &timeout);

                        if (ready < 0)
                        {
                            THROW_MANAPI_EXCEPTION("{}", "couldnt validate socket: select(...) < 0");
                        }

                        else if (ready == 0)
                        {
                            MANAPI_LOG ("The waiting time of {} seconds has been exceeded", http_server->get_keep_alive());
                            return -1;
                        }

                        return socket_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
                    };
                }

                mask_response = [this](auto && PH1) { return tcp_send_response(std::forward<decltype(PH1)>(PH1)); };

                ssize_t size = read_next();

                if (size == -1)
                {
                    THROW_MANAPI_EXCEPTION("Could not read from the socket: {}", conn_fd);
                }

                else
                {
                    const size_t *socket_block_size = &reinterpret_cast<http *> (http_server)->get_socket_block_size();

                    request_data.body_index = 0;
                    request_data.body_size = size;

                    tcp_parse_request_response((char *) buff, *socket_block_size, request_data.body_index);

                    const auto handler = std::move(http_server->get_handler(request_data));

                    if (request_data.has_body)
                    {
                        // if the buffer capacity is exhausted
                        if (request_data.body_index == request_data.body_size)
                        {
                            request_data.body_part = read_next();

                            request_data.headers_part = 0;
                            request_data.body_index = 0;
                        }
                        else
                        {
                            // calculate how much space contains headers in the last block
                            request_data.headers_part = request_data.body_index;
                            request_data.body_part = request_data.body_size - request_data.headers_part;
                        }

                        // request.body_size <- now this is the size of all read data

                        if (!request_data.headers.contains(http_header.CONTENT_LENGTH))
                        {
                            throw manapi::utils::exception("content-length not exists");
                        }

                        request_data.body_size = std::stoull(request_data.headers[http_header.CONTENT_LENGTH]);
                        request_data.body_left = request_data.body_size;
                        request_data.body_ptr = (char *)buff + request_data.body_index * sizeof(char);

                        request_data.body_index = 0;
                    }
                    else
                    {
                        request_data.body_ptr = nullptr;
                        request_data.body_size = 0;
                    }

                    tcp_get_ip_addr();

                    handle_request(&handler);
                }
            }
            catch (const manapi::utils::exception &e)
            {
                MANAPI_LOG("Close connection: {}", e.what());
            }
        }
    }

    close(conn_fd);
}

// HTTP

void manapi::net::http_task::handle_request(const http_handler_page *data, const size_t &status, const std::string &message) {
    // handler function not be found
    if (data->handler == nullptr)
    {

        // check exists static folder/file
        if (data->statics != nullptr)
        {
            // if statics exists
            std::string path;

            for (size_t i = data->statics_parts_len; i < request_data.path.size(); i++)
            {
                path += manapi::filesystem::delimiter + request_data.path[i];
            }

            path = manapi::filesystem::join (*data->statics, path);

            if (manapi::filesystem::exists(path) && manapi::filesystem::is_file(path))
            {
                http_response res(request_data, status, message, http_server);

                res.set_compress_enabled(true);
                res.set_partial_status(true);
                res.file(path);

                try {
                    send_response(res);
                }
                catch (const std::exception &e) {
                    MANAPI_LOG("Unexpected error: %s", e.what());

                    send_error_response(503, http_status.SERVICE_UNAVAILABLE_503, data->error);
                }

                return;
            }
        }

        return send_error_response (404, http_status.NOT_FOUND_404, data->error);
    }

    ip_data_t ip_data {
            .ip     = ip,
            .family = client.ss_family
    };

    http_request    req (ip_data, request_data, this, http_server, data);
    http_response   res (request_data, status, message, http_server);

    try
    {
        data->handler->handler (req, res);

        send_response (res);
    }
    catch (const std::exception &e)
    {
        MANAPI_LOG("Unexpected error: {}", e.what());

        send_error_response (503, http_status.SERVICE_UNAVAILABLE_503, data->error);
    }
}

void manapi::net::http_task::tcp_get_ip_addr() {
    void *addr;

    if (client.ss_family == AF_INET)
        addr = &(((struct sockaddr_in*)&client)->sin_addr);
    else
        addr = &(((struct sockaddr_in6*)&client)->sin6_addr);

    inet_ntop(client.ss_family, addr, ip, INET6_ADDRSTRLEN);
}

void manapi::net::http_task::set_http_server(manapi::net::http *new_http_server) {
    http_server = new_http_server;
}

size_t manapi::net::http_task::read_next_part(size_t &size, size_t &i, void *_http_task, request_data_t *request_data) {
    const ssize_t next_block    = reinterpret_cast<http_task *> (_http_task)->read_next();

    if (next_block == -1)
    {
        THROW_MANAPI_EXCEPTION("socket read error: read_next() < {}", "0");
    }

    if (next_block == 0)
    {
        return next_block;
    }

    if (next_block > request_data->body_size)
    {
        THROW_MANAPI_EXCEPTION("body limit: next_block({}) > body_size({})", next_block, request_data->body_size);
    }

    size        -=  i;
    i           =   0;

    request_data->body_ptr              = (char *) reinterpret_cast<http_task *> (_http_task)->buff;

    return next_block;
}

std::string manapi::net::http_task::compress_file(const std::string &file, const std::string &folder, const std::string *compress, manapi::utils::compress::TEMPLATE_INTERFACE compressor) const {
    std::string filepath;

    // compressor
    auto cached = http_server->get_compressed_cache_file(file, *compress);

    if (cached == nullptr) {
        filepath = compressor (file, &folder);

        http_server->set_compressed_cache_file(file, filepath, *compress);
    }
    else
        filepath = *cached;

    return filepath;
}

void manapi::net::http_task::send_response(manapi::net::http_response &res) const {
    std::string response;
    std::string compressed;

    manapi::utils::compress::TEMPLATE_INTERFACE compressor = nullptr;

    auto compress = &res.get_compress();
    auto body     = &res.get_body();


    if (!compress->empty()) {
        if (!res.is_file()                  ||
            !res.get_auto_partial_enabled() ||
            manapi::filesystem::get_size(res.get_file()) < http_server->get_partial_data_min_size()) {

            compressor = http_server->get_compressor(*compress);

            if (compressor != nullptr)
                res.set_header(http_header.CONTENT_ENCODING, *compress);
        }
    }

    bool exists_replacers = res.get_replacers() != nullptr;
    bool exists_compressor = compressor != nullptr;

    // set time
    res.set_header(http_header.DATE, manapi::utils::time ("%a, %d %b %Y %H:%M:%S GMT", false));

    if (res.is_file())
    {
        std::string filepath;

        if (exists_compressor)
        {
            if (exists_replacers)
            {
                THROW_MANAPI_EXCEPTION("{}", "replacers can not be use with compressing");
            }

            filepath = compress_file (res.get_file(), *http_server->config_cache_dir, compress, compressor);
        }

        else
        {
            filepath = res.get_file();
        }

        std::ifstream f;

        f.open(filepath, std::ios::binary);

        if (!f.is_open())
        {
            THROW_MANAPI_EXCEPTION("Could not open the file by the following path: {}", filepath);
        }

        else
        {
            std::vector<utils::replace_founded_item> replacers;

            // get file size
            const ssize_t fileSize = manapi::filesystem::get_size(f);
            ssize_t dynamicFileSize = fileSize;

            if (exists_replacers)
            {
                replacers = utils::found_replacers_in_file (filepath, 0, fileSize, *res.get_replacers());

                for (const auto &replacer: replacers)
                {
                    dynamicFileSize = (ssize_t) (dynamicFileSize - (replacer.pos.second - replacer.pos.first + 1) + replacer.value->size());
                }


            }

            // partial enabled
            if (res.get_auto_partial_enabled() && http_server->get_partial_data_min_size() <= fileSize)
            {
                if (exists_compressor)
                {
                    f.close();
                    THROW_MANAPI_EXCEPTION("the compress '{}' with the partial content is not supported.", utils::escape_string(res.get_compress()));
                }


                if (exists_replacers)
                {
                    f.close();

                    THROW_MANAPI_EXCEPTION("{}", "replacers can not be use with partial");
                }

                res.set_status(206, http_status.PARTIAL_CONTENT_206);
                res.set_header(http_header.ACCEPT_RANGES, "bytes");

                ssize_t start   = 0,
                        back    = (ssize_t)(http_server->get_partial_data_min_size()) - 1,
                        size;

                switch (res.ranges.size())
                {
                    case 1:
                        if (res.ranges[0].first != -1)
                            start   = res.ranges[0].first;

                        if (res.ranges[0].second != -1)
                            back    = res.ranges[0].second;
                        else
                            back    = fileSize - 1;
                    case 0:
                        size = back - start + 1;

                        res.set_header(http_header.CONTENT_LENGTH, std::to_string(size));
                        res.set_header(http_header.CONTENT_RANGE, "bytes " + std::to_string(start) + '-' + std::to_string(back) + '/' + std::to_string(fileSize));

                        // set start position
                        f.seekg(start);

                        if (mask_response(res) >= 0)
                            // set size and send
                            send_file(res, f, size);

                        break;

                    default:
                        f.close();
                        throw manapi::utils::exception ("multi bytes unsupported");

                }
            }
            else {
                res.set_header(http_header.CONTENT_LENGTH, std::to_string(dynamicFileSize));

                if (mask_response (res) >= 0)
                {
                    if (replacers.empty())
                    {
                        // without replacers
                        send_file(res, f, fileSize);
                    }

                    else
                    {
                        // with replacers
                        send_file(res, f, fileSize, replacers);
                    }
                }
            }
            f.close();

            return;
        }
    }
    else if (res.is_text())
    {
        if (compressor != nullptr)
        {
            // encode content !
            compressed      = compressor (*body, nullptr);

            body            = &compressed;
        }

        res.set_header(http_header.CONTENT_LENGTH, std::to_string(body->size()));

        if (!res.get_headers().contains(http_header.CONTENT_TYPE))
        {
            res.set_header(http_header.CONTENT_TYPE, "text/html; charset=UTF-8");
        }

        if (mask_response (res) >= 0)
        {
            send_text (*body, body->size());
        }

        return;
    }
    else if (res.is_proxy())
    {
        auto proxy = new fetch (res.get_data());

        proxy->handle_headers([this, &res] (const std::map <std::string, std::string> &headers) {
             res.set_status_code(200);
             res.set_status_message(http_status.OK_200);

             if (headers.contains(http_header.CONTENT_LENGTH))
             {
                 res.set_header(http_header.CONTENT_LENGTH, headers.at(http_header.CONTENT_LENGTH));
             }

             mask_response(res);
        });

        proxy->handle_body([this] (char *buffer, const size_t &size) {
            const ssize_t sw = mask_write (buffer, size);

            if (sw < 0)
            {
                throw manapi::utils::exception ("Can not write pocket");
            }

            return (size_t) sw;
        });

        res.tasks->await ((task *) proxy);

        return;
    }

    mask_response (res);
}

void manapi::net::http_task::send_text (const std::string &text, const size_t &size) const {
    const char *current = text.data();
    size_t sent = size;

    while (sent != 0) {
        const ssize_t result = mask_write (current, sent);

        if (result <= 0)
        {
            THROW_MANAPI_EXCEPTION("Could not send the text: mask_write(...) = {}. Size: {}", result, sent);
        }

        if (result > sent) {
            THROW_MANAPI_EXCEPTION("Total sent size > prepared sent size. {} > {}", result, sent);
        }

        sent -= result;

        current = current + result * sizeof (char);
    }
}

void manapi::net::http_task::send_file (manapi::net::http_response &res, std::ifstream &f, ssize_t size) const {
    std::string block;
    auto        block_size = (ssize_t) http_server->get_socket_block_size();

    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    while (size != current)
    {
        const ssize_t left = size - current;

        if (left < block_size)
            block_size = left;

        f.read(block.data(), block_size);

        ssize_t sent = mask_write(block.data(), block_size);

        if (sent < 0)
        {
            // cannot to send
            break;
        }

        current += sent;

        //printf("STEP: %zi LEFT: %zi NEED: %zi CURRENT: %zi\n", sent, left, size, current);

        f.seekg(current);
    }
}

void manapi::net::http_task::send_file (manapi::net::http_response &res, std::ifstream &f, ssize_t size, std::vector<utils::replace_founded_item> &replacers) const {
    std::string block;
    auto        block_size = (ssize_t) (http_server->get_socket_block_size());

    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    ssize_t index;
    ssize_t replacer_index = 0;
    ssize_t current_key_index = 0;

    while (size != current) {
        const ssize_t left = size - current;

        if (left < block_size)
            block_size = left;

        f.read(block.data(), block_size);

        index = f.tellg();

        ssize_t shift = 0;

        while (replacer_index != replacers.size() && index > replacers[replacer_index].pos.first + shift) {
            ssize_t key_size = replacers[replacer_index].pos.second - replacers[replacer_index].pos.first + 1;
            ssize_t start_index_in_block = block_size - (index - (replacers[replacer_index].pos.first + shift + current_key_index));

            ssize_t index_in_block = start_index_in_block;

            for (; index_in_block < block_size && current_key_index < key_size; index_in_block++, current_key_index++) {
                if (current_key_index < replacers[replacer_index].value->size()) {
                    block[index_in_block] = replacers[replacer_index].value->at(current_key_index);
                    continue;
                }

                break;
            }

            // if KEY_SIZE >= VALUE_SIZE
            if (current_key_index == replacers[replacer_index].value->size()) {
                // shift <<
                // key_size <- the shift
                const ssize_t shifted_index_in_block = start_index_in_block + key_size;
                const ssize_t key_left_size = key_size - current_key_index;

                // shift chars
                ssize_t i = shifted_index_in_block;
                for (; i < block_size; i++, index_in_block++) {
                    block[index_in_block] = block[i];
                }

                if (index_in_block < block_size)
                {
                    auto needed = block_size - index_in_block;

                    if (index < size)
                    {

                        // + 1 bcz we dont want to grab } special symbol at the end of the special key {{KEY}}
                        current = (index - block_size) + i;

                        f.seekg(current);
                        f.read(block.data() + index_in_block * sizeof(char), needed);

                        current = f.tellg() - block_size;
                    }
                    else
                    {
                        shift -= key_left_size;
                    }
                }
                else
                {
                    current += key_left_size;
                    f.seekg(current);
                }

                index = f.tellg();

                replacer_index++;

                current_key_index   = 0;

                continue;
            }
            // if KEY_SIZE < VALUE_SIZE
            else if (current_key_index == key_size && current_key_index < replacers[replacer_index].value->size()) {
                next:
                // shift >>
                bool repeat = false;

                ssize_t value_left_size = (ssize_t) replacers[replacer_index].value->size() - current_key_index;

                // replace
                ssize_t i = index_in_block;
                ssize_t free_space = block_size - (ssize_t) i;

                if (free_space < value_left_size) {
                    block_size += value_left_size - free_space;

                    if (block_size >= http_server->get_socket_block_size()) {
                        block_size = (ssize_t) http_server->get_socket_block_size();
                        repeat = true;
                    }
                }

                for (; i < block_size && current_key_index < replacers[replacer_index].value->size(); i++, current_key_index++) {
                    char a = replacers[replacer_index].value->at(current_key_index);
                    block[i] = a;
                }

                if (repeat) {
                    ssize_t sent = mask_write(block.data(), block_size);

                    if (sent < 0)
                        // cannot to send
                        return;

                    current += sent;

                    shift   += block_size - index_in_block;

                    // resolve
                    index_in_block = 0;

                    goto next;
                }

                if (i > block_size) {
                   // -printf("OK\n");
                }
                else {
                    free_space = block_size - i;
                    ssize_t can_read = (ssize_t) http_server->get_socket_block_size() - i;

                    // we want to read chars by size can_read
                    f.seekg(current + index_in_block - shift);
                    ssize_t read = f.readsome (block.data() + i * sizeof (char), can_read);

                    block_size += read - free_space;

                    current = f.tellg() - block_size;

                    shift = 0;
                }

                index = f.tellg();

                replacer_index++;

                current_key_index   = 0;

                continue;
            }

            break;
        }


        ssize_t sent = mask_write(block.data(), block_size);

        if (sent < 0)
            // cannot to send
            break;

        current += sent;

        f.seekg(current);
    }

}

ssize_t manapi::net::http_task::read_next() {
    return mask_read(reinterpret_cast<const char *>(buff), http_server->get_socket_block_size());
}

void manapi::net::http_task::send_error_response(const size_t &status, const std::string &message, const http_handler_functions *error) {
    if (error == nullptr)
    {
        // TODO: Default error page
        return;
    }

    http_handler_page handler;

    handler.error = nullptr;
    handler.handler = error;

    handle_request(&handler, status, message);
}

// MASKS

ssize_t manapi::net::http_task::socket_read(const char *part_buff, const size_t &part_buff_size) const {
    return read (conn_fd, (void*) part_buff, part_buff_size);
}

ssize_t manapi::net::http_task::socket_write(const char *part_buff, const size_t &part_buff_size) const {
    return send (conn_fd, (void*) part_buff, part_buff_size, MSG_NOSIGNAL);
}

ssize_t manapi::net::http_task::openssl_read(const char *part_buff, const size_t &part_buff_size) const {
    return SSL_read (ssl, (void *) part_buff, (int) part_buff_size);
}

ssize_t manapi::net::http_task::openssl_write(const char *part_buff, const size_t &part_buff_size) const {
    return SSL_write (ssl, (void *) part_buff, (int) part_buff_size);
}

// TCP

void manapi::net::http_task::tcp_stringify_headers(std::string &response, manapi::net::http_response &res, char *delimiter) {
    // add headers
    for (const auto& header : res.get_headers())
        response += header.first + ": " + header.second + delimiter;
}

void manapi::net::http_task::tcp_stringify_http_info(std::string &response, manapi::net::http_response &res, char *delimiter) const {
    response += "HTTP/" + http_server->get_http_version_str() + ' ' + std::to_string(res.get_status_code())
                + ' ' + res.get_status_message() + delimiter;
}

void manapi::net::http_task::tcp_parse_request_response(char *response, const size_t &size, size_t &i) {
    request_data.headers_size = 0;

    size_t maxsize = http_server->get_max_header_block_size();

    // Method
    for (; i < maxsize; i++) {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == ' ')
            break;

        request_data.method += response[i];
    }


    // URI
    // scip \n
    i++;
    parse_uri_path_dynamic (request_data, (char *) response, size, i, [&maxsize, this, &size, &i] () { MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i); });

    // HTTP
    for (i++; i < maxsize; i++) {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == '\r')
            continue;

        if (response[i] == '\n')
            break;

        request_data.http += response[i];
    }



    // Headers
    std::string key;
    std::string *value;

    bool is_key = true;
    bool dbl    = false;

    for (i++; i < maxsize; i++) {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == '\n') {
            is_key  = true;
            if (!key.empty())
                key = "";

            // double \n
            if (dbl) {
                i++;
                break;
            }

            dbl = true;
            continue;
        }

        if (response[i] == '\r')
            continue;

        else if (response[i] == ':' && is_key) {
            is_key  = false;
            value   = &request_data.headers[key];

            continue;
        }

        if (dbl)
            dbl = false;



        if (is_key)
            key     += (char)std::tolower(response[i]);
        else {
            if (response[i] == ' ' && value->empty())
                continue;

            *value  += response[i];
        }
    }

    if (maxsize == i)
        throw manapi::utils::exception ("request header is too large.");

    request_data.headers_size += i;
}

ssize_t manapi::net::http_task::tcp_send_response(manapi::net::http_response &res) {
    char delimiter[] = "\r\n\0";

    std::string response;

    tcp_stringify_http_info (response, res, delimiter);
    tcp_stringify_headers   (response, res, delimiter);

    response += delimiter;

    return mask_write(response.data(), response.size());
}

// QUIC

void manapi::net::http_task::mint_token(const uint8_t *d_cid, size_t d_cid_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *token, size_t *token_len) {
    memcpy(token, "quiche", sizeof("quiche") - 1);
    memcpy(token + sizeof("quiche") - 1, addr, addr_len);
    memcpy(token + sizeof("quiche") - 1 + addr_len, d_cid, d_cid_len);

    *token_len = sizeof("quiche") - 1 + addr_len + d_cid_len;
}

bool manapi::net::http_task::validate_token(const uint8_t *token, size_t token_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *od_cid, size_t *od_cid_len) {
    if ((token_len < sizeof("quiche") - 1) ||
        memcmp(token, "quiche", sizeof("quiche") - 1)) {
        return false;
    }

    token += sizeof("quiche") - 1;
    token_len -= sizeof("quiche") - 1;

    if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
        return false;
    }

    token += addr_len;
    token_len -= addr_len;

    if (*od_cid_len < token_len) {
        return false;
    }

    memcpy(od_cid, token, token_len);
    *od_cid_len = token_len;

    return true;
}

uint8_t *manapi::net::http_task::gen_cid(uint8_t *cid, const size_t &cid_len) {
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        THROW_MANAPI_EXCEPTION("{}", "failed to open /dev/urandom: open(...) < 0");
        return nullptr;
    }

    ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        THROW_MANAPI_EXCEPTION ("{}", "failed to create connection ID: read(...) < 0");
        return nullptr;
    }

    return cid;
}

void manapi::net::http_task::quic_flush_egress(manapi::net::http_qc_conn_io *conn_io) {
    static uint8_t out[1350];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(conn_io->conn, out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            //fprintf(stderr, "done writing\n");
            break;
        }

        if (written < 0) {
            THROW_MANAPI_EXCEPTION("failed to create packet: {}", written);
            return;
        }

        ssize_t sent = sendto(conn_io->sock_fd, out, written, 0, (struct sockaddr *) &conn_io->peer_addr, conn_io->peer_addr_len);
        if (sent != written) {
            THROW_MANAPI_EXCEPTION ("{}", "failed to send: sendto(...) != written");
            return;
        }

        //fprintf(stderr, "sent %zd bytes\n", sent);
    }
}

int manapi::net::http_task::quic_get_header(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) {
    auto request_data = (request_data_t *) argp;
    if (name_len <= 0 || value_len <= 0)
        return -1;

    std::string name_string = std::string ((char *) name, name_len);

    // no header
    if (name_string[0] == ':') {
        if (name_string == ":method")
            request_data->method = std::string ((char *) value, value_len);

        else if (name_string == ":scheme")
            return 0;

        else if (name_string == ":authority")
            return 0;

        else if (name_string == ":path") {
            size_t i = 0;

            parse_uri_path_dynamic (*request_data, (char *) value, value_len, i);
        }

        return 0;
    }
    // header

    request_data->headers.insert({std::string((char *) name, name_len), std::string((char *) value, value_len)});

    return 0;
}

manapi::net::http_qc_conn_io * manapi::net::http_task::quic_create_connection(uint8_t *s_cid, size_t s_cid_len, uint8_t *od_cid, size_t od_cid_len) {
    auto new_conn_io = new http_qc_conn_io ();

    if (s_cid_len != MANAPI_QUIC_CONNECTION_ID_LEN) {
        THROW_MANAPI_EXCEPTION ("{}", "failed, s_cid length too short");
        return nullptr;
    }

    memcpy(new_conn_io->cid, s_cid, MANAPI_QUIC_CONNECTION_ID_LEN);

    quiche_conn *conn = quiche_accept(new_conn_io->cid, MANAPI_QUIC_CONNECTION_ID_LEN, od_cid, od_cid_len, http_server->server_addr, http_server->server_len,
                                      (struct sockaddr *) (&client), client_len, q_config);

    if (conn == nullptr) {
        THROW_MANAPI_EXCEPTION ("{}", "failed to create connection: quiche_accept(...) = nullptr");
        return nullptr;
    }

    new_conn_io->conn           = conn;
    new_conn_io->sock_fd        = conn_fd;

    new_conn_io->peer_addr      = client;
    new_conn_io->peer_addr_len  = client_len;

    new_conn_io->key            = std::string((char *) (new_conn_io->cid), MANAPI_QUIC_CONNECTION_ID_LEN);

    new_conn_io->mutex.lock();

    auto d                      = new std::pair(new_conn_io, q_map_conns);

    new_conn_io->timer.set (http_server->get_loop());
    new_conn_io->timer.set<http_task::quic_timeout_cb> (d);
    new_conn_io->timer.start();

    q_map_conns->insert(new_conn_io->key, new_conn_io);

    new_conn_io->mutex.unlock();

    return new_conn_io;
}

void manapi::net::http_task::quic_timeout_cb(ev::timer &watcher, int revents) {
    auto p = static_cast<std::pair<http_qc_conn_io *, QUIC_MAP_CONNS_T *> *>(watcher.data);

    std::unique_lock <std::mutex> lk (p->first->mutex, std::try_to_lock);

    if (!lk.owns_lock())
    {
        return;
    }

    quiche_conn_on_timeout(p->first->conn);

    quic_flush_egress(p->first);

    if (quiche_conn_is_closed(p->first->conn)) {

        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(p->first->conn, &stats);
        quiche_conn_path_stats(p->first->conn, 0, &path_stats);

        THROW_MANAPI_EXCEPTION("connection closed, recv={} sent={} lost={} rtt={} ns cwnd={}\n",
                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        p->second->erase(p->first->key);

        lk.unlock();

        quic_delete_conn_io (p->first);

        return;
    }

    p->first->timer.start(1);
}

void manapi::net::http_task::set_quic_config(quiche_config *config) {
    q_config = config;
}

void manapi::net::http_task::set_quic_map_conns(QUIC_MAP_CONNS_T *new_map_conns) {
    q_map_conns = new_map_conns;
}

void manapi::net::http_task::parse_uri_path_dynamic(request_data_t &request_data, char *response, const size_t &size, size_t &i, const std::function<void()>& finished) {
    request_data.path       = {};
    request_data.divided    = -1;

    char hex_symbols[2];
    char hex_index = -1;

    for (;i <= size; i++) {
        if (i == size) {
            // if finished func is nullptr, so then we can continue exec
            if (finished == nullptr)
                break;

            finished ();
        }

        if (response[i] == ' ')
            break;

        request_data.uri += response[i];

        if (hex_index >= 0) {
            hex_symbols[hex_index] = response[i];

            if (hex_index == 1) {
                char x = (char)(manapi::utils::hex2dec(hex_symbols[0]) << 4 | manapi::utils::hex2dec(hex_symbols[1]));

                if (manapi::utils::valid_special_symbol(x)) {
                    request_data.path.back()    += x;

                    continue;
                }
                else {
                    request_data.path.back()    += '%';
                    request_data.path.back()    += hex_symbols;
                }

                hex_index = -1;
            }
            else
                hex_index++;
        }

        if (response[i] == '%' && !request_data.path.empty()) {
            hex_index = 0;
            continue;
        }

        else if (request_data.divided == -1) {
            if (response[i] == '/') {
                request_data.path.emplace_back("");
                continue;
            }

            if (response[i] == '?') {
                request_data.divided = (ssize_t) request_data.path.size();
                request_data.path.emplace_back("");
                continue;
            }
        }

        if (!request_data.path.empty())
            request_data.path.back() += response[i];
    }

    if (request_data.path.empty())
        return;


    for (size_t j = request_data.path.size() - 1;;) {
        if (request_data.path[j].empty())
            request_data.path.pop_back();


        if (j == 0)
            break;

        j--;
    }
}

void manapi::net::http_task::quic_delete_conn_io(manapi::net::http_qc_conn_io *conn_io) {
    // force clear tasks
    for (auto it = conn_io->tasks.begin(); it != conn_io->tasks.end();)
    {
        auto task = reinterpret_cast <http_task *> (it->second);

        if (task->to_delete)
        {
            it = conn_io->tasks.begin();

            // stop cpu overload
            // TODO: resolve quic_delete_conn_io(...)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        else
        {
            task->to_delete = true;

            // unlock read/write
            task->quic_m_write.unlock();
            task->quic_m_read.unlock();

            // unlock workers
            task->quic_m_worker.unlock();

            it = conn_io->tasks.begin();
        }
    }

    conn_io->timer.stop();
    quiche_conn_free(conn_io->conn);

    free (conn_io->timer.data);

    delete conn_io;
}


