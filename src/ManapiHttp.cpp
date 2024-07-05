#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>
#include "ManapiTaskFunction.h"
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiUtils.h"
#include "ManapiFilesystem.h"

#define MANAPI_HTTP_URI_PAGE_DEFAULT        0
#define MANAPI_HTTP_URI_PAGE_ERROR          1

std::string manapi::net::http::default_cache_dir      = "/tmp/";
std::string manapi::net::http::default_config_name    = "config.json";

manapi::net::http::http(std::string port): keep_alive(2), port(std::move(port)) {
    signal(SIGPIPE, SIG_IGN);


    setup ();
}

static void debug_log(const char *line, void *argp) {
    fprintf(stderr, "%s\n", line);
}


int manapi::net::http::pool(const size_t &thread_num) {
    if (tasks_pool == nullptr) {
        tasks_pool = new threadpool<task>(thread_num);
        tasks_pool->start();
    }

    if (http_version == 1 || http_version == 2) {

        const struct addrinfo hints = {
                .ai_family      = PF_UNSPEC,
                .ai_socktype    = SOCK_STREAM,
                .ai_protocol    = IPPROTO_TCP
        };


        struct addrinfo *local;
        if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
            perror("failed to resolve host");
            return -1;
        }

        server_addr = local->ai_addr;
        server_len  = local->ai_addrlen;

        MANAPI_LOG("HTTP TCP PORT USED: %s. http://%s:%s", port.data(), address.data(), port.data());

        tcp_io  = new ev::io (loop);
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);

        const int opt = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

        if (sock_fd < 0) {
            MANAPI_LOG("%s", "SOCKET ERROR");
            return 1;
        }

        fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK);

        if (bind(sock_fd, local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT %s IS ALREADY IN USE", port.data());
            return 1;
        }

        if (listen(sock_fd, 10) < 0) {
            MANAPI_LOG("LISTEN ERROR. sock_fd: %d", sock_fd);
            return 1;
        }

        if (ssl_config.enabled) {
            // setup ssl certs

            // init
            ctx = ssl_create_context();
            // setup ctx (load certs)
            ssl_configure_context();
        }

        tcp_io->set <http, &http::new_connection_tls> (this);
        tcp_io->start(sock_fd, ev::READ);

        loop.run(EVFLAG_AUTO);

        if (ssl_config.enabled)
            SSL_CTX_free(ctx);

        freeaddrinfo(local);
    }
    else {
        const struct addrinfo hints = {
                .ai_family      = PF_UNSPEC,
                .ai_socktype    = SOCK_DGRAM,
                .ai_protocol    = IPPROTO_UDP
        };

        struct addrinfo *local;
        if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
            perror("failed to resolve host");
            return -1;
        }

        server_addr = local->ai_addr;
        server_len  = local->ai_addrlen;

        MANAPI_LOG("HTTP UDP PORT USED: %s. http://%s:%s", port.data(), address.data(), port.data());

        // for HTTP/3
        udp_io  = new ev::io(loop);
        sock_fd = socket (local->ai_family, SOCK_DGRAM, 0);

        if (sock_fd < 0) {
            MANAPI_LOG("%s", "SOCKET ERROR");
            return 1;
        }

        fcntl(sock_fd, F_SETFL, fcntl(sock_fd, F_GETFL, 0) | O_NONBLOCK);

        if (bind(sock_fd, local->ai_addr, local->ai_addrlen) < 0) {
            MANAPI_LOG("PORT %s IS ALREADY IN USE", port.data());
            return 1;
        }


        //quiche_enable_debug_logging(debug_log, NULL);

        q_config = quiche_config_new(QUICHE_PROTOCOL_VERSION);

        // ssl
        quiche_config_load_cert_chain_from_pem_file (q_config, ssl_config.cert.data());
        quiche_config_load_priv_key_from_pem_file   (q_config, ssl_config.key.data());

        quiche_config_set_application_protos(q_config, (uint8_t *) QUICHE_H3_APPLICATION_PROTOCOL, sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1);

        quiche_config_set_max_idle_timeout                      (q_config, 10000);
        quiche_config_set_max_recv_udp_payload_size             (q_config, socket_block_size);
        quiche_config_set_max_send_udp_payload_size             (q_config, socket_block_size);
        quiche_config_set_initial_max_data                      (q_config, 10000000);
        quiche_config_set_initial_max_stream_data_bidi_local    (q_config, 1000000);
        quiche_config_set_initial_max_stream_data_bidi_remote   (q_config, 1000000);
        quiche_config_set_initial_max_stream_data_uni           (q_config, 1000000);
        quiche_config_set_initial_max_streams_bidi              (q_config, 100);
        quiche_config_set_initial_max_streams_uni               (q_config, 100);
        quiche_config_set_disable_active_migration              (q_config, true);
        quiche_config_set_cc_algorithm                          (q_config, QUICHE_CC_RENO);
        quiche_config_enable_early_data                         (q_config);

        http3_config = quiche_h3_config_new();

        if (http3_config == nullptr) {
            fprintf(stderr, "failed to create HTTP/3 config\n");
            return 1;
        }

        // create watcher
        udp_io->set <http, &http::new_connection_udp> (this);
        udp_io->start(sock_fd, ev::READ);
        //udp_io->loop.run(EVFLAG_AUTO);
        //udp_io->loop.run(0);
        loop.run(EVFLAG_AUTO);

        quiche_h3_config_free(http3_config);
        quiche_config_free(q_config);

        freeaddrinfo(local);
    }

    tasks_pool->stop();
    save();

    return 0;
}

