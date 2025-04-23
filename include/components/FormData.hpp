#pragma once

#include "Buffer.hpp"
#include "ObjectPool.hpp"
#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncParallelRun.hpp"

namespace manapi::net {
    struct file_data_t {
        std::string file_name;
        std::string mime_type;
        std::string param_name;
    };

    class formdata_recv {
    public:
        formdata_recv (std::shared_ptr<async::context> ctx, size_t buffer_size,
            ssize_t &body_buffer_size, char *buffer, ssize_t &body_max_size_left, ssize_t &body_index, std::function<future<ssize_t>(void *, ssize_t)> body_read);
        ~formdata_recv ();

        formdata_recv (formdata_recv &&n) noexcept;
        formdata_recv &operator=(formdata_recv &&n) noexcept;

        future<void> _init (bool has_body, const std::string &content_type);

        [[nodiscard]] bool next_file () const;
        [[nodiscard]] bool next_param () const;

        [[nodiscard]] file_data_t about_file () const;
        future<void> get_file(std::function<void(const char *, ssize_t)> handler);
        future<void> get_async_file(std::function<manapi::future<>(const char *, ssize_t)> handler);
        future<std::string> get_file_to_str();
        future<void> save_file (std::string filepath);

        [[nodiscard]] const std::string &about_param () const;
        future<std::pair <std::string, std::string>> get_param ();

        static std::string json2form (const json &obj);
    private:
        enum data_type {
            DATA_NONE = 0,
            DATA_FILE = 1,
            DATA_PLAIN = 2
        };

        enum content_type {
            CONTENT_TYPE_NONE = 0,
            CONTENT_TYPE_MULTIPART_FORM_DATA = 1,
            CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED = 2
        };

        void _move (formdata_recv &&n) noexcept;
        future<void> multipart_read_param (std::function<manapi::future<>(const char *, ssize_t )> send_line = nullptr);
        future<void> urlencoded_read_param (std::function<manapi::future<>(const char *, ssize_t )> send_line = nullptr);

        std::function<future<void>(std::function<manapi::future<>(const char *, ssize_t )> )> current_read_param;
        std::function<future<ssize_t>(void *, ssize_t)> body_read;

        // boundary --XXXXXxxxXXX for form data
        std::string body_boundary;
        std::string buff_extra;
        size_t buffer_size;

        // the data of the next file
        file_data_t file_data;
        std::pair <std::string, std::string> param_data;

        std::shared_ptr<async::context> ctx;

        bool first_line = true;

        data_type type = DATA_NONE;
        content_type content_type_form = CONTENT_TYPE_NONE;

        char *body_buffer;
        ssize_t *body_buffer_size;
        ssize_t *body_max_size_left;
        ssize_t *body_index;
    };

    class formdata_send {
    public:
        formdata_send (std::shared_ptr<async::context> ctx);
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
        enum data_type {
            DATA_NONE = 0,
            DATA_FILE = 1,
            DATA_PLAIN = 2
        };

        struct data_file_storage {
            std::string filename;
            std::string filemime;
        };

        struct data_storage {
            data_type type;
            std::string data;
            std::optional<data_file_storage> file;
        };

        std::map<std::string, data_storage> data{};
        std::shared_ptr<async::context> ctx;
    };
}
