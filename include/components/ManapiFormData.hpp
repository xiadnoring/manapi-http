#pragma once

#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../worker/ManapiBaseWorker.hpp"

namespace manapi::net {
    struct file_data_t {
        std::string file_name;
        std::string mime_type;
        std::string param_name;
    };


    class formdata_recv {
        struct formdata_recv_ctx_t;
    public:
        typedef std::move_only_function<manapi::future<ssize_t> (slice_view buffs, bool fin)> ondata_cb_t;

        typedef std::move_only_function<ondata_cb_t(std::string name)> onparam_cb_t;

        typedef std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)> req_data_cb_t;

        typedef manapi::future<> (*onrecv_cb_t)(worker::base *worker, worker::shared_conn *conn, http::request_data_t *req, req_data_cb_t handler);

        formdata_recv (onrecv_cb_t onrecv_cb, manapi::net::worker::base *worker, worker::shared_conn *conn, http::request_data_t *req);

        ~formdata_recv ();

        formdata_recv (formdata_recv &&n) noexcept;

        formdata_recv &operator=(formdata_recv &&n) noexcept;

        manapi::future<void> get (onparam_cb_t cb);

        static ondata_cb_t save_file (std::string file, int mode = ev::IRUSR|ev::IWUSR|ev::IRGRP|ev::IROTH, ssize_t maxlen = -1, manapi::async::cancellation_action cancellation = nullptr);

        static ondata_cb_t save_string (std::string *str, ssize_t maxlen = -1);

        static ondata_cb_t skip (ssize_t maxlen = -1);
    private:
        manapi::future<ssize_t> onrecv_multipart_ (slice_view buffs);
        manapi::future<ssize_t> onrecv_urlencoded_ (slice_view buffs);

        onparam_cb_t onparam_cb_;
        ondata_cb_t ondata_cb_;
        onrecv_cb_t onrecv_cb_;
        std::unique_ptr<formdata_recv_ctx_t> ctx_;
        worker::base *worker_;
        worker::shared_conn *conn_;
        http::request_data_t *req_;
    };

    class formdata_send {
    public:
        formdata_send (async::shared_ctx ctx);
        ~formdata_send ();

        formdata_send (formdata_send &&n) noexcept;
        formdata_send& operator= (formdata_send &&n) noexcept;

        void append_file (const std::string &name, std::string filepath);
        void append_file (const std::string &name, std::string filepath, std::string filename, std::string filemime);
        void append_text (const std::string &name, std::string data);

        void erase (const std::string &name);
        [[nodiscard]] bool contains (const std::string &name) const;

        [[nodiscard]] manapi::future<ssize_t> payload_size () const;
        [[nodiscard]] ssize_t multipart_size (ssize_t boundary_size) const;

        [[nodiscard]] std::string generate_boundary () const;

        manapi::future<> data2multipart (std::string boundary, ssize_t buffer_size,  std::function<manapi::future<void>(const void *buffer, ssize_t size)> write);
    private:
        struct data_file_storage {
            std::string filename;
            std::string filemime;
        };

        struct data_storage {
            int type;
            std::string data;
            std::optional<data_file_storage> file;
        };

        std::map<std::string, data_storage> data{};
        async::shared_ctx ctx;
    };
}
