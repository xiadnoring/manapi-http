#pragma once

#include "../ManapiSite.hpp"
#include <functional>

#include "Base.hpp"

namespace manapi::net::worker {
    struct quic_initial_keys_t {
        std::string key;
        std::string iv;
        std::string hp_key;
        uint64_t iv_cnt;
    };

    struct quic_packet_header_byte_t {
        bool long_header_format;
        bool fixed_bit;
        uint8_t packet_type;
        uint8_t reserved;
        uint8_t packet_number_length;
    };

    struct quic_frame_data_t {
        std::shared_ptr<worker::connection> connection;

        std::string dcid;
        std::string scid;
        std::string token;
        std::string data;

        uint32_t version;
        quic_packet_header_byte_t header_byte;
        size_t packet_number;
    };

    enum quic_error_code {
        QUIC_ERROR_NO_ERROR = 0x00,
        QUIC_ERROR_INTERNAL_ERROR = 0x01,
        QUIC_ERROR_CONNECTION_REFUSED = 0x02,
        QUIC_ERROR_FLOW_CONTROL_ERROR = 0x03,
        QUIC_ERROR_STREAM_LIMIT_ERROR = 0x04,
        QUIC_ERROR_STREAM_STATE_ERROR = 0x05,
        QUIC_ERROR_FINAL_SIZE_ERROR = 0x06,
        QUIC_ERROR_FRAME_ENCODING_ERROR = 0x07,
        QUIC_ERROR_TRANSPORT_PARAMETER_ERROR = 0x08,
        QUIC_ERROR_CONNECTION_ID_LIMIT_ERROR = 0x09,
        QUIC_ERROR_PROTOCOL_VIOLATION = 0x0a,
        QUIC_ERROR_INVALID_TOKEN = 0x0b,
        QUIC_ERROR_APPLICATION_ERROR = 0x0c,
        QUIC_ERROR_CRYPTO_BUFFER_EXCEEDED = 0x0d,
        QUIC_ERROR_KEY_UPDATE_ERROR = 0x0e,
        QUIC_ERROR_AEAD_LIMIT_REACHED = 0x0f,
        QUIC_ERROR_NO_VIABLE_PATH = 0x10,
        QUIC_ERROR_CRYPTO_ERROR = 0x0100,
        QUIC_ERROR_CRYPTO_ERROR256 = 0x01ff
    };

    enum quic_frame_type {
        QUIC_FRAME_PADDING = 0x00,
        QUIC_FRAME_PING = 0x01,
        QUIC_FRAME_ACK = 0x02,
        QUIC_FRAME_ACK_ECN = 0x03,
        QUIC_FRAME_RESET_STREAM = 0x04,
        QUIC_FRAME_STOP_SENDING = 0x05,
        QUIC_FRAME_CRYPTO = 0x06,
        QUIC_FRAME_NEW_TOKEN = 0x07,
        QUIC_FRAME_STREAM0 = 0x08,
        QUIC_FRAME_STREAM8 = 0x0f,
        QUIC_FRAME_MAX_DATA = 0x10,
        QUIC_FRAME_MAX_STREAM_DATA = 0x11,
        QUIC_FRAME_MAX_STREAMS = 0x12,
        QUIC_FRAME_MAX_STREAMS1 = 0x13,
        QUIC_FRAME_DATA_BLOCKED = 0x14,
        QUIC_FRAME_STREAM_DATA_BLOCKED = 0x15,
        QUIC_FRAME_STREAMS_BLOCKED = 0x16,
        QUIC_FRAME_STREAMS_BLOCKED1 = 0x17,
        QUIC_FRAME_NEW_CONNECTION_ID = 0x18,
        QUIC_FRAME_RETIRE_CONNECTION_ID = 0x19,
        QUIC_FRAME_PATH_CHALLENGE = 0x1a,
        QUIC_FRAME_PATH_RESPONSE = 0x1b,
        QUIC_FRAME_CONNECTION_CLOSE = 0x1c,
        QUIC_FRAME_CONNECTION_CLOSE1 = 0x1d,
        QUIC_FRAME_HANDSHAKE_DONE = 0x1e
    };

