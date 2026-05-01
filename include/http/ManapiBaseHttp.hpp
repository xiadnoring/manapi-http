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
        std::shared_ptr<worker::connection> conn;
        worker::shared_worker worker;
        request_data_t * req_data;
        cont_callback_t cb;
        std::unique_ptr<http::http_handler_page> router;
    };

    typedef std::unique_ptr<handle_data_t> uq_handle_data_t;

    std::string generate_default_page (int status, std::string_view msg);
    void send_response (std::unique_ptr<response> res);
    future<void> send_response_file (std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_text (std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_proxy (std::unique_ptr<response> res, response_features_t features);
    future<void> send_response_formdata (std::unique_ptr<response> res, response_features_t features);
    void send_response_sync_cb (std::unique_ptr<response> res, response_features_t features);
    void send_response_stream_cb (std::unique_ptr<response> res, response_features_t features);
    void send_response_async_cb (std::unique_ptr<response> res, response_features_t features);
    future<int> mask_response (response *res, bool finish);
    void handle_income_request (uq_handle_data_t cdata,int status);
    void send_error_response (uq_handle_data_t cdata, int status = http::INTERNAL_SERVER_ERROR_500);
    //future<void> send_file(uq_handle_data_t cdata, filesystem::fstream f, ssize_t size, std::vector<replace_founded_item> replacers);
    future<void> send_file(std::unique_ptr<response> res, fs::fstream f, ssize_t size);
    future<void> send_text(std::unique_ptr<response> res, std::string text);
    void expect_header (uq_handle_data_t cdata);

    /**
     * Compress file (or get already compressed in the cache)
     *
     * @param site Site Ctx
     * @param file File Path
     * @param folder Folder Path
     * @param compress Compress Algo
     * @param compressor Compressor Function
     * @param force_compress Force Compress Flag
     * @return
     *
     * errors:
     * - internal - something gets wrong
     * - filesystem - fs failed
     */

    future<manapi::status_or<std::string>> compress_file(std::shared_ptr<net::http::server> site, std::string file, std::string folder, std::string compress, response_features_t::compress_file_cb *compressor, bool force_compress = false);
}