std::future <int> manapi::net::http::run () {
    auto p = new std::promise <int> ();
    std::future <int> f = p->get_future();

    std::thread t ([p, this] () { p->set_value(pool()); delete p; });
    t.detach();

    return f;
}

/**
 * keep alive in seconds
 * @param seconds
 */
void manapi::net::http::set_keep_alive(const long int &seconds) {
    keep_alive = seconds;
}

const long int &manapi::net::http::get_keep_alive() const {
    return keep_alive;
}

void manapi::net::http::GET(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("GET", uri, handler);
}

void manapi::net::http::POST(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("POST", uri, handler, true);
}

void manapi::net::http::PUT(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("PUT", uri, handler);
}

void manapi::net::http::PATCH(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("PATCH", uri, handler);
}

void manapi::net::http::DELETE(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("DELETE", uri, handler);
}

manapi::net::http_handler_page manapi::net::http::get_handler(request_data_t &request_data) {
    http_handler_page handler_page;

    try
    {
        const http_uri_part *cur = &handlers;
        size_t maxi = std::min (request_data.path.size(), request_data.divided);
        for (size_t i = 0; i <= maxi; i++)
        {
            if (cur->errors != nullptr)
            {
                // TODO why all errors will be export instead of one by method ?
                // find errors handlers for methods!
                for (auto &handler: *cur->errors)
                {
                    handler_page.errors[handler.first] = handler.second;
                }
            }

            if (cur->statics != nullptr && cur->statics->contains(request_data.method))
            {
                handler_page.statics = &cur->statics->at(request_data.method);
                handler_page.statics_parts_len = i;
            }

            if (i == maxi)
                break;

            if (cur->map == nullptr || !cur->map->contains(request_data.path.at(i))) {
                if (cur->regexes == nullptr)
                    return handler_page;

                std::smatch match;
                bool find = false;

                for (const auto &regex: *cur->regexes) {
                    // regex.first  <- regex string
                    // regex.second <- pair <regex, value (maybe next or handler)>

                    if (std::regex_match(request_data.path.at(i), match, regex.second.first)) {
                        cur     = regex.second.second;
                        find    = true;

                        if (cur->params == nullptr) {
                            // bug

                            MANAPI_LOG("cur->regexes_title (params) is null. cur located at %x", cur);
                            return handler_page;
                        }

                        const size_t expected_size = match.size() - 1;
                        if (cur->params->size() != expected_size) {
                            // bug

                            MANAPI_LOG("The expected number of parameters (%zu) does not correspond of reality (%zu). uri part: %s.",
                                       cur->params->size(), expected_size, request_data.path.at(i).data());
                            return handler_page;
                        }

                        // get params
                        for (size_t z = 0; z < cur->params->size(); z++)
                            request_data.params.insert({cur->params->at(z), match.str(z + 1)});


                        break;
                    }
                }

                if (find)
                    continue;

                return handler_page;
            }

            cur = cur->map->at(request_data.path.at(i));
        }

        // handler page

        if (cur->handlers == nullptr) {
            // TODO: error

            return handler_page;
        }

        handler_template_t handler = cur->handlers->at (request_data.method);

        if (handler == nullptr) {
            // TODO: handler error
        }

        if (cur->has_body)
            request_data.has_body = true;

        handler_page.handler = handler;

        return handler_page;
    } catch (const std::exception &e) {
        return handler_page;
    }
}

