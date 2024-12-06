#ifndef MANAPIHTTP_FORMDATA_HPP
#define MANAPIHTTP_FORMDATA_HPP
#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"

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
        formdata_recv (request_data_t &request_data, std::shared_ptr<http::config> config, http::base *http_task);
        ~formdata_recv ();
        [[nodiscard]] bool next_file () const;
        [[nodiscard]] bool next_param () const;

        [[nodiscard]] file_data_t about_file () const;
        void get_file(const std::function<void(const char *, const size_t &)> &handler);
        std::string get_file_to_str();
        void save_file (const std::string &filepath);

        [[nodiscard]] const std::string &about_param () const;
        std::pair <std::string, std::string> get_param ();
    private:
        enum data_type {
            DATA_NONE = 0,
            DATA_FILE = 1,
            DATA_PLAIN = 2
        };

        static void buff_to_extra_buff (const request_data_t &req_data, const size_t &start, const size_t &end, std::string &dest, size_t &size);
        void multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line = nullptr);
        void urlencoded_read_param (const std::function<void(const char *, const size_t &)> &send_line = nullptr);

        std::function<void(const std::function<void(const char *, const size_t &)> &)> current_read_param;

        request_data_t &request_data;
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
    };
}

#endif //MANAPIHTTP_FORMDATA_HPP
