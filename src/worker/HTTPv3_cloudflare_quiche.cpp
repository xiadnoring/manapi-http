// #include <http/HTTPv2.hpp>
//
// #include "crypto/ManapiAEAD.hpp"
// #include "worker/HTTPv3_clouflare_quiche.hpp"
// #include "ManapiVersions.hpp"
// #include "ManapiParams.hpp"
//
// #if MANAPIHTTP_QUICHE_DEPENDENCY
//
// # define MANAPI_MAX_DATAGRAM_SIZE 1350
// # define MANAPI_QUICHE_CONNECTION_ID_LEN 16
//
// constexpr static size_t _quiche_token_max_len = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;
//
// template<typename T>
// requires(version_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
// bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
//     return quiche_h3_event_headers_has_more_frames(static_cast<T>(event));
// }
//
// template<typename T>
// requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
// bool manapi_quiche_h3_event_headers_has_more_frames_ (T event) {
//     return quiche_h3_event_headers_has_body(static_cast<T>(event));
// }
//
// template<typename ...Args>
// requires(version_greater_or_equal(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
// ssize_t manapi_quiche_h3_send_additional_headers_(Args...args) {
//     return quiche_h3_send_additional_headers(args...);
// }
//
// template<typename ...Args>
// requires(version_less(MANAPIHTTP_QUICHE_VERSION, "0.23.0"))
// ssize_t manapi_quiche_h3_send_additional_headers_(Args...args) { /* skip */ return 0; }
//
// manapi::net::worker::http_v3_cloudflare_quiche::http_v3_cloudflare_quiche(net::site &site) : udp(site) {
//     this->recv_buffer_freed = 0;
//     try {
//         this->recv_buffer = memory::alloc<char>(MANAPI_MAX_DATAGRAM_SIZE);
//     }
//     catch (...) {
//         site.async_context()->logger()->error(
//             manapi::logger::default_service, ERR_FATAL, "udp buffer alloc failed");
//     }
// }
//
// manapi::net::worker::http_v3_cloudflare_quiche::~http_v3_cloudflare_quiche() {
//     quiche_config_free(this->_quiche_config);
//     quiche_h3_config_free(this->_quiche_h3_config);
//     memory::free(this->recv_buffer);
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) {
//     socklen_t sockaddr_len = 0;
//     auto sockaddr_src = (sockaddr *)addr;
//     if (addr->sa_family == ev::IPv4) {
//         sockaddr_len = sizeof (sockaddr_in);
//     }
//     else if (addr->sa_family == ev::IPv6) {
//         sockaddr_len = sizeof (sockaddr_in6);
//     }
//
//     uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];
//
//     connection_t *conn_data{nullptr};
//
//     uint8_t type;
//     uint32_t version;
//
//     std::string scid;
//     scid.resize(QUICHE_MAX_CONN_ID_LEN);
//     uint64_t scid_len = scid.size();
//
//     std::string dcid;
//     dcid.resize(QUICHE_MAX_CONN_ID_LEN);
//     uint64_t dcid_len = dcid.size();
//
//     std::string odcid;
//
//     std::string token;
//     token.resize(_quiche_token_max_len);
//     uint64_t token_len = token.size();
//
//     if(0 != quiche_header_info (reinterpret_cast<const uint8_t *> (buff), size, MANAPI_QUICHE_CONNECTION_ID_LEN,
//         &version, &type, reinterpret_cast<uint8_t *> (scid.data()), &scid_len, reinterpret_cast<uint8_t *> (dcid.data()), &dcid_len,
//         reinterpret_cast<uint8_t *> (token.data()), &token_len)) {
//
//         /* broken packet */
//         return;
//         }
//
//     scid.resize(scid_len);
//     dcid.resize(dcid_len);
//     token.resize(token_len);
//
//     auto it = this->connections.find(dcid);
//     if (it == this->connections.end()) {
//         if (this->connections.size() >= this->config->max_connections()) {
//             return;
//         }
//
//         if (!quiche_version_is_supported(version)) {
//             const int64_t written = quiche_negotiate_version(reinterpret_cast<uint8_t *> (scid.data()),
//                 scid.size(), reinterpret_cast<uint8_t *> (dcid.data()), dcid.size(), out, sizeof (out));
//
//             if (written < 0) {
//                 return;
//             }
//
//             ev::buff_t buff2[1] = {{.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)}};
//             ssize_t rhs = this->udp_accept_->try_send(buff2, 1, reinterpret_cast<sockaddr *>(sockaddr_src));
//
//             if (rhs != written) {
//                 /* failed to send */
//             }
//
//             return;
//         }
//
//         if (token.empty()) {
//             token = http_v3_cloudflare_quiche::_gen_mint_token(dcid, sockaddr_src, sockaddr_len);
//             std::string new_scid = crypto::random_string(MANAPI_QUICHE_CONNECTION_ID_LEN);
//             const int64_t written = quiche_retry(reinterpret_cast<uint8_t *> (scid.data()), scid.size(), reinterpret_cast<uint8_t *>(dcid.data()), dcid.size(),
//                 reinterpret_cast<uint8_t *>(new_scid.data()), new_scid.size(), reinterpret_cast<uint8_t *>(token.data()), token.size(), version, out, sizeof (out));
//
//             if (written < 0) {
//                 return;
//             }
//
//             ev::buff_t buff2[1] = {{.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)}};
//             ssize_t rhs = this->udp_accept_->try_send(buff2, 1, reinterpret_cast<sockaddr *>(sockaddr_src));
//
//             if (rhs != written) {
//                 /* failed to send */
//             }
//
//             return;
//         }
//
//         if (!http_v3_cloudflare_quiche::_validate_token(token, odcid, sockaddr_src, sockaddr_len)) {
//             return;
//         }
//
//         auto quiche_conn_ = quiche_accept(reinterpret_cast<uint8_t *> (dcid.data()), dcid.size(),
//             reinterpret_cast<uint8_t *>(odcid.data()), odcid.size(), &*this->config->server_address(), this->config->server_len(),
//             reinterpret_cast<sockaddr *>(sockaddr_src), sockaddr_len, this->_quiche_config);
//
//         if (!quiche_conn_) {
//             return;
//         }
//
//         auto connection = std::make_unique<worker::connection>(new connection_t {
//             .cid = dcid, .conn = quiche_conn_, .http3_conn = nullptr, .quiche_timer = {}, .streams = {},
//             .worker = std::shared_ptr (this->self_), .transfared_last_second = 0, .status = 0}, http_v3_cloudflare_quiche::clean_connection_
//         );
//
//         conn_data = connection->as<connection_t>();
//
//         conn_data->quiche_timer = this->site().async_context()->eventloop()->create_watcher_timer ([connection = connection.get()] (std::shared_ptr<ev::timer> &w)
//             -> void { http_v3_cloudflare_quiche::quiche_timeout_(w, connection); });
//
//         conn_data->quiche_timer->start(0, 1);
//
//         this->connections.insert({
//             dcid,
//             std::move(connection)
//         });
//     }
//     else {
//         conn_data = it->second->as<connection_t>();
//     }
//
//     quiche_recv_info recv_info {
//         reinterpret_cast <sockaddr *>(sockaddr_src),
//         sockaddr_len,
//         (sockaddr *)&*this->config->server_address(),
//         this->config->server_len()
//     };
//
//     ssize_t done = quiche_conn_recv(conn_data->conn, reinterpret_cast<uint8_t *> (buff), size, &recv_info);
//
//     if (done < 0) {
//         return;
//     }
//
//     if ((quiche_conn_is_in_early_data(conn_data->conn) || quiche_conn_is_established(conn_data->conn)) && !conn_data->http3_conn) {
//         conn_data->http3_conn = quiche_h3_conn_new_with_transport(conn_data->conn, this->_quiche_h3_config);
//
//         if (!conn_data->http3_conn) {
//             MANAPIHTTP_LOG2(this->site().async_context(), "QUICHE: assert(!conn_data->http3_conn) failed");
//             return;
//         }
//     }
//
//     if (conn_data->http3_conn && !(conn_data->status & CONN_CLOSED)) {
//         http_v3_cloudflare_quiche::flush_write_(conn_data, false);
//
//         quiche_h3_event *event{nullptr};
//
//         while (true) {
//             int64_t stream_id = quiche_h3_conn_poll(conn_data->http3_conn, conn_data->conn, &event);
//
//             if (stream_id < 0) {
//                 break;
//             }
//
//             switch (quiche_h3_event_type(event)) {
//                 case QUICHE_H3_EVENT_HEADERS: {
//                     auto stream_it = conn_data->streams.find(stream_id);
//
//                     quiche_h3_event_for_each_header(event, http_v3_cloudflare_quiche::_grab_headers, client.get());
//
//                     client->request_data.body_index = 0;
//                     client->request_data.body_index = 0;
//                     client->request_data.uri = client->request_data.headers[":path"];
//                     client->request_data.headers_size = 0;
//                     client->request_data.divided = -1;
//                     client->request_data.http = http::versions::HTTP_v3;
//
//                     client->request_data.has_body = manapi_quiche_h3_event_headers_has_more_frames_(event);
//
//                     if (client->request_data.has_body) {
//                         auto contentlength = client->request_data.headers.find(http::HEADER.CONTENT_LENGTH);
//                         client->request_data.headers_part = 0;
//                         client->request_data.body_part = 0;
//                         client->request_data.body_size = contentlength != client->request_data.headers.end()
//                             ? std::stoll(contentlength->second) : -1 /* The size isn't fixed */;
//                     }
//                     else {
//                         client->request_data.body_size = 0;
//                         client->request_data.body_part = 0;
//                     }
//                     client->request_data.body_left = client->request_data.body_size;
//                     client->request_data.method = client->request_data.headers[":method"];
//
//                     async::run(connection->as<connection_t>().worker->site.async_context(), [stream_id, connection, client = std::move(client)] () mutable -> future<> {
//                         auto &ctx = connection->as<connection_t>().worker->site.async_context();
//
//                         try {
//                             if (co_await client->parse_request(0, 0)) {
//                                 co_await client->execute_handler();
//                             }
//                         }
//                         catch (std::exception const &e) {
//                             /* error */
//                             MANAPIHTTP_LOG(ctx, "Unexpected exception: {}", e.what());
//                         }
//
//                         co_await ctx->eventloop()->custom_callback(
//                             [connection, stream_id] (manapi::event_loop *ev) -> void {
//                                 auto &conn = connection->as<connection_t>();
//                                 auto &parent = connection->as<connection_t>();
//                                 parent.flags |= CONN_REVIEW_STREAMS;
//                                 parent.closed_streams.push_back(stream_id);
//                                 parent.write_watcher->send();
//                         });
//
//                         // auto &conn_data = client->connection->as<connection_t>();
//                         // conn_data->write_watcher.send()
//                         // conn_data->status.fetch_or(CONN_HALF_CLOSED);
//                     });
//
//                     break;
//                 }
//                 case QUICHE_H3_EVENT_DATA: {
//                     auto stream_it = conn_data->streams.find(stream_id);
//                     if (stream_it == conn_data->streams.end()) {
//                         break;
//                     }
//
//                     auto &stream_connection = stream_it->second;
//                     auto stream = stream_connection->as<connection_stream_t>();
//
//                     flush_read_stream_(stream->connection->as<connection_t>(), stream_it);
//
//                     // skip
//
//                     break;
//                 }
//                 case QUICHE_H3_EVENT_FINISHED: {
//                     auto stream_it = conn_data->streams.find(stream_id);
//                     if (stream_it == conn_data->streams.end()) {
//                         break;
//                     }
//
//                     auto &stream_connection = stream_it->second;
//                     auto stream = stream_connection->as<connection_stream_t>();
//
//                     stream->status2 |= STREAM_CONN_RECV_END;
//
//                     flush_read_stream_(stream.connection->as<connection_t>(), stream_it);
//                     break;
//                 }
//                 case QUICHE_H3_EVENT_RESET: {
//                     auto stream_it = conn_data->streams.find(stream_id);
//                     if (stream_it == conn_data->streams.end()) {
//                         break;
//                     }
//                     auto stream = stream_it->second->as<connection_stream_t>();
//
//                     this->_stream_close(stream);
//
//                     break;
//                 }
//                 case QUICHE_H3_EVENT_PRIORITY_UPDATE: {
//                     break;
//                 }
//                 case QUICHE_H3_EVENT_GOAWAY: {
//                     http_v3_cloudflare_quiche::_reset_all_streams(conn_data);
//
//                     break;
//                 }
//             }
//
//             quiche_h3_event_free(event);
//         }
//     }
//
//     /* setup timeout */
//     auto connection_it = this->connections.find(dcid);
//     if (connection_it != this->connections.end()) {
//         this->_quiche_flush_egress(conn_data);
//         http_v3_cloudflare_quiche::_quiche_timeout_again(conn_data);
//         http_v3_cloudflare_quiche::flush_connection_closed_(conn_data);
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::init() {
//     udp::init();
//     do {
//         if (auto rhs = this->udp_accept_->recv_start()) {
//             this->site().async_context()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't start recv due to result - {}", rhs);
//             goto err;
//         }
//
//         this->_quiche_config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
//         this->_quiche_h3_config = quiche_h3_config_new();
//
//         auto ssl_config = this->config->ssl_config();
//
//         if (!ssl_config->enabled) {
//             THROW_MANAPIHTTP_EXCEPTION2(ERR_CONFIG_ERROR, "QUICHE: QUIC requires SSL be enabled");
//         }
//
//         if (0 != quiche_config_load_cert_chain_from_pem_file(this->_quiche_config, ssl_config->cert.data())) {
//             THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load cert chain from pem file: {}", ssl_config->cert);
//         }
//
//         if (0 != quiche_config_load_priv_key_from_pem_file(this->_quiche_config, ssl_config->key.data())) {
//             THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "QUICHE: failed to load priv key from pem file: {}", ssl_config->key);
//         }
//
//         if(quiche_config_set_application_protos(this->_quiche_config,
//             reinterpret_cast <const uint8_t *> (QUICHE_H3_APPLICATION_PROTOCOL), sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1)) {
//             goto err;
//         }
//         quiche_config_set_max_idle_timeout(this->_quiche_config, 5000);
//         quiche_config_set_max_recv_udp_payload_size(this->_quiche_config, MANAPI_MAX_DATAGRAM_SIZE);
//         quiche_config_set_max_send_udp_payload_size(this->_quiche_config, MANAPI_MAX_DATAGRAM_SIZE);
//         quiche_config_set_initial_max_data(this->_quiche_config, 10000000);
//         quiche_config_set_initial_max_stream_data_bidi_local(this->_quiche_config, 1000000);
//         quiche_config_set_initial_max_stream_data_bidi_remote(this->_quiche_config, 1000000);
//         quiche_config_set_initial_max_stream_data_uni(this->_quiche_config, 1000000);
//         quiche_config_set_initial_max_streams_bidi (this->_quiche_config, 100);
//         quiche_config_set_initial_max_streams_uni (this->_quiche_config, 100);
//         //quiche_config_set_disable_active_migration (this->_quiche_config, true);
//         quiche_config_verify_peer(this->_quiche_config, this->config->verify_peer());
//         if (this->config->is_quic_debug()) {
//             if (quiche_enable_debug_logging([] (const char *line, void *argp)
//                 -> void {
//                 MANAPIHTTP_LOG2((static_cast<http_v3_cloudflare_quiche*>(argp))->site.async_context(), line);
//             }, this)) {
//                 goto err;
//             }
//         }
//
//         if (this->config->quic_cc_algo().load() != http::versions::QUIC_CC_NONE) {
//             quiche_cc_algorithm algo = QUICHE_CC_RENO;
//
//             switch (this->config->quic_cc_algo().load())
//             {
//                 case http::versions::QUIC_CC_CUBIC:   algo = QUICHE_CC_CUBIC;     break;
//                 case http::versions::QUIC_CC_RENO:    algo = QUICHE_CC_RENO;      break;
//                 case http::versions::QUIC_CC_BBR:     algo = QUICHE_CC_BBR;       break;
//                 case http::versions::QUIC_CC_BBR2:    algo = QUICHE_CC_BBR2;      break;
//                 default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "invalid quic_cc_algo: {}",
//                         static_cast<int>(this->config->quic_cc_algo().load()));
//             }
//
//             quiche_config_set_cc_algorithm (this->_quiche_config, algo);
//         }
//
//         /* every 1 second */
//         this->limit_rate_timer = this->site().async_context()->timerpool()->append_interval_sync(1000,
//             [this] (manapi::timer t) -> void { this->update_limit_rate(); });
//     }
//     while (0);
//     return;
// err:
//     THROW_MANAPIHTTP_EXCEPTION2(ERR_SOCKET, "quiche: init(...) failed");
// }
//
// std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche> manapi::net::worker::http_v3_cloudflare_quiche::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
//     auto worker = std::make_shared<worker::http_v3_cloudflare_quiche>(site);
//     worker->config(std::move(config));
//     worker->new_dependency = [worker = std::weak_ptr<worker::http_v3_cloudflare_quiche> (worker)] () -> std::shared_ptr<worker::http_v3_cloudflare_quiche> {
//           return std::shared_ptr<worker::http_v3_cloudflare_quiche> (worker);
//     };
//     worker->self_ = std::weak_ptr (worker);
//     return std::move(worker);
// }
//
// manapi::future<ssize_t> manapi::net::worker::http_v3_cloudflare_quiche::response(worker::connection *connection, http::response *resp, bool finish) {
//     auto stream_data = connection->as<connection_stream_t>();
//     auto conn_data = stream_data->connection->as<connection_t>();
//
//     stream_data->headers = std::make_unique<std::map<std::string, std::string>>(std::move(resp->headers()));
//     stream_data->quiche_headers.reset(new quiche_h3_header[stream_data->headers->size() + 1]);
//     stream_data->headers_size = static_cast<int>(stream_data->headers->size() + 1);
//     size_t i = 1;
//
//     const size_t max_header_value_len = 200;
//     for (auto header = stream_data->headers->begin(); header != stream_data->headers->end(); ++header) {
//         size_t header_value_cursor = 0;
//         while (true) {
//             if (header->first.size() > max_header_value_len) {
//                 THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "header key is too long. Size: {}", header->first.size());
//             }
//             auto len = std::min(max_header_value_len - header->first.size(), header->second.size() - header_value_cursor);
//             http_v3_cloudflare_quiche::quiche_set_header_(stream_data->quiche_headers.get()[i++], header->first, std::string_view{header->second.data() + header_value_cursor, len});
//             header_value_cursor += len;
//             if (header_value_cursor == header->second.size()) {
//                 break;
//             }
//             ++stream_data->headers_size;
//             auto nheaders = static_cast<quiche_h3_header *>(realloc(stream_data->quiche_headers.release(), sizeof (quiche_h3_header) * stream_data->headers_size));
//             if (nheaders) {
//                 stream_data->quiche_headers.reset(nheaders);
//             }
//             else {
//                 THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "failed to realloc(...) headers buffer");
//             }
//         }
//     }
//
//     i = 0;
//
//     auto &status_header = stream_data->headers->operator[](":status");
//     status_header = std::to_string(resp->status_code());
//     http_v3_cloudflare_quiche::quiche_set_header_(stream_data.quiche_headers.get()[i++], ":status", status_header);
//
//
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::stop() {
//     this->limit_rate_timer.sync_stop(this->site().async_context());
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) {
//     if (this->recv_buffer_freed) {
//         buff->base = this->recv_buffer;
//         buff->len = MANAPI_MAX_DATAGRAM_SIZE;
//         this->recv_buffer_freed = false;
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::recv_buffer_dealloc_(const ev::buff_t *buf) {
//     this->recv_buffer_freed = 0;
// }
//
// ssize_t manapi::net::worker::http_v3_cloudflare_quiche::sync_write(worker::connection *conn, const void *buff, ssize_t size, bool finish) {
//     return -1;
// }
//
// manapi::net::worker::http_v3_cloudflare_quiche * manapi::net::worker::http_v3_cloudflare_quiche::get_dynamic_worker_(
//     const std::shared_ptr<worker::base> &w) {
//     return dynamic_cast<http_v3_cloudflare_quiche *> (w.get());
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::flush_write_stream_(connection_t *conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it, bool app) {
//
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::flush_read_stream_(connection_t *conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it) {
//
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::flush_write_(connection_t *conn_data, bool app) {
//     http_v3_cloudflare_quiche::get_dynamic_worker_(conn_data->worker)->_quiche_flush_egress(conn_data);
//     /* send data */
//     int64_t stream_id;
//     quiche_stream_iter *stream = quiche_conn_writable(conn_data->conn);
//     while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
//         auto stream_it = conn_data->streams.find(stream_id);
//         //MANAPIHTTP_LOG("WRITE STREAM:{}", stream_id);
//         if (stream_it != conn_data->streams.end()) {
//             http_v3_cloudflare_quiche::flush_write_stream_(conn_data, stream_it, app);
//         }
//     }
//     quiche_stream_iter_free(stream);
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::flush_read_(connection_t *conn_data) {
//     http_v3_cloudflare_quiche::get_dynamic_worker_(conn_data->worker)->_quiche_flush_egress(conn_data);
//
//     /* recv data */
//     int64_t stream_id;
//     quiche_stream_iter *stream = quiche_conn_readable(conn_data->conn);
//     while (quiche_stream_iter_next(stream, reinterpret_cast<uint64_t *>(&stream_id))) {
//         auto stream_it = conn_data->streams.find(stream_id);
//         //MANAPIHTTP_LOG("READ STREAM:{}", stream_id);
//         if (stream_it != conn_data->streams.end()) {
//             flush_read_stream_(conn_data, stream_it);
//         }
//     }
//     quiche_stream_iter_free(stream);
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::quiche_set_header_(quiche_h3_header &header, std::string_view key, std::string_view value) {
//     header = {
//         .name = reinterpret_cast<const uint8_t *> (key.data()),
//         .name_len = key.size(),
//         .value = reinterpret_cast<const uint8_t *> (value.data()),
//         .value_len = value.size()
//     };
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::_stream_close(connection_stream_t *stream) {
//
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::force_close_(connection_t *conn_data) {
//     auto connection = *static_cast<std::shared_ptr<worker::connection> *> (conn_data->quiche_timer->data());
//
//     if (quiche_conn_is_closed(conn_data->conn)) {
//         quiche_stats stats;
//         quiche_path_stats path_stats;
//
//         quiche_conn_stats(conn_data->conn, &stats);
//         quiche_conn_path_stats(conn_data->conn, 0, &path_stats);
//
//         fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%zu ns cwnd=%zu\n",
//                 stats.recv, stats.sent, stats.lost, path_stats.rtt, path_stats.cwnd);
//
//         http_v3_cloudflare_quiche::get_dynamic_worker_(conn_data->worker)->connections.erase(conn_data->cid);
//
//
//         conn_data->quiche_timer->stop();
//
//         conn_data->quiche_timer->unbind();
//
//         if (conn_data->http3_conn) {
//             quiche_h3_conn_free(std::exchange(conn_data->http3_conn, nullptr));
//         }
//         quiche_conn_free(std::exchange(conn_data->conn, nullptr));
//     }
//     else {
//         if ((conn_data->status & CONN_HALF_CLOSED) == 0) {
//             conn_data->status|=CONN_HALF_CLOSED;
//             /** refuse connection */
//             quiche_conn_close(conn_data->conn, true, 0x02, reinterpret_cast <const uint8_t *> ("i/o timeout"), sizeof ("i/o timeout") - 1);
//         }
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::flush_connection_closed_(connection_t *conn_data) {
//     // if (!(conn_data->status & CONN_CLOSED) && (conn_data->status & CONN_HALF_CLOSED) && conn_data->streams.empty()) {
//     //     conn_data->status.fetch_xor(CONN_HALF_CLOSED);
//     // }
//
//     if (quiche_conn_is_closed(conn_data->conn)) {
//         if (!(conn_data->status & CONN_CLOSED)) {
//             conn_data->status|=(CONN_CLOSED);
//             if (conn_data->streams.size()) {
//                 http_v3_cloudflare_quiche::get_dynamic_worker_(conn_data->worker)->_reset_all_streams(conn_data);
//             }
//             else {
//                 force_close_(conn_data);
//             }
//         }
//         else {
//             if (conn_data->streams.empty()) {
//                 /* retry */
//                 force_close_(conn_data);
//             }
//         }
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::clean_connection_(void *conn_data) {
//     std::cout << "CONN CLOSE\n";
//     delete static_cast<connection_t*>(conn_data);
// }
//
//
// ssize_t manapi::net::worker::http_v3_cloudflare_quiche::buffer_size() {
//     return this->config->buffer_size();
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::_reset_all_streams(connection_t *conn_data) {
//     for (auto &stream: conn_data->streams) {
//         this->_stream_close(stream.second->as<connection_stream_t>());
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::_io_timeout(connection_t *conn_data) {
//     // if (conn_data->stream_read_cnt > 0) {
//     //     if (conn_data->read_total - conn_data->read_total_prev < conn_data->worker->config->speed_check_bytes()) {
//     //         //conn_data->status.fetch_or(CONN_CLOSED);
//     //         force_close_(conn_data);
//     //         return;
//     //     }
//     // }
//     //
//     // if (conn_data->stream_write_cnt > 0) {
//     //     if (conn_data->write_total - conn_data->write_total_prev < conn_data->worker->config->speed_check_bytes()) {
//     //         //conn_data->status.fetch_or(CONN_CLOSED);
//     //         force_close_(conn_data);
//     //         return;
//     //     }
//     // }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::quiche_timeout_(std::shared_ptr<ev::timer> w, worker::connection *connection) {
//     auto conn_data = connection->as<connection_t>();
//
//     quiche_conn_on_timeout(conn_data->conn);
//
//     http_v3_cloudflare_quiche::get_dynamic_worker_(conn_data->worker)->_quiche_flush_egress(conn_data);
//     http_v3_cloudflare_quiche::_quiche_timeout_again(conn_data);
//     http_v3_cloudflare_quiche::flush_connection_closed_(conn_data);
// }
//
// int manapi::net::worker::http_v3_cloudflare_quiche::_grab_headers(uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) {
//     auto client = static_cast<http::http_v2 *> (argp);
//     client->request_data.headers[std::string{reinterpret_cast<const char *>(name), name_len}] += std::string_view{reinterpret_cast<const char *>(value), value_len};
//     return 0;
// }
//
// bool manapi::net::worker::http_v3_cloudflare_quiche::_validate_token(std::string_view token, std::string &odcid, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) {
//     if (!token.starts_with("quiche")
//         || token.substr(sizeof("quiche") - 1, sockaddr_len) != std::string_view{reinterpret_cast<const char *> (sockaddr_src), sockaddr_len}){
//         return false;
//     }
//
//     odcid = token.substr(sizeof("quiche") - 1 + sockaddr_len);
//     return true;
// }
//
// std::string manapi::net::worker::http_v3_cloudflare_quiche::_gen_mint_token(std::string_view dcid, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) {
//     std::string token;
//     token.reserve(sizeof("quiche")-1 + sockaddr_len + dcid.size());
//     token += "quiche";
//     token += std::string_view{reinterpret_cast<const char *>(sockaddr_src), sockaddr_len};
//     token += dcid;
//     return std::move(token);
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_flush_egress(connection_t *connection) {
//     if (connection->status & CONN_CLOSED) {
//         return;
//     }
//
//     uint8_t out[MANAPI_MAX_DATAGRAM_SIZE];
//
//     quiche_send_info send_info;
//
//     while (true) {
//         ssize_t written = quiche_conn_send(connection->conn, out, sizeof(out), &send_info);
//
//         if (written == QUICHE_ERR_DONE) {
//             /* done writing */
//             break;
//         }
//
//         if (written < 0) {
//             /* failed to create packet */
//             return;
//         }
//
//         ev::buff_t buff[1] = {{.base = reinterpret_cast<char *> (out), .len = static_cast<std::size_t>(written)}};
//         ssize_t sent = this->udp_accept_->try_send(buff, 1, reinterpret_cast<sockaddr *>(&send_info.to));
//
//         if (sent != written) {
//             /* failed to send */
//             return;
//         }
//     }
//
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::_quiche_timeout_again(connection_t *connection) {
//     auto repeat = quiche_conn_timeout_as_millis(connection->conn);
//
//     connection->quiche_timer->repeat(repeat);
//     connection->quiche_timer->again();
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate() {
//     /* in the event loop */
//     for (auto &conn: this->connections) {
//         this->update_limit_rate_connection(*conn.second);
//     }
// }
//
// void manapi::net::worker::http_v3_cloudflare_quiche::update_limit_rate_connection(connection &conn) {
//     auto conn_data = conn.as<connection_t>();
//
//     auto delay = this->config->speed_check_delay().load();
//     auto size = this->config->speed_check_bytes().load();
//
//
//     conn_data->transfared_last_second = 0;
//
//     flush_write_(conn_data, false);
//     flush_read_(conn_data);
// }
//
// #endif
