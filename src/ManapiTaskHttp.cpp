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

#include "ManapiApi.hpp"
#include "ManapiFetch.hpp"
#include "ManapiTaskHttp.hpp"
#include "ManapiCompress.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiTaskFunction.hpp"

#define MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(_x) if (_x == size) {                 \
                                request_data.headers_size += _x;                        \
                                if (!manapi::net::http_task::read_next_part (maxsize, _x, this, &request_data)) \
                                    return;                                             \
                                }

#define MANAPI_QUIC_CONNECTION_ID_LEN 16

// TODO: Client may create 100+ threads task

// HEADER CLASS


manapi::net::http_task::~http_task() {
    delete []static_cast <uint8_t *> (buff);
}

manapi::net::http_task::http_task(int _fd, const sockaddr &_client, const socklen_t &_client_len, class manapi::net::site *site, class manapi::net::config *config, const enum conn_type &conn_type) {
    this->client_len        = _client_len;
    this->conn_fd           = _fd;
    this->site              = site;
    this->client            = _client;
    this->config            = config;
    this->conn_type         = conn_type;

    socket_information = {
        .ip = inet_ntoa(reinterpret_cast<struct sockaddr_in *> (&client)->sin_addr),
        .port = htons(reinterpret_cast<struct sockaddr_in *> (&client)->sin_port)
    };

    buff = new uint8_t[config->get_socket_block_size()];

    buff_size = 0;
}

// DO ITS

void manapi::net::http_task::doit() {
    switch (conn_type) {
        case CONN_TCP: {
            tcp_doit();
            break;
        }
        case CONN_UDP: {
            udp_doit();
            break;
        }
        default:
            THROW_MANAPI_EXCEPTION("invalid connection protocol: conn_type = {}", conn_type);
    }
}

