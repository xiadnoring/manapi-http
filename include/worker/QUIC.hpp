#pragma once

// #if defined(__unix__)||defined(__APPLE__)
// #   include <netdb.h>
// #endif
//
// #include "../ManapiUtils.hpp"
// #include "./base_worker.hpp"
// #include "./QUIC_CB_Base.hpp"
// #include "./UDP.hpp"
//
// #if MANAPIHTTP_OPENSSL_DEPENDENCY
//
// namespace manapi::net::worker {
//     class quic : public udp {
//     public:
//         enum quic_packet_number_type {
//             QUIC_PACKET_NUMBER_INITIAL = 0x00,
//             QUIC_PACKET_NUMBER_HANDSHAKE = 0x01,
//             QUIC_PACKET_NUMBER_APPLICATION = 0x02
//         };
//
//         quic (net::site &site);
//         ~quic ();
//         void init ();
//         void onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) override;
//         static std::shared_ptr<worker::quic> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
//
//
//         template <typename T>
//         static T parse_number_ (std::string_view buffer, ssize_t &i) {
//             const int s = sizeof (T);
//             if (i + s >= buffer.size()) {
//                 THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "Can not parse number");
//             }
//             T res = 0;
//             for (int j = 0; j < s; j++, i++) {
//                 res = ( res << (j * 8) ) | static_cast<uint8_t>(buffer[i]);
//             }
//             return res;
//         }
//         static std::string_view parse_string_ (std::string_view buffer, ssize_t &i, const ssize_t &len);
//
//         void stop() override;
//         void close_connection(std::shared_ptr<connection> conn, bool clean_disconnect) override;
//         manapi::future<bool> configure_connection(std::shared_ptr<connection> conn) override;
//         ssize_t sync_read(worker::connection *conn, void *buff, ssize_t size) override;
//         ssize_t sync_write(worker::connection *conn, const void *buff, ssize_t size) override;
//         bool is_valid_connection(worker::connection &connection) override;
//
//     private:
//         static const int aead_token_size;
//         static const int quic_version;
//         static std::set <int> server_settings_only;
//         void init_connection_settings_ (quic_cb_base::quic_connection_t &connection);
//         future<void> send_frame (const std::shared_ptr<connection>& connection, std::string_view data, const bool &long_header_fmt, std::optional<quic_packet_type> packet_type = {});
//         std::shared_ptr<connection> registry_new_connection_ (std::string_view initial_key, std::string dcid, sockaddr_storage sockaddr_src, socklen_t sockaddr_len);
//         std::shared_ptr<connection> get_connection_ (const std::string &scid);
//         future<void> work_ (int fd, sockaddr_storage sockaddr_src, socklen_t sockaddr_len, quic_frame_data_t frame_data);
//
//         std::string make_ack_frame_ (std::shared_ptr<connection> &connection, std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds> tp);
//         std::string make_crypto_frame_ (std::shared_ptr<connection> &connection, std::string_view crypto_data);
//
//         std::map <std::string, std::shared_ptr<connection>> connections;
//         std::string hkdf_expand_label_ (std::string_view salt, std::string_view label, std::string_view ctx, const int &length);
//         /**
//          * gen initial keys
//          *
//          * @param dcid recv dest connection id
//          * @param type client/server in/out
//          * @return
//          */
//         quic_initial_keys_t gen_keys_ (std::string_view dcid, std::string_view type);
//         std::optional<quic_frame_data_t> parse_frame_ (std::string &buffer, ssize_t &size, sockaddr_storage &sockaddr_src, socklen_t &sockaddr_len);
//         void replace_prev_byte_with_ (std::string &buffer, ssize_t &i, ssize_t &size, uint8_t c);
//         template<typename T>
//         requires(std::is_integral_v<T>)
//         bool bit_at_ (const T &n, int i = 0) {
//             return (n) & (1 << i);
//         }
//         std::string build_iv (std::string iv, const uint64_t &seq);
//         uint64_t parse_length_number_ (std::string_view buffer, ssize_t &i, const ssize_t &size);
//         static std::string stringify_length_number_ (uint64_t n);
//         static void calculate_length_number_len_ (const uint64_t &n, int &slen, uint8_t &nlen);
//         template <typename T>
//         std::string number_to_bytes_ (T n) {
//             std::string result;
//             result.reserve(sizeof (n));
//             for (int i = sizeof (n) - 1; i >= 0; i--) {
//                 result += static_cast<char> ((n >> i * 8) & 0xFF);
//             }
//             return std::move(result);
//         }
//         std::string gbuffer;
//         size_t gbuffer_size;
//         uint64_t cnt_packets[3];
//         quic_packet_header_byte_t parse_header_byte (uint8_t hb);
//     };
// }
//
//
// #endif