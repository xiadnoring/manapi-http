#include <fcntl.h>
#include "worker/QUIC.hpp"

#include <memory>

#include "ManapiParams.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiAES.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "crypto/ManapiHKDF.hpp"
#include "worker/QUIC_OpenSSL_TLS.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

static constexpr char SHA1_FIRST_COLLISION[] = "38762cf7f55934b34d179ae6a4c80cadccbb7f0a";

const int manapi::net::worker::quic::aead_token_size = 16;
const int manapi::net::worker::quic::quic_version = 1;
std::set <int> manapi::net::worker::quic::server_settings_only = {QUIC_SETTING_ORIGINAL_DESTINATION_CONNECTION_ID, QUIC_SETTING_RETRY_SOURCE_CONNECTION_ID, QUIC_SETTING_PREFERED_ADDRESS};

manapi::net::worker::quic::quic(net::site &site) : udp(site) {
    this->gbuffer_size = 1350;
    this->gbuffer.reserve(this->gbuffer_size);
}

manapi::net::worker::quic::~quic() {
    quic_openssl_tls::global_deinit(this->site);
}

void manapi::net::worker::quic::init() {
    udp::init();

    quic_openssl_tls::global_init(this->site);

    // ssize_t i = 0;
    // ssize_t size = 12312312312312;
    // auto data = std::string{"04"};
    // data = crypto::strhex2strdec(data);
    // std::cout << "1073741824 " << (this->_parse_length_number(data, i, size)) << "\n";
    //
    // std::cout << crypto::strdec2strhex(_stringify_length_number (4611686018427387903)) << " "  << (crypto::strdec2strhex(_stringify_length_number (4611686018427387903)) == "FFFFFFFFFFFFFFFF") << "\n";
    // std::cout << crypto::strdec2strhex(_stringify_length_number (63)) << " "  << (crypto::strdec2strhex(_stringify_length_number (63)) == "3F") << "\n";
    // std::cout << crypto::strdec2strhex(_stringify_length_number (16383)) << " "  << (crypto::strdec2strhex(_stringify_length_number (16383)) == "7FFF") << "\n";
    // std::cout << crypto::strdec2strhex(_stringify_length_number (1073741823)) << " "  << (crypto::strdec2strhex(_stringify_length_number (1073741823)) == "BFFFFFFF") << "\n";
    // std::cout << crypto::strdec2strhex(_stringify_length_number (1073741824)) << " "  << (crypto::strdec2strhex(_stringify_length_number (1073741824)) == "C000000040000000") << "\n";
}

void manapi::net::worker::quic::onrecv(ev::io &watcher, int revents) {
    while (true) {
        sockaddr_storage sockaddr_src{};
        socklen_t sockaddr_len = sizeof (sockaddr_src);
        memset(&sockaddr_src, '\0', sockaddr_len);
        ssize_t rhs = ::recvfrom(watcher.fd, this->gbuffer.data(), this->gbuffer_size, 0, reinterpret_cast <sockaddr *>(&sockaddr_src), &sockaddr_len);
        if (rhs < 0) {
            return;
        }

        auto frame_data = this->_parse_frame(this->gbuffer, rhs, sockaddr_src, sockaddr_len);

        if (frame_data.has_value()) {
            async::run(this->site.async_context(),
                this->_work(watcher.fd, sockaddr_src, sockaddr_len, std::move(frame_data.value())));
        }
    }
}