// udp doit (single connections)
void manapi::net::http_task::udp_doit() {
    // set wrappers for I/O
    mask_read = [this] (const char *part_buff, const size_t &part_buff_size) -> ssize_t {
        if (is_deleting)
        {
            return -1;
        }

        const size_t stream_id = this->stream_id;

        while (true)
        {
            if (!quic_v_read)
            {
                std::lock_guard <std::timed_mutex> lk(quic_m_read);

                if (is_deleting)
                {
                    return -1;
                }

                // ASK NEW STREAM

                // notify: we need read stream!
                quic_m_worker.unlock();

                // wait worker response
                const auto res = quic_m_read.try_lock_for(std::chrono::seconds(config->get_keep_alive()));

                if (!res)
                {
                    ssize_t capacity = quiche_conn_stream_capacity(conn_io->conn, stream_id);
                    MANAPI_LOG("quic read timeout. capacity: {}", capacity);
                    // timeout / lock failed
                    is_deleting = true;
                }

                if (is_deleting)
                {
                    return -1;
                }

                quic_v_read = true;
            }

            const ssize_t read = quiche_h3_recv_body(conn_io->http3, conn_io->conn, stream_id, (uint8_t *) part_buff, part_buff_size);

            if (read <= QUICHE_H3_ERR_DONE)
            {
                quic_v_read = false;
                continue;
            }

            return read;
        }
    };

    mask_write = [this] (const char *part_buff, const size_t &part_buff_size) -> ssize_t {
        if (is_deleting)
        {
            return -1;
        }

        //const size_t stream_id = this->stream_id;
        ssize_t capacity = quiche_conn_stream_capacity(conn_io->conn, stream_id);

        if (capacity < 0)
        {
            is_deleting = true;
        }

        while (capacity < MANAPI_QUIC_CAPACITY_MIN)
        {
            {
                if (is_deleting)
                {
                    return -1;
                }

                size_t keep_alive = config->get_send_timeout() * 1000;

                utils::before_delete bd_m_worker ([this] () -> void {quic_m_write.unlock(); });
                if (!quic_m_write.try_lock_for(std::chrono::milliseconds(keep_alive)))
                {
                    MANAPI_LOG("quic write timeout({}s): {}", keep_alive, conn_io->key);
                    is_deleting = true;
                }

                if (is_deleting)
                {
                    return -1;
                }

                // ASK NEW STREAM

                // notify worker to get write streams
                quic_m_worker.unlock();

                const size_t step = 50;
                bool res = false;
                while (keep_alive > step) {
                    keep_alive -= step;

                    // wait a worker response
                    res = quic_m_write.try_lock_for(std::chrono::milliseconds(step));

                    if (is_deleting)
                    {
                        return -1;
                    }

                    capacity = quiche_conn_stream_capacity(conn_io->conn, stream_id);

                    if (capacity <= QUICHE_H3_ERR_DONE)
                    {
                        MANAPI_LOG("capacity <= QUICHE_H3_ERR_DONE: {}", capacity);
                        // timeout / lock failed
                        is_deleting = true;
                        break;
                    }

                    if (res)
                    {
                        break;
                    }

                    if (capacity >= MANAPI_QUIC_CAPACITY_MIN)
                    {
                        res = true;
                        break;
                    }

                    // quiche_h3_priority priority = {
                    //     true,
                    //     1
                    // };
                    // quiche_h3_send_priority_update_for_request(conn_io->http3, conn_io->conn, stream_id, &priority);
                }

                if (!res)
                {
                    MANAPI_LOG("quic write timeout ({}s): capacity: {}. stream_id: {}", config->get_send_timeout(), capacity, stream_id);
                    // timeout / lock failed
                    is_deleting = true;
                }
            }

            if (is_deleting)
            {
                return -1;
            }
        }


        const bool final = (capacity - MANAPI_QUIC_CAPACITY_MIN + 1) <= part_buff_size;

        const ssize_t written = quiche_h3_send_body(conn_io->http3, conn_io->conn, stream_id, (uint8_t *) part_buff, std::min(part_buff_size, (size_t) capacity), final);

        if (written <= QUICHE_H3_ERR_DONE)
        {
            MANAPI_LOG("write error: capacity: {}", capacity);
        }

        return written;
    };

    mask_write_file = [this] (const std::string &filePath, const ssize_t &start, const ssize_t &size) -> void {
        std::lock_guard<std::timed_mutex> lk (quic_m_write);
        if (is_deleting) { return; }

        ssize_t preSize = size;

        fti = { .filePath = filePath, .start = start, .size = size };
        quic_m_worker.unlock();
        while (true)
        {
            if (is_deleting) { return; }

            if (!quic_m_write.try_lock_for(std::chrono::seconds (config->get_send_timeout())))
            {
                if (is_deleting) { return; }

                if (preSize == fti.size)
                {
                    MANAPI_LOG("quic write timeout ({}s): {}", config->get_send_timeout(), conn_io->key);
                    is_deleting = true;
                }
                else
                {
                    preSize = fti.size;
                    if (preSize == 0) { break; }
                    continue;
                }
            }

            break;
        }
    };

    mask_response = [this] (http_response &res) -> ssize_t {
        if (is_deleting)
        {
            return -1;
        }
        const size_t stream_id = this->stream_id;
        // shutdown the stream

        quiche_conn_stream_shutdown(conn_io->conn, stream_id, QUICHE_SHUTDOWN_READ, 0);

        if (quic_v_body)
        {
            quic_v_body = false;
        }

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

        const ssize_t result = quiche_h3_send_response(conn_io->http3, conn_io->conn, stream_id, headers, headers_len, false);

        // dont wait bcz quiche_h3_send_response(...) executed
        //quic_m_worker.unlock();

        return result;
    };

    utils::before_delete bd_conn ([this] () -> void {
        if (is_deleting)
        {
            if (stream_id != -1)
            {
                quiche_h3_send_goaway(conn_io->http3, conn_io->conn, stream_id);
                quiche_conn_close(conn_io->conn, true, 0, nullptr, 0);
            }
        }
        else
        {
            is_deleting = true;
        }

        // unlock read/write
        quic_m_write.unlock();
        quic_m_read.unlock();

        // unlock locker
        quic_m_worker.unlock();

        //std::lock_guard<std::mutex> lk_conn_mutex (conn_io->mutex);

        if (stream_id != -1)
        {
            conn_io->tasks.erase(stream_id);

            // if (!task_doit_mutex.try_lock_for(std::chrono::seconds(config->get_keep_alive())))
            // {
            //     MANAPI_LOG ("timeout task wait doit: {}", "task_doit_mutex.try_lock_for(...) = false");
            // }

            // delete task;

            //conn_io->wait.unlock();
        }
    });

    const auto handler = site->get_handler(request_data);

    if (request_data.has_body)
    {
        if (!request_data.headers.contains(HTTP_HEADER.CONTENT_LENGTH))
        {
            THROW_MANAPI_EXCEPTION("{}", "content-length not exists");
        }

        // we accept peer body
        quic_v_body = true;

        // data not contains headers
        request_data.headers_part = 0;
        request_data.body_size = std::stoull(request_data.headers[HTTP_HEADER.CONTENT_LENGTH]);
        request_data.body_left = request_data.body_size;
        request_data.body_ptr = (char *) buff;

        request_data.body_part = mask_read (
                (char *) buff,
                std::min (request_data.body_size, config->get_socket_block_size())
        ) - request_data.headers_part;

        request_data.body_index = 0;


    }
    else
    {
        request_data.body_ptr = nullptr;
        request_data.body_size = 0;
        request_data.body_part = 0;
        request_data.headers_part = 0;
    }

    handle_request(&handler);

    if (!is_deleting)
    {
        quiche_h3_send_body(conn_io->http3, conn_io->conn, stream_id, nullptr, 0, true);
    }
}
void manapi::net::http_task::udp_loop_event(QUIC_MAP_CONNS_T *quic_map_conns, class site *site, class config *config, const std::string &scid, const sockaddr &client, const socklen_t &client_len) {
    http_quic_conn_io *conn_io;

    {
        quic_map_conns->lock();
        utils::before_delete bd_quic_map_conns ([&quic_map_conns] () -> void { quic_map_conns->unlock(); });

        // if the connection is closed while waiting
        if (!quic_map_conns->contains(scid))
        {
            return;
        }

        conn_io = quic_map_conns->at(scid);
    }

    utils::before_delete increase_responsing ([&] () -> void { conn_io->is_responsing = false; });

    if (conn_io->is_deleting) { return; }

    // we want to unlock mutex and say that we working with connection yet.
    //utils::before_delete unflag_responsing ([&conn_io] () -> void { conn_io->is_responsing --; });

    const quiche_recv_info recv_info = {
        (sockaddr *)(&client),
        client_len,
        &config->get_server_address(),
        config->get_server_len()
    };

    while (true) {
        {
            std::lock_guard<std::mutex> lk (conn_io->buffers_mutex);
            if (conn_io->buffers.empty()) { break; }
            auto first = conn_io->buffers.begin();
            const ssize_t done = quiche_conn_recv(conn_io->conn, (uint8_t *)first->data(), first->size(), &recv_info);
            conn_io->buffers.erase(first);
            if (done < 0)
            {
                MANAPI_LOG ("failed to process packet: {}", done);
                return;
            }
        }

        std::unique_lock <std::mutex> lk (conn_io->mutex);

        // if the connection is closed
        if (quiche_conn_is_closed(conn_io->conn) || quiche_conn_is_timed_out(conn_io->conn) || conn_io->is_deleting)
        {
            return;
        }

        if (quiche_conn_is_in_early_data(conn_io->conn) || quiche_conn_is_established(conn_io->conn) && conn_io->http3 == nullptr) {
            if (conn_io->http3 == nullptr) {
                conn_io->http3 = quiche_h3_conn_new_with_transport(conn_io->conn, config->get_http3_config());

                if (conn_io->http3 == nullptr) {
                    THROW_MANAPI_EXCEPTION("{}", "failed to create HTTP/3 connection: quiche_h3_conn_new_with_transport(...) = nullptr");
                    return;
                }
            }
        }

        ssize_t stream_id;

        // if conn_io->http3 is some
        if (conn_io->http3 != nullptr) {
            // do next step
            {
                quiche_stream_iter *stream = quiche_conn_writable(conn_io->conn);
                utils::before_delete bd_stream ([&stream] () -> void { quiche_stream_iter_free(stream); });


                while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
                    if (conn_io->tasks.contains(stream_id)) {
                        auto task = reinterpret_cast <http_task *> (conn_io->tasks.at(stream_id));

                        if (task->quic_v_body || task->is_deleting)
                        {
                            continue;
                        }

                        {
                            std::lock_guard<std::timed_mutex> lock_task (task->quic_m_worker);
                            if (task->is_deleting)
                            {
                                continue;
                            }

                            if (task->buff_type == MANAPI_HTTP_BUFF_BINARY)
                            {
                                //MANAPI_LOG("{}", "WRITE UNLOCK");

                                // notify: we got write stream
                                task->quic_m_write.unlock();

                                task->quic_m_worker.lock();
                            }
                            else if (task->buff_type == MANAPI_HTTP_BUFF_FILE)
                            {
                                auto fileData = &task->fti;
                                std::ifstream f (fileData->filePath, std::ios::in | std::ios::binary);
                                if (f.is_open()) {
                                    manapi::utils::before_delete close_stream ([&f] () -> void { f.close(); });
                                    const ssize_t capacity = std::min (quiche_conn_stream_capacity(conn_io->conn, stream_id), fileData->size);
                                    if (capacity < 0) { task->is_deleting = true; task->quic_m_write.unlock(); continue; }
                                    uint8_t buff [capacity];
                                    f.seekg(fileData->start);
                                    f.read(reinterpret_cast<char *> (buff), capacity);
                                    const bool final = capacity == fileData->size;
                                    const ssize_t written = quiche_h3_send_body(conn_io->http3, conn_io->conn, stream_id, buff, capacity, final);
                                    if (written < 0) {
                                        task->quic_m_write.unlock();
                                        THROW_MANAPI_EXCEPTION("{}" ,"ERROR");
                                    }
                                    fileData->start += written;
                                    fileData->size -= written;
                                    if (final) { task->quic_m_write.unlock(); }
                                    if (fileData->size < 0) { MANAPI_LOG("fileData->size < 0, = {}. This is a bug", fileData->size); task->quic_m_write.unlock(); }
                                }
                            }
                        }
                    }
                }
            }



            // UNLOCK
            lk.unlock();

            quiche_h3_event *ev;
            while (true) {
                stream_id = quiche_h3_conn_poll(conn_io->http3,conn_io->conn, &ev);

                if (stream_id < 0)
                {
                    break;
                }

                utils::before_delete unwrap_event ([&ev] () -> void { quiche_h3_event_free(ev); });

                switch (quiche_h3_event_type(ev)) {
                    case QUICHE_H3_EVENT_FINISHED: {
                        MANAPI_LOG("{}", "FINISHED");
                        break;
                    }

                    case QUICHE_H3_EVENT_HEADERS: {
                        auto task = new http_task (config->get_socket_fd(), client, client_len, site, config, CONN_UDP);
                        utils::before_delete bd_task_if_error ([&task] () -> void { delete task; });

                        const int rc = quiche_h3_event_for_each_header(ev, quic_get_header, &task->request_data);

                        if (rc != 0)
                        {
                            THROW_MANAPI_EXCEPTION("{}", "failed to process headers: quiche_h3_event_for_each_header(...) != 0");
                        }
                        //TODO: resolve if the first connection is timeout -> second also
                        std::lock_guard<std::timed_mutex> lock_worker (task->quic_m_worker);

                        task->conn_io = conn_io;
                        task->stream_id = stream_id;
                        task->request_data.has_body = quiche_h3_event_headers_has_body (ev);
                        // task->to_delete   = false;
                        task->is_deleting = false;

                        if (conn_io->tasks.contains(stream_id))
                        {
                            THROW_MANAPI_EXCEPTION("stream_id {} exists", stream_id);
                        }

                        // LOCK
                        lk.lock();

                        conn_io->tasks.insert({stream_id, task});
                        // without occures
                        bd_task_if_error.disable();
                        // add a task to the tasks pool
                        site->append_task(task, 2);

                        // UNLOCK
                        lk.unlock();

                        // wait the init connection
                        if (!task->quic_m_worker.try_lock_for(std::chrono::seconds (100)))
                        {
                            task->is_deleting = true;
                            MANAPI_LOG("quic_m_worker.try_lock_for(...) timeout \n"
                                                   "id: {}", conn_io->key);
                        }
                        break;
                    }

                    case QUICHE_H3_EVENT_DATA: {
                        if (conn_io->tasks.contains(stream_id))
                        {

                            auto task = reinterpret_cast <http_task *> (conn_io->tasks.at(stream_id));

                            if (!task->quic_v_body || task->is_deleting)
                            {
                                break;
                            }


                            {
                                // auto lock/unlock
                                std::lock_guard <std::timed_mutex> lk (task->quic_m_worker);

                                // unlock read function
                                task->quic_m_read.unlock();


                                if (!task->is_deleting)
                                {
                                    // await read function response
                                    task->quic_m_worker.lock();
                                }
                            }
                        }

                        break;
                    }

                    case QUICHE_H3_EVENT_RESET:
                        MANAPI_LOG("{}", "RESET");

                    if (quiche_conn_close(conn_io->conn, true, 0, nullptr, 0) < 0)
                    {
                        THROW_MANAPI_EXCEPTION("failed to close connection: {}", conn_io->key);
                    }

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
            }
        }


        if (conn_io->is_deleting) { return; }
        http_task::quic_flush_egress(quic_map_conns, conn_io, site);
    }

    //config->append_task(new function_task ([config] () -> void { http_task::quic_generate_output_packages (config); }), 2);
}

