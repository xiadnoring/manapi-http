#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
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
#include "ManapiTaskHttp.h"
#include "ManapiCompress.h"
#include "ManapiFilesystem.h"

#define MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(_x) if (_x == size) {             \
                                request_data.headers_size += _x;                \
                                if (!manapi::net::http_task::read_next_part (maxsize, _x, this, &request_data)) \
                                    return;                                     \
                                }

#define MANAPI_QUIC_CONNECTION_ID_LEN 16

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
            throw manapi::toolbox::manapi_exception ("invalid connection protocol");
    }
}

// udp doit (single connections)
void manapi::net::http_task::udp_doit() {
    static uint8_t out[4096];

    uint8_t     type;
    uint32_t    version;

    uint8_t     scid[QUICHE_MAX_CONN_ID_LEN];
    size_t      scid_len = sizeof (scid);

    uint8_t     dcid[QUICHE_MAX_CONN_ID_LEN];
    size_t      dcid_len = sizeof (scid);

    uint8_t     odcid[QUICHE_MAX_CONN_ID_LEN];
    size_t      odcid_len = sizeof (scid);

    uint8_t     token[quic_token_max_len];
    size_t      token_len = sizeof(token);

    int rc = quiche_header_info(buff, buff_size, MANAPI_QUIC_CONNECTION_ID_LEN, &version, &type,
                                scid, &scid_len, dcid, &dcid_len, token, &token_len);

    if (rc < 0)
        throw manapi::toolbox::manapi_exception ("failed to parse quic headers.");

    struct http_qc_conn_io *conn_io;

    const std::string dcid_str ((const char *) (dcid), dcid_len);

    conn_io = q_map_conns->contains(dcid_str) ? q_map_conns->at(dcid_str) : nullptr;

    if (conn_io == nullptr) {
        // no connections in the history
        if (!quiche_version_is_supported(version)) {
            MANAPI_LOG("version negotiation: %zu", version);

            ssize_t written = quiche_negotiate_version(scid, scid_len,
                                                       dcid, dcid_len,
                                                       out, sizeof(out));

            if (written < 0) {
                fprintf(stderr, "failed to create vneg packet: %zd\n",
                        written);
                return;
            }

            ssize_t sent = sendto(conn_fd, out, written, 0,
                                  (struct sockaddr *) &client,
                                  client_len);

            if (sent != written) {
                perror("failed to send");
                return;
            }

            fprintf(stderr, "sent %zd bytes\n", sent);
            return;
        }

        if (token_len == 0) {
            printf("stateless retry\n");

            mint_token(dcid, dcid_len, &client, client_len,
                       token, &token_len);

            uint8_t new_cid[MANAPI_QUIC_CONNECTION_ID_LEN];

            if (gen_cid(new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) == nullptr) {
                return;
            }

            std::cout << "new scid -> " << std::string((char *)new_cid, MANAPI_QUIC_CONNECTION_ID_LEN) << "\n";

            ssize_t written = quiche_retry(scid, scid_len,
                                           dcid, dcid_len,
                                           new_cid, MANAPI_QUIC_CONNECTION_ID_LEN,
                                           token, token_len,
                                           version, out, sizeof(out));

            if (written < 0) {
                fprintf(stderr, "failed to create retry packet: %zd\n",
                        written);
                return;
            }

            ssize_t sent = sendto(conn_fd, out, written, 0,
                                  (struct sockaddr *) &client,
                                  client_len);
            if (sent != written) {
                perror("failed to send");
                return;
            }

            printf(" -> sent %zd bytes\n", sent);
            return;
        }

        if (!validate_token(token, token_len, &client, client_len,
                            odcid, &odcid_len)) {
            fprintf(stderr, "invalid address validation token\n");
            return;
        }


        conn_io = quic_create_connection(dcid, dcid_len, odcid, odcid_len);

        if (conn_io == nullptr) {
            printf("RETURN!\n");
            return;
        }
    }

    std::lock_guard <std::mutex> lock (conn_io->mutex);

    quiche_recv_info recv_info = {
            (struct sockaddr *)&client,
            client_len,
            http_server->server_addr,
            http_server->server_len
    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buff, buff_size, &recv_info);

    if (done < 0) {
        printf("failed to process packet: %zd\n", done);
        return;
    }

    printf("success recv %zd bytes\n", done);


    if (quiche_conn_is_established(conn_io->conn)) {
        printf("ESTABLISHED\n");
        quiche_h3_event *ev;

        if (conn_io->http3 == nullptr) {
            conn_io->http3 = quiche_h3_conn_new_with_transport(conn_io->conn, http_server->http3_config);

            if (conn_io->http3 == nullptr) {
                fprintf(stderr, "failed to create HTTP/3 connection\n");
                return;
            }
        }

        int64_t s;

        // set wrappers for I/O
        mask_read = [conn_io, &s] (const char *buff, const size_t &buff_size) {
            return quiche_h3_recv_body(conn_io->http3, conn_io->conn, s, (uint8_t *) buff, buff_size);
        };

        mask_write = [conn_io, &s] (const char *buff, const size_t &buff_size) {
            auto a = quiche_h3_send_body(conn_io->http3, conn_io->conn, s, (uint8_t *) buff, buff_size, false);
            std::cout << "SEND: " << a << "\n";
            return a;
        };

        mask_response = [conn_io, &s] (http_response &res) {
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

        while (true) {
            s = quiche_h3_conn_poll(conn_io->http3,conn_io->conn, &ev);

            if (s < 0) {
                break;
            }

            switch (quiche_h3_event_type(ev)) {
                case QUICHE_H3_EVENT_FINISHED: {
                    printf("FINISH\n");

                    const auto handler = std::move(http_server->get_handler(request_data));

                    if (request_data.has_body) {
                        if (!request_data.headers.contains(http_header.CONTENT_LENGTH))
                            throw manapi::toolbox::manapi_exception("content-length not exists");

                        request_data.body_size  = std::stoull(request_data.headers[http_header.CONTENT_LENGTH]);
                        request_data.body_left  = request_data.body_size;
                        request_data.body_ptr   = (char *)buff;

                        request_data.body_index = 0;
                    }
                    else {
                        request_data.body_ptr   = nullptr;
                        request_data.body_size  = 0;
                    }

                    handle_request(&handler);

                    quiche_h3_send_body(conn_io->http3, conn_io->conn, s, nullptr, 0, true);

                    break;
                }
                case QUICHE_H3_EVENT_HEADERS: {
                    int rc = quiche_h3_event_for_each_header(ev, quic_get_header, &request_data);

                    printf("OPENED: ");

                    for (auto part: request_data.path)
                        printf("%s / ", part.data());

                    printf("\n");

                    if (rc != 0)
                        fprintf(stderr, "failed to process headers\n");

                    break;
                }

                case QUICHE_H3_EVENT_DATA: {
                    printf("HTTP DATA");
                    break;
                }

                case QUICHE_H3_EVENT_RESET:
                    printf("RESET\n");
                    break;

                case QUICHE_H3_EVENT_PRIORITY_UPDATE:
                    printf("PRIORITY_UPDATE\n");
                    break;

                case QUICHE_H3_EVENT_GOAWAY: {
                    fprintf(stderr, "got GOAWAY\n");
                    break;
                }

                default:
                    printf("INVALID EVENT\n");
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
        perror("select");

    else if (ready == 0)
        MANAPI_LOG("The waiting time of %llu seconds has been exceeded", http_server->get_keep_alive());

    else {
        if (FD_ISSET(conn_fd, &read_fds)) {
            try {
                if (ssl != nullptr) {
                    SSL_set_fd(ssl, conn_fd);

                    mask_write  = std::bind(&http_task::openssl_write, this, std::placeholders::_1, std::placeholders::_2);
                    mask_read   = std::bind(&http_task::openssl_read, this, std::placeholders::_1, std::placeholders::_2);

                    if (!SSL_accept(ssl))
                        throw manapi::toolbox::manapi_exception ("cannot SSL accept!");
                }
                else {
                    mask_write  = std::bind(&http_task::socket_write, this, std::placeholders::_1, std::placeholders::_2);
                    mask_read   = std::bind(&http_task::socket_read, this, std::placeholders::_1, std::placeholders::_2);
                }

                mask_response = std::bind (&http_task::tcp_send_response, this, std::placeholders::_1);

                ssize_t size = read_next();

                if (size == -1)
                    MANAPI_LOG("Cannot read from socket %d", conn_fd);

                else {
                    const size_t *socket_block_size = &reinterpret_cast<http *> (http_server)->get_socket_block_size();

                    request_data.body_index = 0;
                    request_data.body_size = size;

                    tcp_parse_request_response(buff, *socket_block_size, request_data.body_index);

                    const auto handler = std::move(http_server->get_handler(request_data));

                    if (request_data.has_body) {
                        if (!request_data.headers.contains(http_header.CONTENT_LENGTH))
                            throw manapi::toolbox::manapi_exception("content-length not exists");

                        request_data.body_size = std::stoull(request_data.headers[http_header.CONTENT_LENGTH]);
                        request_data.body_left = request_data.body_size;
                        request_data.body_ptr = (char *)buff + request_data.body_index * sizeof(char);

                        request_data.body_index = 0;
                    } else {
                        request_data.body_ptr = nullptr;
                        request_data.body_size = 0;
                    }

                    handle_request(&handler);
                }
            }
            catch (const manapi::toolbox::manapi_exception &e) {
                MANAPI_LOG("close connect: %s", e.what());
            }
        }
    }

    close(conn_fd);
}

// HTTP

void manapi::net::http_task::handle_request(const http_handler_page *handler, const size_t &status, const std::string &message) {
    get_ip_addr();

    // ?)
    if (handler->handler == nullptr)
        return send_error_response (404, http_status.NOT_FOUND_404, handler->errors);

    struct ip_data_t ip_data {
            .ip     = ip,
            .family = client.ss_family
    };

    http_request    req (ip_data, request_data, this, http_server);
    http_response   res (request_data, status, message, http_server);

    try {
        handler->handler (req, res);

        send_response (res);
    }
    catch (const std::exception &e) {
        MANAPI_LOG("Unexpected error: %s", e.what());

        send_error_response (503, http_status.SERVICE_UNAVAILABLE_503, handler->errors);
    }
}

void manapi::net::http_task::get_ip_addr() {
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

bool manapi::net::http_task::read_next_part(size_t &size, size_t &i, void *_http_task, request_data_t *request_data) {
    const ssize_t next_block    = reinterpret_cast<http_task *> (_http_task)->read_next();

    if (next_block == -1)
        throw manapi::toolbox::manapi_exception ("socket read error");

    if (next_block == 0)
        return false;

    if (next_block > request_data->body_size)
        throw manapi::toolbox::manapi_exception ("body limit");

    //printf("-> %zu %zu , %zu\n", size, i, next_block);

    size        -=  i;
    i           =   0;

    request_data->body_ptr              = (char *) reinterpret_cast<http_task *> (_http_task)->buff;

    return true;
}

std::string manapi::net::http_task::compress_file(const std::string &file, const std::string &folder, const std::string *compress, manapi::toolbox::compress::TEMPLATE_INTERFACE compressor) const {
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
    //char delimiter[] = "\r\n\0";

    std::string response;
    std::string compressed;

    manapi::toolbox::compress::TEMPLATE_INTERFACE compressor = nullptr;

    auto compress = &res.get_compress();
    auto body     = &res.get_body();

    if (!compress->empty()) {
        if (!res.is_sendfile()              ||
            !res.get_auto_partial_enabled() ||
            manapi::toolbox::filesystem::get_size(res.get_sendfile()) < http_server->get_partial_data_min_size()) {

            compressor = http_server->get_compressor(*compress);

            if (compressor != nullptr)
                res.set_header(http_header.CONTENT_ENCODING, *compress);
        }
    }

    // set time
    res.set_header(http_header.DATE, manapi::toolbox::time ("%a, %d %b %Y %H:%M:%S GMT", false));

    if (res.is_sendfile()) {
        std::string filepath;

        if (compressor != nullptr)
            filepath = compress_file (res.get_sendfile(), *http_server->config_cache_dir, compress, compressor);

        else
            filepath = res.get_sendfile();

        std::ifstream f;

        f.open(filepath, std::ios::binary);

        if (!f.is_open()) {
            MANAPI_LOG("can not open the file on the path: %s", filepath.data());

            res.set_status(500, http_status.INTERNAL_SERVER_ERROR_500);
        }
        else {
            // get file size
            const ssize_t fileSize = manapi::toolbox::filesystem::get_size(f);

            // partial enabled
            if (res.get_auto_partial_enabled() && http_server->get_partial_data_min_size() <= fileSize) {
                if (compressor != nullptr) {
                    f.close();
                    throw manapi::toolbox::manapi_exception ("the compress with the partial content is not supported.");
                }

                res.set_status(206, http_status.PARTIAL_CONTENT_206);
                res.set_header(http_header.ACCEPT_RANGES, "bytes");

                ssize_t start   = 0,
                        back    = (ssize_t)(http_server->get_partial_data_min_size()) - 1,
                        size;

                switch (res.ranges.size()) {
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

                        if (mask_response(res) != -1)
                            // set size and send
                            send_file(res, f, size);

                        break;

                    default:
                        f.close();
                        throw manapi::toolbox::manapi_exception ("multi bytes unsupported");

                }
            }
            else {
                res.set_header(http_header.CONTENT_LENGTH, std::to_string(fileSize));

                if (mask_response (res) != -1)
                    send_file(res, f, fileSize);
            }
            f.close();

            return;
        }
    }
    else {
        if (compressor != nullptr) {
            // encode content !
            compressed      = compressor (*body, nullptr);

            body            = &compressed;
        }

        res.set_header(http_header.CONTENT_LENGTH, std::to_string(body->size()));
        res.set_header(http_header.CONTENT_TYPE, "text/html; charset=UTF-8");

        if (mask_response (res) != -1)
            mask_write(body->data(), body->size());

        return;
    }

    //mask_write(response.data(), response.size());
}

void manapi::net::http_task::send_file (manapi::net::http_response &res, std::ifstream &f, ssize_t size) const {
    std::string block;
    ssize_t     block_size;

    block.resize(BUFFER_SIZE);

    while (size != 0) {
        if (size > BUFFER_SIZE)
            block_size = BUFFER_SIZE;
        else
            block_size = size;

        f.read(block.data(), block_size);

        if (mask_write(block.data(), block_size) == -1)
            // cannot to send
            break;

        size -= block_size;
    }
}

ssize_t manapi::net::http_task::read_next() {
    struct timeval timeout{};

    timeout.tv_sec  = http_server->get_keep_alive(); // keep alive in n sec
    timeout.tv_usec = 0;

    int ready = select (conn_fd + 1, &read_fds, nullptr, nullptr, &timeout);

    if (ready < 0)
        perror("select");

    else if (ready == 0) {
        MANAPI_LOG("The waiting time of %llu seconds has been exceeded", http_server->get_keep_alive());
        return -1;
    }

    return mask_read(reinterpret_cast<const char *>(buff), http_server->get_socket_block_size());
}

void manapi::net::http_task::send_error_response(const size_t &status, const std::string &message, const handlers_types_t &errors) {
    if (!errors.contains(request_data.method))
        return;

    http_handler_page error;
    error.handler = errors.at (request_data.method);

    handle_request(&error, status, message);
}

// MASKS

ssize_t manapi::net::http_task::socket_read(const char *buff, const size_t &buff_size) const {
    return read (conn_fd, (void*) buff, buff_size);
}

ssize_t manapi::net::http_task::socket_write(const char *buff, const size_t &buff_size) const {
    return send (conn_fd, (void*) buff, buff_size, MSG_NOSIGNAL);
}

ssize_t manapi::net::http_task::openssl_read(const char *buff, const size_t &buff_size) const {
    return SSL_read (ssl, (void *) buff, buff_size);
}

ssize_t manapi::net::http_task::openssl_write(const char *buff, const size_t &buff_size) const {
    return SSL_write (ssl, (void *) buff, buff_size);
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

void manapi::net::http_task::tcp_parse_request_response(uint8_t *response, const size_t &size, size_t &i) {
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
        throw manapi::toolbox::manapi_exception ("request header is too large.");

    MANAPI_TASK_HTTP_TCP_NEXT_BLOCK_IF_NEEDED(i);

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

void manapi::net::http_task::mint_token(const uint8_t *dcid, size_t dcid_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *token, size_t *token_len) {
    memcpy(token, "quiche", sizeof("quiche") - 1);
    memcpy(token + sizeof("quiche") - 1, addr, addr_len);
    memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

    *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

bool manapi::net::http_task::validate_token(const uint8_t *token, size_t token_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *odcid, size_t *odcid_len) {
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

    if (*odcid_len < token_len) {
        return false;
    }

    memcpy(odcid, token, token_len);
    *odcid_len = token_len;

    return true;
}

uint8_t *manapi::net::http_task::gen_cid(uint8_t *cid, size_t cid_len) {
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        perror("failed to open /dev/urandom");
        return nullptr;
    }

    ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        perror("failed to create connection ID");
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
            fprintf(stderr, "failed to create packet: %zd\n", written);
            return;
        }

        ssize_t sent = sendto(conn_io->sock_fd, out, written, 0, (struct sockaddr *) &conn_io->peer_addr, conn_io->peer_addr_len);
        if (sent != written) {
            perror("failed to send");
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

manapi::net::http_qc_conn_io * manapi::net::http_task::quic_create_connection(uint8_t *scid, size_t scid_len, uint8_t *odcid, size_t odcid_len) {
    auto conn_io = new http_qc_conn_io ();

    if (scid_len != MANAPI_QUIC_CONNECTION_ID_LEN) {
        fprintf(stderr, "failed, scid length too short\n");
        return nullptr;
    }

    memcpy(conn_io->cid, scid, MANAPI_QUIC_CONNECTION_ID_LEN);

    quiche_conn *conn = quiche_accept(conn_io->cid, MANAPI_QUIC_CONNECTION_ID_LEN, odcid, odcid_len, http_server->server_addr, http_server->server_len,
                                      (struct sockaddr *) (&client), client_len, q_config);

    if (conn == nullptr) {
        fprintf(stderr, "failed to create connection\n");
        return nullptr;
    }

    conn_io->conn           = conn;
    conn_io->sock_fd        = conn_fd;

    conn_io->peer_addr      = client;
    conn_io->peer_addr_len  = client_len;

    conn_io->key            = std::string((char *) (conn_io->cid), MANAPI_QUIC_CONNECTION_ID_LEN);

    conn_io->mutex.lock();

    auto d                  = new std::pair(conn_io, q_map_conns);

    conn_io->timer.set<http_task::quic_timeout_cb> (d);
    conn_io->timer.start(1);

    q_map_conns->insert(conn_io->key, conn_io);

    conn_io->mutex.unlock();

    return conn_io;
}

void manapi::net::http_task::quic_timeout_cb(ev::timer &watcher, int revents) {
    auto p = static_cast<std::pair<http_qc_conn_io *, QUIC_MAP_CONNS_T *> *>(watcher.data);

    p->first->mutex.lock();

    quiche_conn_on_timeout(p->first->conn);

    printf("timeout\n");

    quic_flush_egress(p->first);

    if (quiche_conn_is_closed(p->first->conn)) {

        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(p->first->conn, &stats);
        quiche_conn_path_stats(p->first->conn, 0, &path_stats);

        fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu\n",
                stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

        quiche_conn_free(p->first->conn);

        p->second->erase(p->first->key);

        p->first->timer.stop();

        printf("DELETED %s\n", p->first->key.data());

        delete p->first;

        delete p;

        return;
    }

    p->first->mutex.unlock();
}

void manapi::net::http_task::set_quic_config(quiche_config *config) {
    q_config = config;
}

void manapi::net::http_task::set_quic_map_conns(QUIC_MAP_CONNS_T *new_map_conns) {
    q_map_conns = new_map_conns;
}

void manapi::net::http_task::parse_uri_path_dynamic(request_data_t &request_data, char *response, const size_t &size, size_t &i, const std::function<void()>& finished) {
    request_data.path       = {};
    request_data.divided    = 0;

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
                char x = (char)(manapi::toolbox::hex2dec(hex_symbols[0]) << 4 | manapi::toolbox::hex2dec(hex_symbols[1]));

                if (manapi::toolbox::valid_special_symbol(x)) {
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

        else if (request_data.divided == 0) {
            if (response[i] == '/') {
                request_data.path.emplace_back("");
                continue;
            }

            if (response[i] == '?') {
                request_data.divided = request_data.path.size();
                request_data.path.emplace_back("");
                continue;
            }
        }

        if (!request_data.path.empty())
            request_data.path.back() += response[i];
    }

    if (request_data.divided == 0)
        request_data.divided = request_data.path.size();

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


