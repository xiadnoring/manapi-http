#pragma once

#include <set>

#include "../ManapiUtils.hpp"
#include "../ManapiHttpTypes.hpp"
#include "../worker/base_worker.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"

#include "./Utils.hpp"
#include "../async/ManapiAsyncFileStream.hpp"
#include "../components/ManapiBuffer.hpp"

namespace manapi::net::http::internal {
    typedef vbefore_delete<bool, false> cont_callback_cb_t;
    typedef std::unique_ptr<vbefore_delete<bool, false>> cont_callback_t;

    struct handle_data_t {
        std::shared_ptr<worker::connection> conn;
        worker::shared_worker worker;
        request_data_t * req_data;
        cont_callback_t cb;
        std::unique_ptr<http_handler_page> router;
    };

    typedef std::unique_ptr<handle_data_t> uq_handle_data_t;

    void send_response (uq_handle_data_t cdata, std::unique_ptr<response> res);
    future<void> send_response_file (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_text (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_proxy (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_formdata (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    void send_response_sync_cb (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    void send_response_stream_cb (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    void send_response_async_cb (uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features);
    future<ssize_t> mask_response (handle_data_t* cdata, response *res, bool finish);
    void handle_income_request (uq_handle_data_t cdata, int status);
    void send_error_response (uq_handle_data_t cdata, int status = http::INTERNAL_SERVER_ERROR_500);
    //future<void> send_file(uq_handle_data_t cdata, filesystem::fstream f, ssize_t size, std::vector<replace_founded_item> replacers);
    future<void> send_file(uq_handle_data_t cdata, filesystem::fstream f, ssize_t size);
    future<void> send_text(uq_handle_data_t cdata, std::string text);
    void expect_header (uq_handle_data_t cdata);
    future<std::string> compress_file(net::http::site site, std::string file, std::string folder, std::string compress, std::move_only_function<future<void>(std::string src, std::string dest)> *compressor, bool force_compress = false);
}