manapi::net::http_uri_part *manapi::net::http::set_handler(const std::string &method, const std::string &uri, const handler_template_t &handler, bool has_body) {
    size_t  type            = MANAPI_HTTP_URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    switch (type) {
        case MANAPI_HTTP_URI_PAGE_DEFAULT:
            if (cur->handlers == nullptr)
                cur->handlers = new handlers_types_t ();

            cur->handlers->insert({method, handler});

            cur->has_body = has_body;

            break;
        case MANAPI_HTTP_URI_PAGE_ERROR:
            if (cur->errors == nullptr)
                cur->errors = new handlers_types_t ();
            cur->errors->insert({method, handler});
            break;
        default:
            break;
    }

    return cur;
}

void manapi::net::http::GET(const std::string &uri, const std::string &folder) {
    set_handler ("GET", uri, folder, false);
}

manapi::net::http_uri_part *manapi::net::http::set_handler(const std::string &method, const std::string &uri, const std::string &folder, bool has_body) {
    size_t  type            = MANAPI_HTTP_URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    switch (type) {
        case MANAPI_HTTP_URI_PAGE_DEFAULT:
            if (cur->statics == nullptr)
                cur->statics = new handlers_static_types_t ();

            cur->statics->insert({method, folder});

            cur->has_body = has_body;

            break;
        default:
            throw manapi::toolbox::manapi_exception ("can not use the special pages with the static files");
    }

    return cur;
}

manapi::net::http_uri_part *manapi::net::http::build_uri_part(const std::string &uri, size_t &type) {
    std::string                 buff;
    handlers_regex_titles_t     *regexes_title = nullptr;

    http_uri_part *cur      = &handlers;

    bool    is_regex        = false;

    // past params lists. To check for a match
    std::vector <std::vector <std::string> *> past_params_lists;

    for (size_t i = 0; i <= uri.size(); i++) {
        const bool is_last_part = (i == uri.size());

        if (uri[i] == '/' || is_last_part) {
            if (!buff.empty()) {

                if (is_regex) {
                    if (cur->regexes == nullptr || !cur->regexes->contains(buff)) {
                        if (cur->regexes == nullptr)
                            cur->regexes = new handlers_regex_map_t();

                        auto new_part   = new http_uri_part;

                        std::regex p(buff);
                        cur->regexes->insert({buff, std::make_pair(p, new_part)});

                        if (regexes_title != nullptr) {
                            new_part->params = regexes_title;

                            // check for a match
                            for (const auto &past_params: past_params_lists) {
                                for (const auto &param: *regexes_title) {
                                    if (std::find(past_params->begin(), past_params->end(), param) !=
                                        past_params->end())
                                        MANAPI_LOG("Warning: a param with a title '%s' is already in use. (%s)",
                                                   param.data(), uri.data());
                                }
                            }

                            // save params list
                            past_params_lists.push_back(regexes_title);
                        }
                        //clean up
                        regexes_title   = nullptr;

                        cur             = new_part;
                    }
                    else {
                        cur             = cur->regexes->at(buff).second;
                    }

                    is_regex        = false;
                }
                else {

                    if (buff[0] == '+' && is_last_part) {
                        if (buff == "+error") {
                            type = MANAPI_HTTP_URI_PAGE_ERROR;

                            break;
                        }

                        else
                            MANAPI_LOG("First char '%c' is reserved for special pages", '+');
                    }

                    if (cur->map == nullptr)
                        cur->map = new handlers_map_t();

                    if (!cur->map->contains(buff)) {
                        auto new_part       = new http_uri_part;

                        cur->map->insert({buff, new_part});

                        cur                 = new_part;
                    }
                    else
                        cur                 = cur->map->at(buff);
                }


                // clean up
                buff    = "";
            }

            continue;
        }
        else if (uri[i] == '[') {
            std::string     title;
            const size_t    temp = i;

            for (i++; i < uri.size(); i++) {
                if (uri[i] == ']')
                    break;

                else if (manapi::toolbox::escape_char_need(uri[i])) {
                    title = "";
                    break;
                }

                title += uri[i];
            }

            if (!title.empty()) {
                if (!is_regex) {
                    is_regex = true;

                    buff = manapi::toolbox::escape_string(buff);
                }

                // if null -> create
                if (regexes_title == nullptr)
                    regexes_title = new handlers_regex_titles_t ();

                regexes_title->push_back(title);

                buff += "(.+)";
                continue;
            }

            i = temp;
        }

        if (is_regex) {
            if (manapi::toolbox::escape_char_need(uri[i]))
                buff.push_back('\\');

            buff += uri[i];
            continue;
        }

        buff += uri[i];
    }

    return cur;
}


// ======================[ configs funcs]==========================

