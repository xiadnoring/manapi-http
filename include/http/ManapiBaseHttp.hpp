#pragma once

#include <set>

#include "../ManapiUtils.hpp"
#include "./ManapiHttpTypes.hpp"
#include "./ManapiHttpUtils.hpp"
#include "./ManapiHttpConfig.hpp"
#include "../worker/ManapiSite.hpp"
#include "../fs/ManapiFileStream.hpp"
#include "../worker/ManapiBaseWorker.hpp"
#include "../std/ManapiBuffer.hpp"

namespace manapi::net::http {
    struct http_handler_page;
    class server;
}

namespace manapi::net::http::internal {
    typedef vbefore_delete<bool, false> cont_callback_cb_t;
    typedef std::unique_ptr<vbefore_delete<bool, false>> cont_callback_t;

    struct handle_data_t {
        reference <worker::connection> conn;
        worker::shared_worker worker;
        request_data_t * req_data;
        cont_callback_t cb;
        std::unique_ptr<http::http_handler_page> router;

        ~handle_data_t();
    };

    enum file_fd_flags {
        FILE_FD_FLAG_META = 1<<0,
        FILE_FD_FLAG_CUSTOM = 1<<1
    };

    struct file_fd_t {
        int flags;
        manapi::ev::unique_file fd;
        std::string fpath;
        std::size_t fsize;
        std::chrono::time_point<std::chrono::system_clock> flastwrite;
    };

    typedef std::unique_ptr<handle_data_t> uq_handle_data_t;

    std::string generate_default_page (int status, std::string_view msg);

    void send_response (std::unique_ptr<response> res);

    future<void> send_response_file (std::unique_ptr<response> res, response_features_t features);

    future<void> send_response_text (std::unique_ptr<response> res, response_features_t features);

    future<void> send_response_proxy (std::unique_ptr<response> res, response_features_t features);

    future<void> send_response_formdata (std::unique_ptr<response> res, response_features_t features);

    future<void> send_response_slice (std::unique_ptr<response> res, response_features_t features);

    void send_response_sync_cb (std::unique_ptr<response> res, response_features_t features);

    void send_response_stream_cb (std::unique_ptr<response> res, response_features_t features);

    void send_response_async_cb (std::unique_ptr<response> res, response_features_t features);

    future<int> mask_response (response *res, bool finish);

    void handle_income_request (uq_handle_data_t cdata,int status);

    //future<void> send_file(uq_handle_data_t cdata, filesystem::fstream f, ssize_t size, std::vector<replace_founded_item> replacers);

    future<void> send_file(std::unique_ptr<response> res, std::shared_ptr<fs::fstream> f, std::size_t size);

    future<void> send_text(std::unique_ptr<response> res, std::string text);

    future<void> send_slice(std::unique_ptr<response> res, manapi::slice sv);

    void expect_header (uq_handle_data_t cdata);
}