std::shared_ptr<manapi::net::worker::quic> manapi::net::worker::quic::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::quic>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::shared_ptr<manapi::net::worker::connection> manapi::net::worker::quic::_registry_new_connection(std::string_view initial_key, std::string dcid, sockaddr_storage sockaddr_src, socklen_t sockaddr_len) {
    std::string scid;

    do {
        scid = crypto::random_string(16); // 128 bit
    }
    while (this->connections.contains(scid));

    auto worker = std::make_shared<quic_openssl_tls>(this->site);

    auto connection = std::make_shared<class worker::connection>(new quic_cb_base::quic_connection_t {
        .status = CONN_IDLE,
        .settings = {},
        .scid = scid,
        .dcid = std::move(dcid),
        .token = {},
        .server_keys = this->_gen_keys(initial_key, "server in"),
        .client_keys = this->_gen_keys(initial_key, "client in"),
        .sockaddr_src = sockaddr_src,
        .sockaddr_len = sockaddr_len,
        .step = QUIC_STEP_CLIENT_HELLO,
        .worker = worker,
        .acks = {},
        .io_handle = {nullptr}
    }, [] (void *ptr) -> void {
        delete static_cast<quic_cb_base::quic_connection_t *> (ptr);
    });

    connection->as<quic_cb_base::quic_connection_t>().worker->set_connection(connection);

    auto it = this->connections.insert({
        scid,
        connection
    });

    if (!it.second) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_QUIC_PROTOCOL_ERROR, "Failed to insert new quic connection. scid: {}", crypto::strdec2strhex(scid));
    }

    return std::move(connection);
}

std::shared_ptr<manapi::net::worker::connection> manapi::net::worker::quic::_get_connection(const std::string &scid) {
    auto it = this->connections.find(scid);
    if (it == this->connections.end()) {
        return {nullptr};
    }
    return it->second;
}

manapi::future<void> manapi::net::worker::quic::_work(int fd, sockaddr_storage sockaddr_src, socklen_t sockaddr_len, quic_frame_data_t frame_data) {
    auto tp = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now());

    auto connection = frame_data.connection;
    auto &conn_data = connection->as<quic_cb_base::quic_connection_t>();

    if (frame_data.header_byte.long_header_format) {
        switch (frame_data.header_byte.packet_type) {
            case QUIC_PACKET_INITIAL: {
                if (frame_data.packet_number == 0) {
                    std::string server_hello;
                    std::string server_handshake;
                    co_await conn_data.worker->client_init(std::move(frame_data), server_hello, server_handshake);

                    std::cout << "TLS_1.3 server hello : " << crypto::strdec2strhex(server_hello) << "\n";
                    std::cout << "TLS_1.3 server handshake : " << crypto::strdec2strhex(server_handshake) << "\n";

                    /* inital server */
                    auto frames = this->_make_ack_frame(connection, tp)
                        + this->_make_crypto_frame(connection, server_hello);
                    co_await this->send_frame(connection, frames, true, QUIC_PACKET_INITIAL);

                    /* handshake */

                    // Send large handshake data (data size > 1350)
                }
                else if (frame_data.packet_number == 1) {
                    co_await connection->as<quic_cb_base::quic_connection_t>().worker->client_init_ack(std::move(frame_data));
                }
                break;
            }
            case QUIC_PACKET_HANDSHAKE: {
                if (frame_data.packet_number == 0) {
                    co_await connection->as<quic_cb_base::quic_connection_t>().worker->client_handshake(std::move(frame_data));
                }
                else if (frame_data.packet_number == 1) {
                    std::string server_handshake_finished;
                    co_await connection->as<quic_cb_base::quic_connection_t>().worker->client_handshake_finished(std::move(frame_data), server_handshake_finished);
                }
                break;
            }
            default:
                break;
        }
    }
    else {
        co_await connection->as<quic_cb_base::quic_connection_t>().worker->client_application(std::move(frame_data));
    }

    co_return;
}

std::string manapi::net::worker::quic::_make_ack_frame(std::shared_ptr<connection> &connection, std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds> tp) {
    auto &conn_data = connection->as<quic_cb_base::quic_connection_t>();
    std::string data;
    data.reserve(16);

    data += static_cast<uint8_t>(QUIC_FRAME_ACK);
    if (conn_data.acks.empty()) {
        data += '\000';
    }
    else {
        data += quic::_stringify_length_number(conn_data.acks.rbegin()->second);
    }

    auto delay = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()) - tp;
    data += quic::_stringify_length_number(crypto::binpow(2, conn_data.settings[QUIC_SETTING_ACK_DELAY_EXPONENT]) * delay.count());

    if (conn_data.acks.empty()) {
        data += '\000';
    }
    else {
        data += quic::_stringify_length_number(conn_data.acks.size() - 1);
    }

    if (conn_data.acks.empty()) {
        data += '\000';
    }
    else {
        auto rit = conn_data.acks.rbegin();
        data += quic::_stringify_length_number(rit->second - rit->first + 1);

        for (auto it = std::next(rit); it != conn_data.acks.rend(); ++it) {
            data += quic::_stringify_length_number(it->first - rit->second - 1);
            data += quic::_stringify_length_number(it->second - rit->first + 1);
            rit = it;
        }
    }

    return std::move(data);
}