void manapi::net::http::set_socket_block_size(const size_t &s) {
    socket_block_size = s;
}

const size_t &manapi::net::http::get_socket_block_size() const {
    return socket_block_size;
}

void manapi::net::http::set_max_header_block_size(const size_t &s) {
    max_header_block_size = s;
}

const size_t &manapi::net::http::get_max_header_block_size() const {
    return max_header_block_size;
}

const size_t &manapi::net::http::get_partial_data_min_size() const {
    return partial_data_min_size;
}

void manapi::net::http::set_http_version(const size_t &new_http_version) {
    http_version = new_http_version;
}

const size_t &manapi::net::http::get_http_version() const {
    return http_version;
}

void manapi::net::http::set_http_version_str(const std::string &new_http_version) {
    http_version_str = new_http_version;
}

const std::string &manapi::net::http::get_http_version_str() const {
    return http_version_str;
}

void manapi::net::http::set_compressor(const std::string &name, manapi::toolbox::compress::TEMPLATE_INTERFACE handler) {
    compressors[name] = handler;
}

manapi::toolbox::compress::TEMPLATE_INTERFACE manapi::net::http::get_compressor(const std::string &name) {
    if (!contains_compressor(name))
        return nullptr;

    return compressors.at(name);
}

bool manapi::net::http::contains_compressor(const std::string &name) {
    return compressors.contains(name);
}

void manapi::net::http::setup() {
    config                      = manapi::toolbox::json::object();
    cache_config                = manapi::toolbox::json::object();

    set_compressor("deflate", manapi::toolbox::compress::deflate);
    set_compressor("gzip", manapi::toolbox::compress::gzip);
}

void manapi::net::http::set_config(const std::string &path) {
    config_path = path;

    if (!manapi::toolbox::filesystem::exists(config_path)) {
        manapi::toolbox::filesystem::config::write(config_path, config);
        return;
    }

    config = manapi::toolbox::filesystem::config::read (config_path);

    setup_config ();
}

const manapi::toolbox::json &manapi::net::http::get_config() {
    return config;
}

void manapi::net::http::setup_config() {
    // =================[cache config]================= //
    if (config.contains("cache_dir"))
        config_cache_dir = config["cache_dir"].get_ptr<std::string>();
    else
        config_cache_dir = &default_cache_dir;

    manapi::toolbox::filesystem::append_delimiter(*config_cache_dir);

    if (!manapi::toolbox::filesystem::exists(*config_cache_dir))
        manapi::toolbox::filesystem::mkdir(*config_cache_dir);

    else {
        std::string path = *config_cache_dir + default_config_name;
        if (manapi::toolbox::filesystem::exists(path))
            cache_config = manapi::toolbox::filesystem::config::read(path);
    }

    // =================[partial data min size  ]================= //
    if (config.contains("partial_data_min_size"))
        partial_data_min_size = config["partial_data_min_size"].get <size_t> ();

    // =================[socket block size      ]================= //
    if (config.contains("socket_block_size"))
        socket_block_size = config["socket_block_size"].get <size_t> ();

    // =================[http version           ]================= //
    if (config.contains("http_version")) {
        http_version_str = config["http_version"].get <std::string> ();

        if (http_version_str       == "1.1")   http_version = 1;
        else if (http_version_str  == "2")     http_version = 2;
        else if (http_version_str  == "3")     http_version = 3;
        else {
            http_version_str    = "1.1";
            http_version        = 1;

            MANAPI_LOG("http version '%s' is invalid in the config", http_version_str.data());
        }
    }

    // =================[port                   ]================= //
    if (config.contains("port"))
        port = config["port"].get <std::string> ();

    // =================[address                ]================= //
    if (config.contains("address"))
        address = config["address"].get <std::string> ();

    // =================[ssl                    ]================= //
    if (config.contains("ssl")) {
        ssl_config.enabled  = config["ssl"]["enabled"].get<bool>();
        ssl_config.key      = config["ssl"]["key"].get<std::string>();
        ssl_config.cert     = config["ssl"]["cert"].get<std::string>();
    }

    // =================[save config            ]================= //
    if (config.contains("save_config")) {
        if (config["save_config"].is_bool())
        {
            enabled_save_config = config["save_config"].get<bool>();
        }
    }
}

const std::string *manapi::net::http::get_compressed_cache_file(const std::string &file, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
        return nullptr;


    auto files = &cache_config[algorithm];

    if (!files->contains(file))
        return nullptr;

    auto file_info = &files->at(file);

    if (*file_info->at("last-write").get_ptr<std::string>() == manapi::toolbox::filesystem::last_time_write(file, true)) {
        auto compressed = file_info->at("compressed").get_ptr<std::string>();

        if (manapi::toolbox::filesystem::exists(*compressed))
            return compressed;
    }

    files->erase(file);

    return nullptr;
}