void manapi::net::http_task::quic_generate_output_packages (QUIC_MAP_CONNS_T *quic_map_conns, class site *site) {
    {
        quic_map_conns->lock();
        utils::before_delete unwrap ([&quic_map_conns] () -> void { quic_map_conns->unlock(); });

        auto it = quic_map_conns->begin();

        while (true) {

            if (it == quic_map_conns->end())
            {
                break;
            }

            if (!it->second->is_deleting) {
                std::unique_lock <std::mutex> lock_connection (it->second->mutex, std::try_to_lock);
                if (!it->second->is_responsing && !it->second->is_deleting && lock_connection.owns_lock()) {
                    http_task::quic_flush_egress(quic_map_conns, it->second, site);

                    if (quiche_conn_is_closed(it->second->conn)) {
                        it->second->is_deleting = true;

                        quiche_stats stats;
                        quiche_path_stats path_stats;

                        quiche_conn_stats(it->second->conn, &stats);
                        quiche_conn_path_stats(it->second->conn, 0, &path_stats);

                        MANAPI_LOG("connection closed, recv={} sent={} lost={} rtt={} ns cwnd={}",
                                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

                        // multi thread
                        auto conn_io = it->second;


                        lock_connection.unlock();

                        it = quic_map_conns->erase(it);

                        http_task::quic_delete_conn_io(conn_io, site);

                        MANAPI_LOG("connections: {}", quic_map_conns->size());

                        continue;
                    }
                }
            }
            it++;
        }
    }
}

bool manapi::net::http_task::socket_wait_select() const {
    struct timeval timeout{};

    fd_set read_fds;

    FD_ZERO (&read_fds);
    FD_SET  (conn_fd, &read_fds);

    timeout.tv_sec = config->get_recv_timeout(); // keep alive in n sec
    timeout.tv_usec = 0;

    int ready = select(conn_fd + 1, &read_fds, nullptr, nullptr, &timeout);

    if (ready < 0)
    {
        THROW_MANAPI_EXCEPTION ("unknow socket status (select() < 0): {}", conn_fd);
    }

    else if (ready == 0)
    {
        THROW_MANAPI_EXCEPTION("The waiting time of {} seconds has been exceeded", config->get_recv_timeout());
    }

    const bool result = FD_ISSET(conn_fd, &read_fds);

    FD_CLR(conn_fd, &read_fds);

    return result;
}

void manapi::net::http_task::quic_set_to_delete(http_task *task) {
    if (!task->is_deleting)
    {
        task->is_deleting = true;
        task->stream_id = -1;
        // unlock read/write
        task->quic_m_write.unlock();
        task->quic_m_read.unlock();

        // unlock workers
        task->quic_m_worker.unlock();
    }
}

// tcp doit (pool connections)
void manapi::net::http_task::tcp_doit() {
    utils::before_delete bd_tcp_doit ([this] () -> void {
        if (ssl != nullptr)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }

        close(conn_fd);
    });
    if (socket_wait_select())
    {
        try
        {
            if (ssl != nullptr)
            {
                SSL_set_fd(ssl, conn_fd);

                mask_write  = [this](auto && PH1, auto && PH2) -> ssize_t { return openssl_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };
                mask_read   = [this](auto && PH1, auto && PH2) -> ssize_t { return openssl_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };

                const auto res = SSL_accept(ssl);

                if (!res)
                {
                    THROW_MANAPI_EXCEPTION("couldnt SSL accept: SSL_accept(ssl) = {}", res);
                }
            }
            else
            {
                mask_write  = [this](auto && PH1, auto && PH2) -> ssize_t { return socket_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };
                mask_read   = [this](auto && PH1, auto && PH2) -> ssize_t { return socket_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); };
            }

            mask_response = [this](auto && PH1) { return tcp_send_response(std::forward<decltype(PH1)>(PH1)); };

            const ssize_t size = read_next();

            if (size == -1)
            {
                THROW_MANAPI_EXCEPTION("Could not read from the socket: {}", conn_fd);
            }

            else
            {
                const size_t *socket_block_size = &config->get_socket_block_size();

                request_data.body_index = 0;
                request_data.body_size = size;

                tcp_parse_request_response((char *) buff, *socket_block_size, request_data.body_index);

                request_data.has_body = request_data.headers.contains(HTTP_HEADER.CONTENT_LENGTH);

                const auto handler = site->get_handler(request_data);

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

                    request_data.body_size = std::stoull(request_data.headers[HTTP_HEADER.CONTENT_LENGTH]);
                    request_data.body_left = request_data.body_size;
                    request_data.body_ptr = (char *)buff + request_data.body_index * sizeof(char);

                    request_data.body_index = 0;
                }
                else
                {
                    request_data.body_ptr = nullptr;
                    request_data.body_size = 0;
                }

                handle_request(&handler);
            }
        }
        catch (const manapi::utils::exception &e)
        {
            MANAPI_LOG("close connection: {}", e.what());
        }
    }
}