std::string manapi::net::worker::quic::_make_crypto_frame(std::shared_ptr<connection> &connection, std::string_view crypto_data) {
    std::string frame;
    /* 32 bytes for header */
    frame.reserve(crypto_data.size() + 32);
    /* frame type */
    frame += static_cast<uint8_t>(QUIC_FRAME_CRYPTO);
    /* data offset */
    frame += '\0';
    /* data size */
    frame += quic::_stringify_length_number(crypto_data.size());
    /* data */
    frame += crypto_data;
    return std::move(frame);
}

std::string manapi::net::worker::quic::_hkdf_expand_label(std::string_view salt, std::string_view label, std::string_view ctx, const int &length) {
    std::string blabel = "tls13 ";
    blabel+=label;

    // length
    std::string info = this->_number_to_bytes <uint16_t> (length);

    // label
    info += this->_number_to_bytes <uint8_t> (blabel.size());
    info += blabel;

    // context
    info += this->_number_to_bytes<uint8_t>(ctx.size());
    info += ctx;

    return std::move(crypto::hkdf_expand(salt, info, length));
}

manapi::net::worker::quic_initial_keys_t manapi::net::worker::quic::_gen_keys(std::string_view dcid, std::string_view type) {
    quic_initial_keys_t keys;

    std::string salt = crypto::strhex2strdec(std::string({SHA1_FIRST_COLLISION, sizeof (SHA1_FIRST_COLLISION) - 1}));
    auto init_secret = crypto::hkdf_extract(salt, dcid, crypto::SHA_256);
    auto secret = this->_hkdf_expand_label(init_secret, type, "", 32);
    keys.iv = this->_hkdf_expand_label(secret, "quic iv", "", 12);
    keys.key = this->_hkdf_expand_label(secret, "quic key", "", 16);
    keys.hp_key = this->_hkdf_expand_label(secret, "quic hp", "", 16);
    keys.iv_cnt = 0;

    return std::move(keys);
}

std::string manapi::net::worker::quic::build_iv(std::string iv, const uint64_t &seq) {
    size_t i;
    for (i = 0; i < 8; i++) {
        iv[iv.size()-1-i] ^= ((seq >> (i*8))&0xFF);
    }
    return std::move(iv);
}

std::string_view manapi::net::worker::quic::_parse_string(std::string_view buffer, ssize_t &i, const ssize_t &len) {
    if (len + i > buffer.size()) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "No enough left space");
    }
    auto j = i;
    i += len;
    return {buffer.data() + j, static_cast<size_t>(len)};
}

void manapi::net::worker::quic::_init_connection_settings(quic_cb_base::quic_connection_t &connection) {
    connection.settings[QUIC_SETTING_ORIGINAL_DESTINATION_CONNECTION_ID].store(0);
    connection.settings[QUIC_SETTING_MAX_IDLE_TIMEOUT].store(0);
    connection.settings[QUIC_SETTING_STATELESS_RESET_TOKEN].store(0);
    connection.settings[QUIC_SETTING_MAX_UDP_UPLOAD_SIZE].store(1350);
    connection.settings[QUIC_SETTING_INITIAL_MAX_DATA].store(0);
    connection.settings[QUIC_SETTING_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL].store(0);
    connection.settings[QUIC_SETTING_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE].store(0);
    connection.settings[QUIC_SETTING_INITIAL_MAX_STREAM_DATA_UNI].store(0);
    connection.settings[QUIC_SETTING_INITIAL_MAX_STREAMS_BIDI].store(0);
    connection.settings[QUIC_SETTING_INITIAL_MAX_STREAMS_UNI].store(0);
    connection.settings[QUIC_SETTING_ACK_DELAY_EXPONENT].store(3);
    connection.settings[QUIC_SETTING_MAX_ACK_DELAY].store(25);
    connection.settings[QUIC_SETTING_DISABLE_ACTIVE_MIGRATION].store(0);
    connection.settings[QUIC_SETTING_PREFERED_ADDRESS].store(0);
    connection.settings[QUIC_SETTING_ACTIVE_CONNECTION_ID_LIMIT].store(2);
    connection.settings[QUIC_SETTING_INITIAL_SOURCE_CONNECTION_ID].store(0);
    connection.settings[QUIC_SETTING_RETRY_SOURCE_CONNECTION_ID].store(0);
}