    enum quic_connection_steps {
        QUIC_STEP_CLIENT_HELLO,
        QUIC_STEP_SERVER_HELLO,
        QUIC_STEP_SERVER_HANDSHAKE,
        QUIC_STEP_SERVER_HANDSHAKE_FINISHED,
        QUIC_STEP_CLIENT_HANDSHAKE_ACK,
        QUIC_STEP_CLIENT_HANDSHAKE,
        QUIC_STEP_CLIENT_HANDSHAKE_FINISHED,
        QUIC_STEP_APPLICATION,
        QUIC_STEP_CLOSE
    };

    enum quic_packet_type {
        QUIC_PACKET_INITIAL = 0x00,
        QUIC_PACKET_HANDSHAKE = 0x02
    };

    enum quic_setting_type {
        QUIC_SETTING_ORIGINAL_DESTINATION_CONNECTION_ID = 0x00,
        QUIC_SETTING_MAX_IDLE_TIMEOUT = 0x01,
        QUIC_SETTING_STATELESS_RESET_TOKEN = 0x02,
        QUIC_SETTING_MAX_UDP_UPLOAD_SIZE = 0x03,
        QUIC_SETTING_INITIAL_MAX_DATA = 0x04,
        QUIC_SETTING_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL = 0x05,
        QUIC_SETTING_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE = 0x06,
        QUIC_SETTING_INITIAL_MAX_STREAM_DATA_UNI = 0x07,
        QUIC_SETTING_INITIAL_MAX_STREAMS_BIDI = 0x08,
        QUIC_SETTING_INITIAL_MAX_STREAMS_UNI = 0x09,
        QUIC_SETTING_ACK_DELAY_EXPONENT = 0x0a,
        QUIC_SETTING_MAX_ACK_DELAY = 0x0b,
        QUIC_SETTING_DISABLE_ACTIVE_MIGRATION = 0x0c,
        QUIC_SETTING_PREFERED_ADDRESS = 0x0d,
        QUIC_SETTING_ACTIVE_CONNECTION_ID_LIMIT = 0x0e,
        QUIC_SETTING_INITIAL_SOURCE_CONNECTION_ID = 0x0f,
        QUIC_SETTING_RETRY_SOURCE_CONNECTION_ID = 0x10
    };

    class quic_cb_base {
    public:
        struct quic_connection_t {
            std::atomic<base::connection_status> status;
            std::map <int, std::atomic<int>> settings;

            std::string scid;
            std::string dcid;
            std::string token;

            quic_initial_keys_t server_keys;
            quic_initial_keys_t client_keys;

            sockaddr_storage sockaddr_src;
            socklen_t sockaddr_len;

            quic_connection_steps step;
            std::shared_ptr<quic_cb_base> worker;
            std::set <std::pair <int, int> > acks{};

            std::function<void()> io_handle;
        };

        explicit quic_cb_base (net::site &site);
        virtual ~quic_cb_base();

        ssize_t send (void *buf, ssize_t buflen);
        void set_connection (std::weak_ptr <worker::connection> connection);

        static void global_init (net::site &site);
        static void global_deinit (net::site &site);

        virtual future<void> client_init (quic_frame_data_t frame, std::string &server_hello, std::string &server_handshake);
        virtual future<void> client_init_ack (quic_frame_data_t frame);
        virtual future<void> client_handshake (quic_frame_data_t frame);
        virtual future<void> client_handshake_finished (quic_frame_data_t frame, std::string &server_handshake_finished);
        virtual future<void> client_application (quic_frame_data_t frame);

        std::function <manapi::future<void>(quic_frame_type type, std::string data)> send_frame;
    private:
        std::weak_ptr <worker::connection> connection;
    };


}