// HTTP

void manapi::net::http_task::handle_request(const http_handler_page *data, const size_t &status, const std::string &message) {
    http_request    req (socket_information, request_data, this, config, data);
    http_response   res (request_data, status, message, new api::pool (site->get_tasks_pool().get()), config);
    try
    {
        // handle layers
        for (const auto &layer: data->layer) {
            layer->handler (req, res);

            if (!req.get_propagation()) {
                // skip other layers and handlers
                goto finish;
            }
        }

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
                res.set_compress_enabled(true);
                res.set_partial_status(true);
                res.file(path);

                try {
                    send_response(res);
                }
                catch (const std::exception &e) {
                    MANAPI_LOG("Unexpected error: %s", e.what());

                    send_error_response(503, HTTP_STATUS.SERVICE_UNAVAILABLE_503, data->error.get());
                }

                return;
            }
        }

        return send_error_response (404, HTTP_STATUS.NOT_FOUND_404, data->error.get());
    }
        execute_custom_handler (data, req, res);

        finish:
            send_response (res);
    }
    catch (const manapi::utils::exception &e)
    {
        MANAPI_LOG("Unexpected error: {}", e.what());

        send_error_response (503, HTTP_STATUS.SERVICE_UNAVAILABLE_503, data->error.get());
    }
    catch (const std::exception &e)
    {
        MANAPI_LOG("Unexpected error: {}", e.what());

        send_error_response (503, HTTP_STATUS.SERVICE_UNAVAILABLE_503, data->error.get());
    }
}

