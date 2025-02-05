#ifndef MANAPIHTTP_FORMDATA_HPP
#define MANAPIHTTP_FORMDATA_HPP

#include "../ManapiAsync.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiUtils.hpp"
#include "../http/Utils.hpp"

namespace manapi::net::http {
    class base;
}

namespace manapi::net {
    struct file_data_t {
        std::string file_name;
        std::string mime_type;
        std::string param_name;
    };

    class formdata_recv {
    public:
        formdata_recv (http::request_data_t &request_data, std::shared_ptr<http::config> config, http::base *http_task);
        ~formdata_recv ();

        formdata_recv (formdata_recv &&n) noexcept;
        formdata_recv &operator=(formdata_recv &&n) noexcept;

        future<void> _init ();

        [[nodiscard]] bool next_file () const;
        [[nodiscard]] bool next_param () const;

        [[nodiscard]] file_data_t about_file () const;
        future<void> get_file(const std::function<void(const char *, const size_t &)> &handler);
        future<std::string> get_file_to_str();
        future<void> save_file (const std::string &filepath);

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
        static void buff_to_extra_buff (const http::request_data_t &req_data, const size_t &start, const size_t &end, std::string &dest, size_t &size);
        future<void> multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line = nullptr);
        future<void> urlencoded_read_param (const std::function<void(const char *, const size_t &)> &send_line = nullptr);

        std::function<future<void>(const std::function<void(const char *, const size_t &)> &)> current_read_param;

        http::request_data_t *request_data;
        // boundary --XXXXXxxxXXX for form data
        std::string body_boundary;
        std::shared_ptr<http::config> config;
        std::string buff_extra;
        http::base *http_task;
        // the data of the next file
        file_data_t file_data;
        std::pair <std::string, std::string> param_data;

        bool first_line = true;
        data_type type = DATA_NONE;
        content_type content_type_form = CONTENT_TYPE_NONE;
    };
}

#endif //MANAPIHTTP_FORMDATA_HPP
