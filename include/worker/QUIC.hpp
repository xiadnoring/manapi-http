#ifndef MANAPIHTTP_QUIC_HPP
#define MANAPIHTTP_QUIC_HPP

#include <netdb.h>

#include "./Base.hpp"

namespace manapi::net::worker {
    class QUIC : public worker::base {
    public:
        QUIC (net::site &site);
        ~QUIC ();
        void init ();
        void onrecv(const std::shared_ptr<worker::base> &worker) override;
        static std::shared_ptr<worker::QUIC> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
    private:
        void _parse_frame (ssize_t &size);
        std::string _parse_string (ssize_t &i, const ssize_t &len, const ssize_t &size);
        template <typename T>
        T _parse_number (ssize_t &i, const ssize_t &size) {
            const int s = sizeof (T);
            if (i + s >= size) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Can not parse number");
            }
            T res = 0;
            for (int j = 0; j < s; j++, i++) {
                res = ( res << (j * 8) ) | buffer[i];
            }
            return res;
        }
        addrinfo *local;
        int reuseaddr_param = 1;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
        std::string buffer;
    };
}

#endif //MANAPIHTTP_QUIC_HPP