manapi::future<> manapi::net::worker::quic::send_frame(const std::shared_ptr<connection>& connection, std::string_view data, const bool &long_header_fmt, std::optional<quic_packet_type> packet_type) {
    auto &conn_data = connection->as<quic_cb_base::quic_connection_t>();

    std::string packet;
    packet.reserve(data.size());

    uint64_t packet_number;

    if (long_header_fmt) {
        switch (static_cast<uint8_t> (packet_type.value())) {
            case QUIC_PACKET_INITIAL:
                packet_number = this->cnt_packets[QUIC_PACKET_NUMBER_INITIAL]++;
            break;
            case QUIC_PACKET_HANDSHAKE:
                packet_number = this->cnt_packets[QUIC_PACKET_NUMBER_HANDSHAKE]++;
            break;
            default:
                THROW_MANAPIHTTP_EXCEPTION(ERR_QUIC_PROTOCOL_ERROR, "Invalid packet type: {}", static_cast<uint8_t> (packet_type.value()));
        }
    }
    else {
        packet_number = this->cnt_packets[QUIC_PACKET_NUMBER_APPLICATION]++;
    }

    /* packet number len */
    uint8_t packet_number_len = 0x04;

    {
        /* header byte */
        uint8_t header_byte = 0x00;
        if (long_header_fmt) {
            /* long header format */
            header_byte |= 0x80;
            /* fixed bit */
            header_byte |= 0x40;
            /* package type */
            header_byte |= (static_cast<uint8_t> (packet_type.value()) << 4);
            /* packet number field length */
            header_byte |= static_cast<uint8_t>(packet_number_len-1);
        }
        packet += static_cast<char>(header_byte);
    }

    {
        /* quic version */
        packet += crypto::number2bytes<uint32_t>(quic::quic_version);
    }

    {
        /* dcid length */
        packet += crypto::number2bytes<uint8_t>(conn_data.dcid.size());
        /* dcid */
        packet += conn_data.dcid;
    }

    {
        /* scid length */
        packet += crypto::number2bytes<uint8_t>(conn_data.scid.size());
        /* scid */
        packet += conn_data.scid;
    }

    {
        /* token length */
        packet += crypto::number2bytes<uint8_t>(conn_data.token.size());
        /* token */
        packet += conn_data.token;
    }

    {
        /* packet length */
        packet += quic::_stringify_length_number(data.size() + packet_number_len + quic::aead_token_size);
    }

    const int packet_number_pos = static_cast<int>(packet.size());
    {
        /* packet number */
        packet.append(packet_number_len, '\0');

        for (int j = (packet_number_len-1) + packet_number_pos; j >= packet_number_pos; --j) {
            packet[j] = static_cast<char>(packet_number & 0xFF);
            packet_number >>= 8;
        }
    }

    {
        /* data */
        std::string_view aad {packet};
        std::string iv = quic::build_iv(conn_data.server_keys.iv, conn_data.server_keys.iv_cnt++);
        std::string tag;
        packet += crypto::aead_encrypt(data, aad, conn_data.server_keys.key, iv, tag, crypto::AES_128_GCM);
        packet += tag;
    }

    /* apply header protection */
    std::string header_protection_key = crypto::aes_encrypt(std::string_view{packet.data() + packet_number_pos + 4, 16}, conn_data.server_keys.hp_key, {}, crypto::AES_128_ECB);
    //uint8_t hd = static_cast<uint8_t>(packet[0]);
    packet[0] = static_cast<char>((static_cast<uint8_t>(packet[0]) & 0xF0) | ((static_cast<uint8_t>(packet[0]) & 0x0F) ^ (static_cast<uint8_t>(header_protection_key[0]) & 0x0F)));
    for (int i = packet_number_pos, j = 1; i < packet_number_pos + packet_number_len; ++i, ++j) {
        packet[i] = static_cast<char>(static_cast<uint8_t>(packet[i]) ^ static_cast<uint8_t>(header_protection_key[j]));
    }
    std::cout << "============[SEND]==========\n";
    std::cout << "scid: " << crypto::strdec2strhex(conn_data.scid) << "\n";
    std::cout << "dcid: " << crypto::strdec2strhex(conn_data.dcid) << "\n";
    std::cout << "send: " << crypto::strdec2strhex(packet) << "\n";

    int flg = 0;
    #if defined(__unix__)||defined(__APPLE__)
        flg |= MSG_DONTWAIT;
    #endif
    ::sendto(this->fd, packet.data(), packet.size(), flg, reinterpret_cast <sockaddr *> (&conn_data.sockaddr_src), conn_data.sockaddr_len);

    co_return;
}