void manapi::net::http::set_compressed_cache_file(const std::string &file, const std::string &compressed, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
        cache_config.insert(algorithm, manapi::toolbox::json::object());

    manapi::toolbox::json file_info = manapi::toolbox::json::object();

    file_info.insert("last-write", manapi::toolbox::filesystem::last_time_write(file, true));
    file_info.insert("compressed", compressed);

    cache_config[algorithm].insert(file, file_info);
}

void manapi::net::http::save() {
    // close connections
    close (sock_fd);

    if (enabled_save_config)
    {
        // save configs
        save_config();
    }
}

void manapi::net::http::save_config() {
    // main config
    manapi::toolbox::filesystem::config::write(config_path, config);

    // cache config
    manapi::toolbox::filesystem::config::write(*config_cache_dir + default_config_name, cache_config);
}

void manapi::net::http::stop() {
    if (udp_io != nullptr) {
        udp_io->stop();
        delete udp_io;

        udp_io = nullptr;
    }

    if (tcp_io != nullptr) {
        tcp_io->stop();
        delete tcp_io;

        tcp_io = nullptr;
    }

    shutdown(sock_fd, SHUT_RDWR);
}

void manapi::net::http::new_connection_udp(ev::io &watcher, int revents) {
    char    buff[MANAPI_MAX_DATAGRAM_SIZE];
    ssize_t buff_size;

    struct sockaddr_storage client{};
    socklen_t client_len = sizeof(client);
    memset(&client, 0, client_len);

    buff_size = recvfrom(sock_fd, buff, sizeof(buff), 0, (struct sockaddr *) &client, &client_len);
    if (buff_size <= 0)
        return;

    auto *ta = new http_task(sock_fd, buff, buff_size, client, client_len, udp_io, this);

    ta->set_quic_config(q_config);
    ta->set_quic_map_conns(&quic_map_conns);

    tasks_pool->append_task(ta);

    auto check = new function_task ([this] () {
        quic_map_conns.block();

        for (auto it = quic_map_conns.begin(); it != quic_map_conns.end();) {
            std::unique_lock <std::mutex> lock (it->second->mutex, std::try_to_lock);

            if (lock.owns_lock()) {
                http_task::quic_flush_egress(it->second);

                if (quiche_conn_is_closed(it->second->conn) || !it->second->timer.is_active()) {
                    quiche_stats stats;
                    quiche_path_stats path_stats;

                    quiche_conn_stats(it->second->conn, &stats);
                    quiche_conn_path_stats(it->second->conn, 0, &path_stats);

                    fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu\n",
                            stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);

                    printf("DELETED %s\n", it->first.data());

                    // multi thread
                    auto conn_io = it->second;

                    it = quic_map_conns.erase(it);

                    http_task::quic_delete_conn_io(conn_io);

                    continue;
                }
            }

            it++;
        }

        quic_map_conns.unblock();
    });

    tasks_pool->append_task(check);
}

void manapi::net::http::new_connection_tls(ev::io &watcher, int revents) {
    struct sockaddr_storage client{};
    socklen_t len = sizeof(client);
    conn_fd = accept(sock_fd, reinterpret_cast<struct sockaddr *>(&client), &len);

    if (conn_fd < 0)
        return;

    auto *ta = new http_task(conn_fd, client, len, this);

    if (ssl_config.enabled)
        ta->ssl = SSL_new(ctx);

    tasks_pool->append_task(ta);
}

void manapi::net::http::set_port(const std::string &_port) {
    port = _port;
}

const manapi::net::ssl_config_t &manapi::net::http::get_ssl_config() {
    return ssl_config;
}

SSL_CTX *manapi::net::http::ssl_create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLS_server_method();
    ctx = SSL_CTX_new(method);

    if (!ctx)
        throw manapi::toolbox::manapi_exception ("cannot create openssl context for tcp connections");

    return ctx;
}

void manapi::net::http::ssl_configure_context() {
    if (SSL_CTX_use_certificate_file(ctx, ssl_config.cert.data(), SSL_FILETYPE_PEM) <= 0)
        throw manapi::toolbox::manapi_exception ("cannot use cert file openssl");

    if (SSL_CTX_use_PrivateKey_file(ctx, ssl_config.key.data(), SSL_FILETYPE_PEM) <= 0)
        throw manapi::toolbox::manapi_exception ("cannot use private key file openssl");
}