void manapi::net::http_task::execute_custom_handler(const http_handler_page *handler, http_request &req, http_response &resp) {
    handler->handler->handler (req, resp);
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

std::string manapi::net::http_task::compress_file(const std::string &file, const std::string &folder, const std::string &compress, manapi::utils::compress::TEMPLATE_INTERFACE compressor) const {
    std::string filepath;

    // compressor
    auto cached = site->get_compressed_cache_file(file, compress);

    if (cached == nullptr)
    {
        filepath = compressor (file, &folder);

        site->set_compressed_cache_file(file, filepath, compress);
    }
    else
    {
        filepath = *cached;
    }

    return filepath;
}

void manapi::net::http_task::send_response(manapi::net::http_response &res) {
    std::string response;
    std::string compressed;

    manapi::utils::compress::TEMPLATE_INTERFACE compressor = nullptr;

    auto &compress = res.get_compress();
    auto &body     = res.get_body();


    if (!compress.empty()) {
        if (!res.is_file()                  ||
            !res.get_partial_enabled()      ||
            manapi::filesystem::get_size(res.get_file()) < config->get_partial_data_min_size()
            ) {

            compressor = site->get_compressor(compress);

            if (compressor != nullptr)
            {
                res.set_header(HTTP_HEADER.CONTENT_ENCODING, compress);
            }
        }
    }

    bool exists_replacers = res.get_replacers() != nullptr;
    bool exists_compressor = compressor != nullptr;

    // set time
    res.set_header(HTTP_HEADER.DATE, manapi::utils::time ("%a, %d %b %Y %H:%M:%S GMT", false));

    if (config->get_http_version() < versions::HTTP_v2)
    {
        // if HTTP/0.9, HTTP/1.0 or HTTP/1.1
        res.set_header(HTTP_HEADER.CONNECTION, "close");
    }

    if (res.is_file())
    {
        std::string filepath;

        if (exists_compressor)
        {
            if (exists_replacers)
            {
                THROW_MANAPI_EXCEPTION("{}", "replacers can not be use with compressing");
            }

            filepath = compress_file (res.get_file(), site->config_cache_dir, compress, compressor);
        }

        else
        {
            filepath = res.get_file();
        }

        std::ifstream f;

        f.open(filepath, std::ios::binary | std::ios::in);

        if (!f.is_open())
        {
            THROW_MANAPI_EXCEPTION("Could not open the file by the following path: {}", filepath);
        }

        else
        {
            // close ifstream before deleting
            utils::before_delete unwrap_ifstream ([&f] () -> void { f.close(); });

            // set headers
            {
                std::string mimetype = utils::mime_by_file_path(res.get_file());
                if (mimetype.size() > sizeof ("text"))
                {
                    if (strncmp ("text", mimetype.data(), sizeof ("text") - 1) == 0)
                    {
                        mimetype = utils::stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                    }
                }

                res.set_header(HTTP_HEADER.CONTENT_TYPE, mimetype);
            }
            std::vector<utils::replace_founded_item> replacers;

            // get file size
            const ssize_t fileSize = manapi::filesystem::get_size(f);
            ssize_t dynamicFileSize = fileSize;

            // replacers
            if (exists_replacers)
            {
                replacers = utils::found_replacers_in_file (filepath, 0, fileSize, *res.get_replacers());

                for (const auto &replacer: replacers)
                {
                    dynamicFileSize = (ssize_t) (dynamicFileSize - (replacer.pos.second - replacer.pos.first + 1) + replacer.value->size());
                }
            }

            buff_type = conn_type == CONN_UDP ? MANAPI_HTTP_BUFF_FILE : MANAPI_HTTP_BUFF_BINARY;

            // partial enabled
            if (res.get_partial_enabled() && config->get_partial_data_min_size() <= fileSize)
            {
                if (exists_compressor)
                {
                    THROW_MANAPI_EXCEPTION("the compress '{}' with the partial content is not supported.", utils::escape_string(res.get_compress()));
                }


                if (exists_replacers)
                {
                    THROW_MANAPI_EXCEPTION("{}", "replacers can not be use with partial");
                }

                res.set_status(206, HTTP_STATUS.PARTIAL_CONTENT_206);
                res.set_header(HTTP_HEADER.ACCEPT_RANGES, "bytes");

                ssize_t start   = 0,
                        back    = (ssize_t)(config->get_partial_data_min_size()) - 1,
                        size;

                switch (res.ranges.size())
                {
                    case 1:
                        if (res.ranges[0].first != -1)
                        {
                            start   = res.ranges[0].first;
                        }

                        if (res.ranges[0].second != -1)
                        {
                            back    = res.ranges[0].second;
                        }
                        else
                        {
                            back    = fileSize - 1;
                        }
                    case 0:
                        size = back - start + 1;

                        res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(size));
                        res.set_header(HTTP_HEADER.CONTENT_RANGE, "bytes " + std::to_string(start) + '-' + std::to_string(back) + '/' + std::to_string(fileSize));

                        if (mask_response(res) >= 0)
                        {
                            if (buff_type == MANAPI_HTTP_BUFF_FILE)
                            {
                                mask_write_file (filepath, start, size);
                            }
                            else
                            {
                                // set start position
                                f.seekg(start);
                                // set size and send
                                send_file(res, f, size);
                            }
                        }


                        break;

                    default:
                        THROW_MANAPI_EXCEPTION("{}", "multi bytes unsupported");

                }
            }
            else
            {
                res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(dynamicFileSize));

                if (mask_response (res) >= 0)
                {
                    if (replacers.empty())
                    {
                        // without replacers
                        if (buff_type == MANAPI_HTTP_BUFF_FILE)
                        {
                            mask_write_file (filepath, 0, dynamicFileSize);
                        }
                        else
                        {
                            send_file(res, f, fileSize);
                        }
                    }

                    else
                    {
                        // with replacers
                        send_file(res, f, fileSize, replacers);
                    }
                }
            }

            return;
        }
    }
    else if (res.is_text())
    {
        // may contains decoded / encoded body
        const std::string *plaintext = &body;

        // clean up
        utils::before_delete unwrap_plaintext ([&plaintext] () { delete plaintext; });

        if (compressor != nullptr)
        {
            // encode content !
            plaintext      = new std::string (compressor (body, nullptr));
        }
        else
        {
            // no need to clean up
            unwrap_plaintext.disable();
        }

        res.set_header(HTTP_HEADER.CONTENT_LENGTH, std::to_string(plaintext->size()));

        if (!res.get_headers().contains(HTTP_HEADER.CONTENT_TYPE))
        {
            res.set_header(HTTP_HEADER.CONTENT_TYPE, "text/html; charset=UTF-8");
        }

        if (mask_response (res) >= 0)
        {
            send_text (*plaintext, plaintext->size());
        }
        else
        {
            MANAPI_LOG("{}", "mask_response(...) < 0");
        }

        return;
    }
    else if (res.is_proxy())
    {
        auto proxy = new fetch (res.get_data());

        proxy->handle_headers([this, &res] (const std::map <std::string, std::string> &headers) -> void {
             res.set_status_code(200);
             res.set_status_message(HTTP_STATUS.OK_200);

             if (headers.contains(HTTP_HEADER.CONTENT_LENGTH))
             {
                 res.set_header(HTTP_HEADER.CONTENT_LENGTH, headers.at(HTTP_HEADER.CONTENT_LENGTH));
             }

             mask_response(res);
        });

        proxy->handle_body([this] (char *buffer, const size_t &size) -> size_t {
            const ssize_t sw = mask_write (buffer, size);

            if (sw < 0)
            {
                THROW_MANAPI_EXCEPTION("Could not write pocket: {}", "mask_write() < 0");
            }

            return sw;
        });

        res.tasks->await ((task *) proxy);

        return;
    }

    mask_response (res);
}

