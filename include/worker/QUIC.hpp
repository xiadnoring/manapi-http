#pragma once

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "../ManapiUtils.hpp"
#include "./base_worker.hpp"
#include "./QUIC_CB_Base.hpp"
#include "./UDP.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

namespace manapi::net::worker {
    class quic : public udp {
    public:
        enum quic_packet_number_type {
            QUIC_PACKET_NUMBER_INITIAL = 0x00,
            QUIC_PACKET_NUMBER_HANDSHAKE = 0x01,
            QUIC_PACKET_NUMBER_APPLICATION = 0x02
        };

        quic (net::site &site);
        ~quic ();
        void init ();
        void onrecv(ev::io &watcher, int revents) override;
        static std::shared_ptr<worker::quic> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);


        template <typename T>
        static T _parse_number (std::string_view buffer, ssize_t &i) {
            const int s = sizeof (T);
            if (i + s >= buffer.size()) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Can not parse number");
            }
            T res = 0;
            for (int j = 0; j < s; j++, i++) {
                res = ( res << (j * 8) ) | static_cast<uint8_t>(buffer[i]);
            }
            return res;
        }
        static std::string_view _parse_string (std::string_view buffer, ssize_t &i, const ssize_t &len);

    private:
        static const int aead_token_size;
        static const int quic_version;
        static std::set <int> server_settings_only;
        void _init_connection_settings (quic_cb_base::quic_connection_t &connection);
        future<void> send_frame (const std::shared_ptr<connection>& connection, std::string_view data, const bool &long_header_fmt, std::optional<quic_packet_type> packet_type = {});
        std::shared_ptr<connection> _registry_new_connection (std::string_view initial_key, std::string dcid, sockaddr_storage sockaddr_src, socklen_t sockaddr_len);
        std::shared_ptr<connection> _get_connection (const std::string &scid);
        future<void> _work (int fd, sockaddr_storage sockaddr_src, socklen_t sockaddr_len, quic_frame_data_t frame_data);

        std::string _make_ack_frame (std::shared_ptr<connection> &connection, std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds> tp);
        std::string _make_crypto_frame (std::shared_ptr<connection> &connection, std::string_view crypto_data);

        std::map <std::string, std::shared_ptr<connection>> connections;
        std::string _hkdf_expand_label (std::string_view salt, std::string_view label, std::string_view ctx, const int &length);
        /**
         * gen initial keys
         *
         * @param dcid recv dest connection id
         * @param type client/server in/out
         * @return
         */
        quic_initial_keys_t _gen_keys (std::string_view dcid, std::string_view type);
        std::optional<quic_frame_data_t> _parse_frame (std::string &buffer, ssize_t &size, sockaddr_storage &sockaddr_src, socklen_t &sockaddr_len);
        void _replace_prev_byte_with (std::string &buffer, ssize_t &i, ssize_t &size, uint8_t c);
        template<typename T>
        requires(std::is_integral_v<T>)
        bool _bit_at (const T &n, int i = 0) {
            return (n) & (1 << i);
        }
        std::string build_iv (std::string iv, const uint64_t &seq);
        uint64_t _parse_length_number (std::string_view buffer, ssize_t &i, const ssize_t &size);
        static std::string _stringify_length_number (uint64_t n);
        static void _calculate_length_number_len (const uint64_t &n, int &slen, uint8_t &nlen);
        template <typename T>
        std::string _number_to_bytes (T n) {
            std::string result;
            result.reserve(sizeof (n));
            for (int i = sizeof (n) - 1; i >= 0; i--) {
                result += static_cast<char> ((n >> i * 8) & 0xFF);
            }
            return std::move(result);
        }
        std::string gbuffer;
        size_t gbuffer_size;
        uint64_t cnt_packets[3];
        quic_packet_header_byte_t parse_header_byte (uint8_t hb);
    };
}


#endif