uint64_t manapi::net::worker::quic::_parse_length_number(std::string_view buffer, ssize_t &i, const ssize_t &size) {
    uint8_t c = buffer[i++];
    int len = (c >> 6);
    uint64_t n = c xor (len << 6);
    len = crypto::binpow(2, len);
    for (int j = 1; j < len; ++j) {
        c = buffer[i++];
        n = (n << 8) | c;
    }
    return n;
}

std::string manapi::net::worker::quic::_stringify_length_number(uint64_t n) {
    int slen;
    uint8_t nlen;

    quic::_calculate_length_number_len (n, slen, nlen);

    std::string str;
    str.resize(slen);

    for (int i = slen - 1; i >= 0; i--) {
        str[i] = static_cast<char>(n & 0xFF);
        n = (n >> 8);
    }

    str[0] = static_cast<char>(static_cast<uint8_t>(str[0]) | static_cast<uint8_t>(nlen << 6));

    return std::move(str);
}

void manapi::net::worker::quic::_calculate_length_number_len(const uint64_t &n, int &slen, uint8_t &nlen) {
    if (n < 64) {
        slen = 1;
        nlen = 0;
    }
    else if (n < 16384) {
        slen = 2;
        nlen = 1;
    }
    else if (n < 1073741824) {
        slen = 4;
        nlen = 2;
    }
    else if (n < 4611686018427387904) {
        slen = 8;
        nlen = 3;
    }
}

manapi::net::worker::quic_packet_header_byte_t manapi::net::worker::quic::parse_header_byte(uint8_t hb) {
    return quic_packet_header_byte_t{
        .long_header_format = static_cast<bool>(hb & 0x80),
        .fixed_bit = static_cast<bool>(hb & 0x40),
        .packet_type = static_cast<uint8_t>(hb & 0x30),
        .reserved = static_cast<uint8_t>(hb & 0x0C),
        .packet_number_length = static_cast<uint8_t>(hb & 0x03)
    };
}

