#ifndef MANAPIHTTP_MANAPITASKHTTP_H
#define MANAPIHTTP_MANAPITASKHTTP_H

#include <map>
#include <ev++.h>
#include <openssl/ssl.h>

#include "ManapiTask.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiSite.hpp"

#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"

namespace manapi::net {
#define MANAPI_HTTP_BUFF_BINARY 0
#define MANAPI_HTTP_BUFF_FILE   1

#define MANAPI_HTTP_READ_INTERFACE std::function<ssize_t(char *buff, const size_t &buff_size)>
#define MANAPI_HTTP_WRITE_INTERFACE std::function<ssize_t(const char *buff, const size_t &buff_size)>
#define MANAPI_HTTP_WRITE_FILE_INTERFACE std::function<void(const std::string &filePath, const ssize_t &start, const ssize_t &size)>

    struct file_transfer_information {
        std::string filePath;
        ssize_t start;
        ssize_t size;
    };

    enum conn_type {
        CONN_UDP = 0,
        CONN_TCP = 1
    };

    class http_task : public task {
    public:
        ~http_task()    override;

        // tcp/udp
        explicit                http_task(int _fd, const sockaddr &_client, const socklen_t &_client_len, class site *site, class config *config, const enum conn_type &conn_type = CONN_UDP);

        // doit for the task loop
        void                    doit() override;

        void                    send_response           (http_response &res);
        static size_t           read_next_part          (size_t &size, size_t &i, void *_http_task, request_data_t *request_data);

        // TODO: resolve
        [[deprecated]]  ssize_t read_next () const;

        // QUIC TOOLS

        static http_quic_conn_io*quic_create_connection (uint8_t *s_cid, size_t s_cid_len, uint8_t *od_cid, size_t od_cid_len, const int &conn_fd, const sockaddr_storage &client, const socklen_t &client_len, class config *config, class site *site, QUIC_MAP_CONNS_T *quic_map_conns);
        static int              quic_get_header         (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);
        static void             quic_flush_egress       (QUIC_MAP_CONNS_T *conns, manapi::net::http_quic_conn_io *conn_io, class site *site);
        static void             quic_delete_conn_io     (http_quic_conn_io *conn_io, class site *site);
/**
         * to_delete -> true
         * and skip await I/O
         */
        static void             quic_set_to_delete (http_task *task);
        static void             quic_timeout_cb (std::string cid, QUIC_MAP_CONNS_T *conns, class site *site);
        static void             mint_token(const uint8_t *d_cid, size_t d_cid_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *token, size_t *token_len);
        static bool             validate_token(const uint8_t *token, size_t token_len, struct sockaddr_storage *addr, socklen_t addr_len, uint8_t *od_cid, size_t *od_cid_len);
        static uint8_t          *gen_cid(uint8_t *cid, const size_t &cid_len);

        // TCP TOOLS

        static void             tcp_stringify_headers   (std::string &response, http_response &res, char *delimiter);
        void                    tcp_stringify_http_info (std::string &response, http_response &res, char *delimiter) const;

        // I/O
        ssize_t                 socket_read             (char *buff, const size_t &buff_size) const;
        ssize_t                 socket_write            (const char *buff, const size_t &buff_size) const;

        ssize_t                 openssl_read            (char *buff, const size_t &buff_size) const;
        ssize_t                 openssl_write           (const char *buff, const size_t &buff_size) const;

        static void             quic_generate_output_packages (QUIC_MAP_CONNS_T *quic_map_conns, class site *site);
        static void             udp_loop_event (QUIC_MAP_CONNS_T *quic_map_conns, class site *site, class config *config, const std::string &scid, const sockaddr &client, const socklen_t &client_len);

        MANAPI_HTTP_READ_INTERFACE          mask_read;
        MANAPI_HTTP_WRITE_INTERFACE         mask_write;
        MANAPI_HTTP_WRITE_FILE_INTERFACE    mask_write_file;

        std::function<ssize_t(http_response &res)>                          mask_response;


        void                    *buff;
        ssize_t                 buff_size;
        size_t                  buff_type = MANAPI_HTTP_BUFF_BINARY;

        SSL                     *ssl = nullptr;

        struct http_quic_conn_io*conn_io = nullptr;
        int64_t                 stream_id = -1;
    private:
        void                    tcp_doit ();
        void                    udp_doit ();

        bool                    socket_wait_select () const;
        // QUIC



        // TCP
        void                    tcp_parse_request_response (char *response, const size_t &size, size_t &i);
        ssize_t                 tcp_send_response (http_response &res);

        std::string             compress_file (const std::string &file, const std::string &folder, const std::string &compress, manapi::utils::compress::TEMPLATE_INTERFACE compressor) const;

        void                    send_text (const std::string &text, const size_t &size) const;
        void                    send_file (http_response &res, std::ifstream &f, ssize_t size, std::vector<utils::replace_founded_item> &replacers) const;
        void                    send_file (http_response &res, std::ifstream &f, ssize_t size) const;

        void                    handle_request (const http_handler_page *data, const size_t &status = 200, const std::string &message = HTTP_STATUS.OK_200);
        static void             execute_custom_handler (const http_handler_page *handler, http_request &req, http_response &resp);
        void                    send_error_response (const size_t &status, const std::string &message, const http_handler_page *error);

        static void             parse_uri_path_dynamic (request_data_t &request_data, char *response, const size_t &size, size_t &i, const std::function <void()>& finished = nullptr);

        class site              *site;
        class config            *config;

        quiche_config           *quic_config       = nullptr;

        sockaddr                client{};
        socklen_t               client_len;

        int                     conn_fd{};

        utils::manapi_socket_information
                                socket_information;

        request_data_t          request_data{};

        size_t                  conn_type;

        // masks

        std::timed_mutex        quic_m_read;
        std::timed_mutex        quic_m_write;
        std::timed_mutex        quic_m_pre_load;

        bool                    quic_v_read     = false;
        bool                    quic_v_write    = false;
        bool                    quic_v_body     = false;
        bool                    quic_v_worker   = false;

        // cv, m in main loop

        std::timed_mutex        quic_m_worker;

        ev::io                  *ev_io;
        bool                    is_deleting     = false;

        file_transfer_information fti;

    };
}

#endif //MANAPIHTTP_MANAPITASKHTTP_H