void manapi::net::http_task::send_text (const std::string &text, const size_t &size) const {
    const char *current = text.data();
    size_t sent = size;

    while (sent != 0)
    {
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
    auto        block_size = static_cast <ssize_t> (config->get_socket_block_size());
    char        block[block_size];

    ssize_t current = f.tellg();

    size += current;

    while (size != current)
    {
        const ssize_t left = size - current;

        if (left < block_size)
        {
            block_size = left;
        }

        f.read(block, block_size);

        const ssize_t sent = mask_write(block, block_size);

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
    auto        block_size = (ssize_t) (config->get_socket_block_size());

    block.resize(block_size);

    ssize_t current = f.tellg();

    size += current;

    ssize_t index;
    ssize_t replacer_index = 0;
    ssize_t current_key_index = 0;

    while (size > current)
    {
        const ssize_t left = size - current;

        if (left < block_size)
        {
            block_size = left;
        }

        f.read(block.data(), block_size);

        index = f.tellg();

        ssize_t shift = 0;

        while (replacer_index != replacers.size() && index > replacers[replacer_index].pos.first + shift)
        {
            const ssize_t key_size = replacers[replacer_index].pos.second - replacers[replacer_index].pos.first + 1;
            const ssize_t start_index_in_block = block_size - (index - (replacers[replacer_index].pos.first + shift + current_key_index));

            ssize_t index_in_block = start_index_in_block;

            for (; index_in_block < block_size && current_key_index < key_size; index_in_block++, current_key_index++)
            {
                if (current_key_index < replacers[replacer_index].value->size())
                {
                    block[index_in_block] = replacers[replacer_index].value->at(current_key_index);
                    continue;
                }

                break;
            }

            // if KEY_SIZE >= VALUE_SIZE
            if (current_key_index == replacers[replacer_index].value->size())
            {
                // shift <<
                // key_size <- the shift
                const ssize_t shifted_index_in_block = start_index_in_block + key_size;
                const ssize_t key_left_size = key_size - current_key_index;

                // shift chars
                ssize_t i = shifted_index_in_block;
                for (; i < block_size; i++, index_in_block++)
                {
                    block[index_in_block] = block[i];
                }

                if (index_in_block < block_size)
                {
                    const auto needed = block_size - index_in_block;

                    // we want to align block to block_size if it possible
                    if (index < size)
                    {

                        // + 1 bcz we dont want to grab } special symbol at the end of the special key {{KEY}}
                        current = (index - block_size) + i;

                        f.seekg(current);
                        f.read(block.data() + index_in_block * sizeof(char), needed);
                    }
                    else
                    {
                        // no data left
                        //shift -= key_left_size;
                        // decrease block_size
                        block_size -= key_left_size;
                    }

                    // update current
                    current = f.tellg() - block_size;
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
            if (current_key_index == key_size && current_key_index < replacers[replacer_index].value->size()) {
                next:
                // shift >>
                bool repeat = false;

                const auto value_left_size = static_cast<ssize_t> (replacers[replacer_index].value->size()) - current_key_index;

                // replace
                ssize_t i = index_in_block;
                ssize_t free_space = block_size - i;

                if (free_space < value_left_size)
                {
                    block_size += value_left_size - free_space;

                    if (block_size >= config->get_socket_block_size())
                    {
                        block_size = (ssize_t) config->get_socket_block_size();
                        repeat = true;
                    }
                }

                for (; i < block_size && current_key_index < replacers[replacer_index].value->size(); i++, current_key_index++)
                {
                    char a = replacers[replacer_index].value->at(current_key_index);
                    block[i] = a;
                }

                if (repeat)
                {
                    ssize_t sent = mask_write(block.data(), block_size);

                    if (sent < 0)
                    {
                        // cannot to send
                        return;
                    }

                    current += sent;

                    shift   += block_size - index_in_block;

                    // resolve
                    index_in_block = 0;

                    goto next;
                }

                if (i > block_size)
                {
                   // -printf("OK\n");
                }
                else
                {
                    free_space = block_size - i;
                    ssize_t can_read = (ssize_t) config->get_socket_block_size() - i;

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
        {
            // cannot to send
            break;
        }

        current += sent;

        f.seekg(current);
    }

}

ssize_t manapi::net::http_task::read_next() const {
    return mask_read(reinterpret_cast <char *>(buff), config->get_socket_block_size());
}

void manapi::net::http_task::send_error_response(const size_t &status, const std::string &message, const http_handler_page *error) {
    if (error == nullptr)
    {
        // TODO: Default error page
        return;
    }

    handle_request(error, status, message);
}

// MASKS

ssize_t manapi::net::http_task::socket_read(char *part_buff, const size_t &part_buff_size) const {
    if (!socket_wait_select ())
    {
        return -1;
    }

    return read (conn_fd, part_buff, part_buff_size);
}

ssize_t manapi::net::http_task::socket_write(const char *part_buff, const size_t &part_buff_size) const {
    return send (conn_fd, part_buff, part_buff_size, MSG_NOSIGNAL);
}

ssize_t manapi::net::http_task::openssl_read(char *part_buff, const size_t &part_buff_size) const {
    return SSL_read (ssl, part_buff, reinterpret_cast <const int &> (part_buff_size));
}

ssize_t manapi::net::http_task::openssl_write(const char *part_buff, const size_t &part_buff_size) const {
    return SSL_write (ssl, part_buff, reinterpret_cast <const int &> (part_buff_size));
}

// TCP

void manapi::net::http_task::tcp_stringify_headers(std::string &response, manapi::net::http_response &res, char *delimiter) {
    // add headers
    for (const auto& header : res.get_headers())
    {
        response += header.first + ": " + header.second + delimiter;
    }
}

void manapi::net::http_task::tcp_stringify_http_info(std::string &response, manapi::net::http_response &res, char *delimiter) const {
    response += "HTTP/" + config->get_http_version_str() + ' ' + std::to_string(res.get_status_code())
                + ' ' + res.get_status_message() + delimiter;
}

void manapi::net::http_task::tcp_parse_request_response(char *response, const size_t &size, size_t &i) {
    request_data.headers_size = 0;

    size_t maxsize = config->get_max_header_block_size();

    // Method
    for (; i < maxsize; i++)
    {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == ' ')
        {
            break;
        }

        request_data.method += response[i];
    }


    // URI
    // scip \n
    i++;
    parse_uri_path_dynamic (request_data, (char *) response, size, i, [&maxsize, this, &size, &i] () { MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i); });

    // HTTP
    for (i++; i < maxsize; i++)
    {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == '\r')
        {
            continue;
        }

        if (response[i] == '\n')
        {
            break;
        }

        request_data.http += response[i];
    }



    // Headers
    std::string key;
    std::string *value;

    bool is_key = true;
    bool dbl    = false;

    for (i++; i < maxsize; i++)
    {
        MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

        if (response[i] == '\n') {
            is_key  = true;
            if (!key.empty())
            {
                key = "";
            }

            // double \n
            if (dbl)
            {
                i++;
                break;
            }

            dbl = true;
            continue;
        }

        if (response[i] == '\r')
        {
            continue;
        }

        else if (response[i] == ':' && is_key) {
            is_key  = false;
            value   = &request_data.headers[key];

            continue;
        }

        if (dbl)
        {
            dbl = false;
        }



        if (is_key)
        {
            key     += (char)std::tolower(response[i]);
        }
        else
        {
            if (response[i] == ' ' && value->empty())
            {
                continue;
            }

            *value  += response[i];
        }
    }

    if (maxsize == i)
    {
        THROW_MANAPI_EXCEPTION ("request header is too large. Max: {}", maxsize);
    }

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
    const int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        THROW_MANAPI_EXCEPTION("{}", "failed to open /dev/urandom: open(...) < 0");
        return nullptr;
    }

    const ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        THROW_MANAPI_EXCEPTION ("{}", "failed to create connection ID: read(...) < 0");
        return nullptr;
    }

    return cid;
}

void manapi::net::http_task::quic_flush_egress(QUIC_MAP_CONNS_T *conns, manapi::net::http_quic_conn_io *conn_io, class site *site) {
    uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        if (conn_io->conn == nullptr) {
            MANAPI_LOG("ERROR: {}", "conn_io->conn = nullptr");
        }

        const ssize_t written = quiche_conn_send(conn_io->conn, out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            //std::cout << "done writing"<< conn_io->key << "\n";
            break;
        }

        if (written < 0) {
            MANAPI_LOG("failed to create packet: {}", written);
            return;
        }

        const ssize_t sent = sendto(conn_io->sock_fd, out, written, 0, (struct sockaddr *) &conn_io->peer_addr, conn_io->peer_addr_len);
        if (sent != written) {
            THROW_MANAPI_EXCEPTION ("{}", "failed to send: sendto(...) != written");
            return;
        }

        //fprintf(stderr, "sent %zd bytes\n", sent);
    }

    auto a = quiche_conn_timeout_as_millis(conn_io->conn);
    if (a == -1) { return; }
    //a *= 1000;
    // if (a > 100000) {
    //     a = 100000;
    // }
    //std::cerr << a << "\n";
    const auto &key = conn_io->key;
    const auto func = [key, site, conns] () -> void { quic_timeout_cb(key, conns, site); };
    site->remove_timer(conn_io->timer_id);
    if (a == 0) { site->append_task(new function_task (func)); }
    else { conn_io->timer_id = site->append_timer(std::chrono::milliseconds(a), func); }
}

int manapi::net::http_task::quic_get_header(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) {
    auto request_data = static_cast <request_data_t *> (argp);

    if (name_len <= 0 || value_len <= 0)
    {
        return -1;
    }

    const auto name_string = std::move(std::string (reinterpret_cast <const char *> (name), name_len));

    // no header
    if (name_string[0] == ':')
    {
        if (name_string == ":method")
        {
            request_data->method = std::move(std::string (reinterpret_cast<const char *> (value), value_len));
        }

        else if (name_string == ":scheme")
        {

        }

        else if (name_string == ":authority")
        {

        }

        else if (name_string == ":path") {
            size_t i = 0;

            parse_uri_path_dynamic (*request_data, (char *) value, value_len, i);
        }
    }
    // header

    request_data->headers.insert({
        std::move(std::string((const char *) name, name_len)),
        std::move(std::string((const char *) value, value_len))
    });

    return 0;
}

manapi::net::http_quic_conn_io * manapi::net::http_task::quic_create_connection(uint8_t *s_cid, size_t s_cid_len, uint8_t *od_cid, size_t od_cid_len, const int &conn_fd, const sockaddr_storage &client, const socklen_t &client_len, class config *config, class site *site, QUIC_MAP_CONNS_T *quic_map_conns) {
    if (s_cid_len != MANAPI_QUIC_CONNECTION_ID_LEN) {
        THROW_MANAPI_EXCEPTION ("{}", "failed, s_cid length too short");
        return nullptr;
    }

    auto conn_io = new http_quic_conn_io ();
    conn_io->timer_id = 0; // initializate timer
    utils::before_delete bd_free_conn_io ([&conn_io, &site] () -> void {
        // LOCK CONN
        conn_io->mutex.lock();

        // STOP TIMER
        site->remove_timer(conn_io->timer_id);

        // CONN EXISTS
        if (conn_io->conn != nullptr)
        {
            // FREE QUICHE CONN
            quiche_conn_free(conn_io->conn);
        }

        // FREE
        delete conn_io;
    });

    {
        std::lock_guard<std::mutex> lk (conn_io->mutex);

        conn_io->key = std::string(reinterpret_cast<const char *> (s_cid), s_cid_len);

        conn_io->conn = quiche_accept(reinterpret_cast<uint8_t *> (conn_io->key.data()), conn_io->key.size(), od_cid, od_cid_len, &config->get_server_address(), config->get_server_len(),
                                          reinterpret_cast<const struct sockaddr *>(&client), client_len, config->get_quic_config());

        if (conn_io->conn == nullptr)
        {
            THROW_MANAPI_EXCEPTION ("{}", "failed to create connection: quiche_accept(...) = nullptr");
        }

        conn_io->sock_fd        = conn_fd;

        conn_io->peer_addr      = client;
        conn_io->peer_addr_len  = client_len;

        conn_io->timer_id       = 0;

        quic_map_conns->lock();
        const bool result_insert = quic_map_conns->insert(conn_io->key, conn_io).second;
        quic_map_conns->unlock();

        if (!result_insert)
        {
            return nullptr;
        }
    }

    bd_free_conn_io.disable();

    return conn_io;
}

void manapi::net::http_task::quic_timeout_cb(std::string cid, QUIC_MAP_CONNS_T *conns, class site *site) {
    http_quic_conn_io *p;
    {
        conns->lock();
        utils::before_delete lock ([&conns] () -> void { conns->unlock(); });
        if (!conns->contains(cid)) { return; }
        p = conns->at(cid);
        if (p->is_deleting) { return; }
    }
    std::unique_lock<std::mutex> lk (p->mutex);
    if (!conns->contains(cid) || p->is_deleting) { return; }
    p->timer_id = 0;
    // TODO: WHY DOWNLOAD DATA BY CLIENT CAUSED BY THIS ??!!
    quiche_conn_on_timeout(p->conn);

    MANAPI_LOG("timeout: {}", p->key);
    quic_flush_egress(conns, p, site);

    if (!p->is_responsing && quiche_conn_is_closed(p->conn)) {
        p->is_deleting = true;

        {
            conns->lock();
            // SLOW UNLOCK

            utils::before_delete unwrap_map ([&conns] () -> void { conns->unlock(); });

            conns->erase(p->key);

            lk.unlock();
        }

        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(p->conn, &stats);
        quiche_conn_path_stats(p->conn, 0, &path_stats);

        MANAPI_LOG("(timer) connection closed: {}, recv={} sent={} lost={} rtt={} ns cwnd={}",
                p->key, stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        quic_delete_conn_io (p, site);

        return;
    }

    // no way :(
    // This connection is used by the main pool
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
            {
                break;
            }

            finished ();
        }

        if (response[i] == ' ')
        {
            break;
        }

        request_data.uri += response[i];

        if (hex_index >= 0)
        {
            hex_symbols[hex_index] = response[i];

            if (hex_index == 1)
            {
                char x = (char)(manapi::utils::hex2dec(hex_symbols[0]) << 4 | manapi::utils::hex2dec(hex_symbols[1]));

                if (true || manapi::utils::valid_special_symbol(x))
                {
                    request_data.path.back()    += x;
                }
                else
                {
                    request_data.path.back()    += '%';
                    request_data.path.back()    += hex_symbols;
                }

                hex_index = -1;

                continue;
            }

            hex_index++;

            continue;
        }

        if (response[i] == '%' && !request_data.path.empty()) {
            hex_index = 0;

            continue;
        }

        if (request_data.divided == -1) {
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
        {
            request_data.path.back() += response[i];
        }
    }

    if (request_data.path.empty())
    {
        return;
    }


    for (size_t j = request_data.path.size() - 1;;) {
        if (request_data.path[j].empty())
        {
            request_data.path.pop_back();
        }


        if (j == 0)
        {
            break;
        }

        j--;
    }
}

void manapi::net::http_task::quic_delete_conn_io(manapi::net::http_quic_conn_io *conn_io, class site *site) {
    {
        //LOCK
        std::lock_guard<std::mutex> lk (conn_io->mutex);
        if (conn_io->is_responsing) {
            MANAPI_LOG("{}", "conn_io->is_responsing = true");
            return;
        }
        conn_io->is_deleting = true;
        // stop timer
        site->remove_timer(conn_io->timer_id);

        // WAIT LOCK
        //std::lock_guard<std::timed_mutex> lk (conn_io->wait);

        // force clear tasks
        for (auto it = conn_io->tasks.begin(); it != conn_io->tasks.end();)
        {
            auto task = reinterpret_cast <http_task *> (it->second);

            if (task == nullptr)
            {
                continue;
            }

            // UNLOCK
            //conn_io->mutex.unlock();
            //utils::before_delete bd_mutex_lock ([&conn_io] () -> void { conn_io->mutex.lock(); });

            // LOCK & UNLOCK
            quic_set_to_delete(task);

            // AS (thread of the task)->join()
            // while (true)
            // {
            //     if (conn_io->wait.try_lock_for(std::chrono::milliseconds(50)))
            //     {
            //         break;
            //     }
            //
            //     MANAPI_LOG("wait for the task ({}) to complete", it->first);
            //
            //     return;
            // }

            it = conn_io->tasks.erase(it);
            // LOCK
        }

        // UNLOCk
    }
    quiche_conn_free (conn_io->conn);

    delete conn_io;
}