std::optional<manapi::net::worker::quic_frame_data_t> manapi::net::worker::quic::_parse_frame(std::string &buffer, ssize_t &size, sockaddr_storage &sockaddr_src, socklen_t &sockaddr_len) {
    try {
        ssize_t i = 0;
        auto hb = quic::_parse_number<uint8_t>(buffer, i); // Header Byte
        auto quic_version = quic::_parse_number<uint32_t>(buffer, i);

        auto des_id_len = quic::_parse_number<uint8_t>(buffer, i);
        std::string des_id = std::string{quic::_parse_string(buffer, i, des_id_len)};

        auto src_id_len = quic::_parse_number<uint8_t>(buffer, i);
        std::string src_id = std::string{quic::_parse_string(buffer, i, src_id_len)};

        auto token_id_len = quic::_parse_number<uint8_t>(buffer, i);
        auto token_id = std::string{quic::_parse_string(buffer, i, token_id_len)};

        /* payload data length */
        auto len = this->_parse_length_number(buffer, i, size);

        std::shared_ptr<connection> connection;

        if (!this->connections.contains(des_id)) {
            connection = this->_registry_new_connection(des_id, src_id, sockaddr_src, sockaddr_len);
        }
        else {
            connection = this->_get_connection(des_id);
        }

        if (!connection) {
            return {};
        }

        auto &conn_data = connection->as<quic_cb_base::quic_connection_t>();

        // header protection calculate
        std::string_view sample {buffer.data() + i + 4, 16};
        auto bsample = crypto::strdec2strhex(sample);
        auto header_protection_key = crypto::aes_encrypt(sample, conn_data.client_keys.hp_key, "", crypto::AES_128_ECB);
        auto bheader_protection_key = crypto::strdec2strhex(header_protection_key);


        // apply header protection key
        hb = (hb & 0xF0) | (static_cast<uint8_t>(hb & 0x0F) xor static_cast<uint8_t>(static_cast<uint8_t>(header_protection_key[0]) & 0x0F));
        auto header_byte_data = this->parse_header_byte(hb);
        buffer[0] = static_cast<char>(hb);


        /* packet number */
        uint64_t pnum = 0;
        {
            uint8_t mj = header_byte_data.packet_number_length+1;
            for (uint8_t j = 1; j <= mj; j++) {
                auto c_pnum = static_cast<uint8_t>(quic::_parse_number<uint8_t>(buffer, i) xor static_cast<uint8_t>(header_protection_key[j]));
                pnum = (pnum << 8) | c_pnum;
                this->_replace_prev_byte_with(buffer, i, size, c_pnum);
            }

            len-=mj;
        }
        auto aad_len = i;

        std::cout << "header protection key: " << bheader_protection_key << "\n";
        std::cout << std::format("scid: {}, dcid: {}", crypto::strdec2strhex(src_id), crypto::strdec2strhex(des_id)) << "\n";
        std::cout << std::format("chp: {}, civ: {}, ckey: {}", crypto::strdec2strhex(conn_data.client_keys.hp_key), crypto::strdec2strhex(conn_data.client_keys.iv), crypto::strdec2strhex(conn_data.client_keys.key)) << "\n";
        std::cout << crypto::strdec2strhex(buffer) << "\n";

        std::string_view aad (buffer.data(), aad_len);
        std::string_view data = std::string_view{quic::_parse_string(buffer, i, static_cast<ssize_t>(len))};
        std::string_view encrypted (data.data(), data.size() - 16);
        std::string_view auth_tag (data.data() + encrypted.size(), 16);

        std::string iv = build_iv(conn_data.client_keys.iv, std::max(conn_data.client_keys.iv_cnt++, pnum));

        std::string decrypted_data;
        try {
            decrypted_data = crypto::aead_decrypt(std::string_view{encrypted.data(), encrypted.size()}, aad, conn_data.client_keys.key, iv, auth_tag, crypto::AES_128_GCM);
        }
        catch (std::exception const &e) {
            return {};
        }
        return std::move(quic_frame_data_t{
            .connection = std::move(connection),
            .dcid = std::move(des_id),
            .scid = std::move(src_id),
            .token = std::move(token_id),
            .data = std::move(decrypted_data),
            .version = quic_version,
            .header_byte = header_byte_data,
            .packet_number = pnum
        });
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("An error has occurred: {}", e.what());
    }
    return {};
}

void manapi::net::worker::quic::_replace_prev_byte_with(std::string &buffer, ssize_t &i, ssize_t &size, uint8_t c) {
    if (i < 1 || i > size) {
        return;
    }

    buffer[i-1] = static_cast<char>(c);
}

#endif
