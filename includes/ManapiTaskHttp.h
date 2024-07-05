#ifndef MANAPIHTTP_MANAPITASKHTTP_H
#define MANAPIHTTP_MANAPITASKHTTP_H

#include <map>
#include <ev++.h>
#include <openssl/ssl.h>
#include "ManapiTask.h"
#include "ManapiHttp.h"
#include "ManapiUtils.h"
#include "ManapiHttpTypes.h"

#define MANAPI_MAX_DATAGRAM_SIZE 1350

namespace manapi::net {
    class http;

    class http_task : public task{
    public:
        ~http_task()    override;

        // tcp
        explicit        http_task(int _fd, const sockaddr_storage &_client, const socklen_t &_client_len, manapi::net::http *new_http_server);
        // udp
        explicit        http_task(int _fd, char *buff, const ssize_t &size, const sockaddr_storage &_client, const socklen_t &_client_len, ev::io *loop, manapi::net::http *new_http_server);

        // doit for the task loop
        void            doit() override;

        void            set_http_server         (http *new_http_server);
        void            send_response           (http_response &res) const;
        static bool     read_next_part          (size_t &size, size_t &i, void *_http_task, request_data_t *request_data);

        // TODO: resolve
        [[deprecated]]  ssize_t read_next ();

        // QUIC TOOLS

        void            set_quic_config         (quiche_config *config);
        void            set_quic_map_conns      (QUIC_MAP_CONNS_T *new_map_conns);
        http_qc_conn_io *quic_create_connection (uint8_t *s_cid, size_t s_cid_len, uint8_t *od_cid, size_t od_cid_len);
        static int      quic_get_header         (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);
        static void     quic_flush_egress       (manapi::net::http_qc_conn_io *conn_io);
        static void     quic_delete_conn_io     (http_qc_conn_io *conn_io);

        // TCP TOOLS

        static void     tcp_stringify_headers   (std::string &response, http_response &res, char *delimiter);
        void            tcp_stringify_http_info (std::string &response, http_response &res, char *delimiter) const;

        // I/O
        ssize_t         socket_read             (const char *buff, const size_t &buff_size) const;
        ssize_t         socket_write            (const char *buff, const size_t &buff_size) const;

        ssize_t         openssl_read            (const char *buff, const size_t &buff_size) const;
        ssize_t         openssl_write           (const char *buff, const size_t &buff_size) const;

        std::function<ssize_t(const char *buff, const size_t &buff_size)>   mask_read;
        std::function<ssize_t(const char *buff, const size_t &buff_size)>   mask_write;

        std::function<ssize_t(http_response &res)>                          mask_response;


        uint8_t         *buff;
        ssize_t         buff_size;

        SSL *ssl = nullptr;
    private:
        void            tcp_doit ();
        void            udp_doit ();

        // QUIC
        static void     quic_timeout_cb (ev::timer &watcher, int revents);
        static void     mint_token(const uint8_t *d_cid, size_t d_cid_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *token, size_t *token_len);
        static bool     validate_token(const uint8_t *token, size_t token_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *od_cid, size_t *od_cid_len);
        static uint8_t  *gen_cid(uint8_t *cid, const size_t &cid_len);

        // TCP
        void            tcp_parse_request_response (char *response, const size_t &size, size_t &i);
        ssize_t         tcp_send_response (http_response &res);

        std::string     compress_file (const std::string &file, const std::string &folder, const std::string *compress, manapi::toolbox::compress::TEMPLATE_INTERFACE compressor) const;
        void            send_file (http_response &res, std::ifstream &f, ssize_t size, std::vector<toolbox::replace_founded_item> &replacers) const;
        void            send_file (http_response &res, std::ifstream &f, ssize_t size) const;

        void            get_ip_addr();
        void            handle_request (const http_handler_page *handler, const size_t &status = 200, const std::string &message = http_status.OK_200);
        void            send_error_response (const size_t &status, const std::string &message, const handlers_types_t &errors);

        static void     parse_uri_path_dynamic (request_data_t &request_data, char *response, const size_t &size, size_t &i, const std::function <void()>& finished = nullptr);

        http                    *http_server;

        quiche_config           *q_config       = nullptr;
        QUIC_MAP_CONNS_T        *q_map_conns    = nullptr;

        sockaddr_storage        client;
        socklen_t               client_len;

        int                     conn_fd{};
        fd_set                  read_fds{};

        char                    ip[INET6_ADDRSTRLEN]{};

        request_data_t          request_data;

        size_t                  conn_type;

        struct http_qc_conn_io  *conn_io = nullptr;
        int64_t                 s = -1;
        std::mutex              next_connection;
        std::mutex              got_connection;

        std::thread             *quic_thr = nullptr;
        bool                    quic_got_data = false;
        bool                    quic_work = false;
    };
}

#endif //MANAPIHTTP_MANAPITASKHTTP_H
