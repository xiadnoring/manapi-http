#include "ManapiFilesystem.hpp"
#include "ManapiSite.hpp"

#include "encoding/ManapiUnicode.hpp"
#include "worker/ManapiBaseWorker.hpp"
#include "worker/ManapiTcp.hpp"
#include "worker/ManapiOpenSslOverTcp.hpp"
#include "include/ManapiUtils.hpp"

#include "services/ManapiTaskFunction.hpp"
#include "services/ManapiThreadPool.hpp"
#include "worker/ManapiHttp3Cloudflare.hpp"
#include "worker/ManapiWolfSslOverTcp.hpp"
#include "include/ManapiSiteInternal.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"
#include "async/ManapiEasyCancellation.hpp"
#include "include/worker/ManapiNgHttp2Interface.hpp"
#include "worker/ManapiHttp1Interface.hpp"
#include "worker/ManapiHttp2Interface.hpp"

namespace manapi::net {
    namespace worker {
        struct wrk_http2_ctx_global_t;
    }

    // default, +error, +layout in url
    enum uri_page_type {
        URI_PAGE_DEFAULT    = 0,
        URI_PAGE_ERROR      = 1,
        URI_PAGE_LAYER      = 2
    };
}

manapi::net::http::http_handler_function manapi::net::http::site::default_error_handler
    = {
    .handler = [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        co_return resp.text(http::internal::generate_default_page(resp.status_code(), resp.status_message()));
    },
    .post_mask = nullptr,
    .get_mask = nullptr,
};

std::string_view manapi::net::http::site::default_config_name      = "config_.json";

// ======================[ configs funcs]==========================

void manapi::net::http::site::compressor_for_file(const std::string &name, compress_file_cb_t handler) {
    (*this->data->compressors_for_file)[name] = std::move(handler);
}

void manapi::net::http::site::compressor_for_string(const std::string &name, compress_str_cb_t handler) {
    (*this->data->compressors_for_string)[name] = std::move(handler);
}

manapi::net::http::site::compress_file_cb_t & manapi::net::http::site::compressor_for_file(const std::string &name) {
    auto it = this->data->compressors_for_file->find(name);
    if (it == this->data->compressors_for_file->end()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_DATA_LOSS, "The compressor {} doesn't exists", name);
    }

    return it->second;
}

bool manapi::net::http::site::contains_compressor_for_file(const std::string &name) const {
    return this->data->compressors_for_file->contains(name);
}

manapi::net::http::site::compress_str_cb_t & manapi::net::http::site::compressor_for_string(const std::string &name) {
    auto it = this->data->compressors_for_string->find(name);
    if (it == this->data->compressors_for_string->end()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_DATA_LOSS, "The compress {} doesn't exists", name);
    }
    return it->second;
}

bool manapi::net::http::site::contains_compressor_for_string(const std::string &name) const {
    return this->data->compressors_for_string->contains(name);
}

void manapi::net::http::site::transport_protocol_worker(const std::string &type, const std::string &name, implement_create_cb worker) {
    (*this->data->transport_protocol_workers)[type][name] = std::move(worker);
}

const std::map<std::string, manapi::net::http::site::implement_create_cb> &manapi::net::http::site::transport_protocol_worker(const std::string &type) {
    return (*this->data->transport_protocol_workers)[type];
}

void manapi::net::http::site::http_protocol_worker(http::versions::http type, const std::string &name, implemenet_http_cb worker) {
    (*this->data->http_protocol_workers)[type][name] = std::move(worker);
}

const std::map<std::string, manapi::net::http::site::implemenet_http_cb> & manapi::net::http::site::http_protocol_worker(http::versions::http type) {
    return (*this->data->http_protocol_workers)[type];
}

const std::string & manapi::net::http::site::config_cache_dir() {
    return this->data->config_->at("cache_path").as_string();
}

void manapi::net::http::site::on_config_update(std::shared_ptr<data_t> data, const manapi::json &n) {
    if (n.is_null())
        return;

    auto &site = data->config_->at("site");
    auto &cache = data->config_->at("cache");
    auto &site_time =data->config_->at("site_time");
    auto &cache_time = data->config_->at("cache_time");

    if (site_time != n["site_time"]) {
        site = n["site"];
    }

    if (cache_time != n["cache_time"]) {
        cache = n["cache"];

    }

    data->config_ = std::make_shared<json>();
    *data->config_ = n;
    assert(*data->config_ == n);
}

manapi::error::status_or<std::unique_ptr<manapi::net::worker::wrk_interface_global_t>> create_http_protocol_worker (manapi::net::worker::base *w, manapi::error::status (*init_global_cb)(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w)) {
    auto p = std::make_unique<manapi::net::worker::wrk_interface_global_t>();
    auto res = init_global_cb (p.get(), w);
    if (!res.ok())
        return std::move(res);
    return std::move(p);
}

void manapi::net::http::site::setup() {
#if MANAPIHTTP_ZLIB_DEPENDENCY
    this->compressor_for_file("deflate", +[] (std::string src, std::string dest)
        -> future<error::status> { return manapi::compress::deflate_compress_file( std::move(src), std::move(dest)); });
    this->compressor_for_file("gzip", +[] (std::string src, std::string dest)
        -> future<error::status> { return manapi::compress::gzip_compress_file(std::move(src), std::move(dest)); });

    this->compressor_for_string("deflate", +[] (std::string_view data)
        -> error::status_or<std::string> { return compress::deflate_compress_string(data); });
    this->compressor_for_string("gzip", +[] (std::string_view data)
        -> error::status_or<std::string> { return compress::gzip_compress_string(data); });
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    this->compressor_for_file("br", +[] (std::string src, std::string dest)
        -> future<error::status> { return manapi::compress::brotli_compress_file(std::move(src), std::move(dest), 11, 22, 0); });
    this->compressor_for_string("br", +[] (std::string_view data)
        -> error::status_or<std::string> { return compress::brotli_compress_string(data, 11, 22, 0); });
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    this->compressor_for_file("zstd", +[] (std::string src, std::string dest)
        -> future<error::status> { return manapi::compress::zstd_compress_file(std::move(src), std::move(dest), 1); });
    this->compressor_for_string("zstd", +[] (std::string_view data)
        -> error::status_or<std::string> { return compress::zstd_compress_string(data, 1); });
#endif

    this->transport_protocol_worker("tcp", "default", worker::TCP::create);
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    this->transport_protocol_worker("tls", "openssl", worker::OpenSSL_TLS::create);
# ifdef MANAPI_OPENSSL_QUIC_REALIZATION
    this->transport_protocol_worker("quic", "openssl", worker::openssl_quic::create);
# endif
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY
    this->transport_protocol_worker("tls", "wolfssl", worker::WolfSSL_TLS::create);
#endif

#if MANAPIHTTP_QUICHE_DEPENDENCY
    this->transport_protocol_worker("quic", "quiche", worker::http_v3_cloudflare_quiche::create);
#endif

#ifdef MANAPIHTTP_DEFAULT_QUIC
    this->transport_protocol_worker("quic", "default", worker::quic::create);
#endif

    this->http_protocol_worker(http::versions::HTTP_v1_1, "default", [] (worker::base *w)
        { return create_http_protocol_worker (w, worker::default_wrk_http1_global_init); });
    this->http_protocol_worker(http::versions::HTTP_v2, "default", [] (worker::base *w)
        { return create_http_protocol_worker (w, worker::default_wrk_http2_global_init); });

#ifdef MANAPIHTTP_NGHTTP2_DEPENDENCY
    this->http_protocol_worker(http::versions::HTTP_v2, "nghttp", [] (worker::base *w)
        { return create_http_protocol_worker (w, worker::ng_wrk_http2_global_init); });
#endif

}

manapi::future<manapi::error::status> manapi::net::http::site::config(std::string path) {
    try {
        this->data->server_config = co_await this->data->sctx.storage().subscribe([data = this->data] (auto &&f1)
            -> void { on_config_update(data, std::forward<decltype(f1)>(f1)); });

        co_await this->data->sctx.storage().edit_async (this->data->server_config,
            [this, path = std::move(path)] (manapi::json &config) mutable -> manapi::future<bool> {
                if (!config.is_object())
                    config = manapi::json::object();

                if (!config.contains("site") || !config["site"].is_object()) {

                    try {
                        auto exists = co_await manapi::filesystem::async_exists(path);
                        if (!exists.ok() || !exists.unwrap())
                        {
                            std::string data = manapi::json::object().dump(4);
                            auto res = co_await manapi::filesystem::async_write(path, std::move(data), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                            res.unwrap();
                        }

                        auto res = co_await manapi::filesystem::async_read (path);
                        auto obj = manapi::json(res.unwrap(), true);

                        if (!obj.is_object())
                            obj = manapi::json::object();

                        config["site"] = std::move(obj);
                    }
                    catch (std::exception const &e) {
                        MANAPIHTTP_LOG("server router: config read failed due to {}", e.what());
                    }

                    config["site_time"] = 0;
                    config["site_path"] = std::move(path);
                    config["cache_time"] = 0;

                    co_await this->setup_config(config);
                    *this->data->config_ = config;
                    co_return true;
                }
                *this->data->config_ = config;
                co_return false;
        });
        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "config() failed", e.what());
    }
    co_return error::status_internal("config() failed");
}

manapi::future<manapi::error::status> manapi::net::http::site::config_object(json config) {
    try {
        this->data->server_config = co_await this->data->sctx.storage().subscribe([data = this->data] (auto &&f1)
            -> void { on_config_update(data, std::forward<decltype(f1)>(f1)); });

        co_await this->data->sctx.storage().edit_async (this->data->server_config,
            [this, nconfig = std::move(config)] (manapi::json &config) mutable -> manapi::future<bool> {
                if (!config.is_object())
                    config = manapi::json::object();

                if (!config.contains("site") || !config["site"].is_object()) {

                    try {
                        if (!nconfig.is_object())
                            nconfig = manapi::json::object();

                        config["site"] = std::move(nconfig);
                    }
                    catch (std::exception const &e) {
                        MANAPIHTTP_LOG("server router: config read failed due to {}", e.what());
                    }
                }

                config["site_time"] = 0;

                co_await this->setup_config(config);
                co_return true;
        });
        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "config() failed", e.what());
    }
    co_return error::status_internal("config() failed");
}

manapi::future<> manapi::net::http::site::setup_config(manapi::json &n) {
    if (!n.contains("cache") || !n.is_object())
        n["cache"] = manapi::json::object();

    auto &cache = n["cache"];
    auto &csite = n["site"];

    auto it = n.find("cache_path");
    std::string cache_path;

    try {
        if (it != n.end<json::OBJECT>())
            cache_path = it->second.as_string();
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("cache_path invalid: {}", e.what());
    }

    try {
        if (cache_path.empty())
            cache_path = manapi::filesystem::path::join(std::filesystem::temp_directory_path().string(), MANAPIHTTP_NAME, "cache");

        {
            auto mkdir_res = co_await manapi::filesystem::async_mkdir(cache_path, ev::IRUSR|ev::IWUSR|ev::IRGRP|ev::IXUSR|ev::IXGRP, true);
            mkdir_res.unwrap();
        }

        manapi::filesystem::path::append_delimiter(cache_path);
        auto path = manapi::filesystem::path::join(cache_path, std::string{site::default_config_name});
        try {
            auto exists = co_await manapi::filesystem::async_exists(path);
            if (!exists.ok() || exists.unwrap()) {
                auto res = co_await manapi::filesystem::async_read(path);
                cache = manapi::json(res.unwrap(), true);
            }
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "cached data couldn't be loaded from the config due to {}", e.what());
        }
    }
    catch (manapi::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service, e.err_num(), "The configuration directory ({}) couldn't be created due to {}.",
         cache_path, e.what());
    }

    n["cache_path"] = std::move(cache_path);
}

manapi::future<manapi::error::status_or<std::string>> manapi::net::http::site::get_compressed_cache_file(std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    try {
        while (true) {

            auto *cache = &this->data->config_->at("cache");
            if (!cache->contains(algorithm))
                break;


            auto *files = &cache->at(algorithm);

            auto it = files->find(file);
            if (it == files->end<json::OBJECT>())
                break;


            auto file_info = &it->second;

            if (!file_info->is_object()) {
                if (*file_info == false)
                    co_return manapi::error::status_unavailable("compress:Busy");

                break;
            }

            std::string const &lastWrite = file_info->at("last-write").as_string();
            if (lastWrite == std::format("{:%Y-%m-%d-%H-%M-%S}", filetime)) {
                std::string const &compressed = file_info->at("compressed").as_string();
                co_return compressed;
            }

            // last-writes aren't match

            break;
        }

        co_return error::status_not_found("compress:Cache file not found");
    }
    catch (std::exception const &e) {
        manapi_log_error("compress:Get compressed file failed due to %s", e.what());
    }
    co_return error::status_internal("compress:Get compressed failed");
}

manapi::future<manapi::error::status> manapi::net::http::site::set_compressed_cache_file(std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    try {
        co_await this->data->sctx.storage().edit (this->data->server_config,
            [&] (manapi::json &n) -> bool {
                auto *cache = &n["cache"];

                std::string fts = std::format("{:%Y-%m-%d-%H-%M-%S}", filetime);
                std::string del;

                try {
                    if (cache->contains(algorithm)) {
                        auto &alghs = cache->at(algorithm);
                        auto fileit = alghs.as_object().find(file);
                        if (fileit != alghs.as_object().end()
                            && fileit->second.is_object()) {
                            auto const compressedit = fileit->second.as_object().find("compressed");
                            if (compressedit != fileit->second.as_object().end()) {
                                del = compressedit->second.as_string();
                            }
                        }
                    }
                    else {
                        cache->insert(algorithm, manapi::json::object());
                    }

                    manapi::json file_info = manapi::json::object();

                    file_info.insert("last-write", std::move(fts));
                    file_info.insert("compressed", std::move(compressed));

                    cache->at(algorithm)[file] = std::move(file_info);
                }
                catch (std::exception const &e) {
                    MANAPIHTTP_LOG("Fatal error in caching: {}", e.what());
                    cache->at(algorithm).erase(file);
                }

                if (!del.empty()) {
                    manapi::async::run<manapi::sys_error::status>(manapi::filesystem::async_unlink(std::move(del),
                        manapi::async::timeout_cancellation(5000)), [] (std::exception_ptr err, manapi::sys_error::status *s) -> void {
                            if (err) {
                                /* ignore :) */
                                return;
                            }
                            if (!s->ok()) {
                                s->log();
                            }
                    });
                }

                return true;
        });

        co_return manapi::error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("set compressed failed due to %s", e.what());
    }

    co_return manapi::error::status_internal("compress:Set cache file failed");
}

manapi::future<manapi::error::status> manapi::net::http::site::set_locked_cache_file(std::string file, bool lock, std::string algorithm) {
    try {
        bool busy = false;
        co_await this->data->sctx.storage().edit(this->data->server_config, [&] (manapi::json &all) -> bool {
            auto &n = all["cache"];
            auto it = n.find(algorithm);
            if (it == n.end<json::OBJECT>()) {
                auto const res = n.insert({algorithm, json::object()});
                it = res.first;
            }
            auto fit = it->second.find(file);
            if (lock) {
                if (fit == it->second.end<json::OBJECT>()) {
                    it->second.insert({file, false});
                }
                else {
                    if (fit->second.is_object()) {
                        auto compressit = fit->second.find("compressed");
                        if (compressit != fit->second.end<json::OBJECT>() && compressit->second.is_string()) {
                            manapi::async::run<manapi::sys_error::status>(manapi::filesystem::async_unlink(compressit->second.as_string(),
                                manapi::async::timeout_cancellation(5000)), [] (std::exception_ptr err, manapi::sys_error::status *s) -> void {
                                    if (err) {
                                        /* ignore :) */
                                        return;
                                    }
                                    if (!s->ok())
                                        s->log();
                            });
                        }
                    }
                    else {
                        if (fit->second == false) {
                            busy = true;
                            return false;
                        }
                    }

                    fit->second = false;
                }
            }
            else {
                it->second.erase(fit);
            }
            return true;
        });

        if (busy)
            co_return error::status_unavailable("compress:Busy");

        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("set locked compressed failed due to %s", e.what());
    }

    co_return error::status_internal("compress:Set cache file failed");
}

manapi::future<> manapi::net::http::site::save_config(std::shared_ptr<data_t> data) {
    auto &config = *data->config_;
    co_await manapi::filesystem::async_write(config["site_path"].as_string(),
            config["site"].dump(4), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
}

void manapi::net::http::site::check_exists_method_on_url(const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "The method {} already contains in the url {}", method, url); }
}

void manapi::net::http::site::check_exists_method_on_url(const std::string &url,
    const std::unique_ptr<handlers_static_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "The method {} already contains in the static url {}", method, url); }
}

std::unique_ptr<manapi::net::http::http_handler_page> manapi::net::http::site::handler(http::request_data_t *request_data) const {
    auto handler_page = std::make_unique<http_handler_page>();
    handler_page->error = std::make_unique<http_handler_page>();
    handler_page->error->handler = &site::default_error_handler;

    // how much we will take the layers from handler_page.layers at the start to the handler_page.error.layer
    size_t error_layer_depth = 0;
    bool not_found = false;
    try
    {
        const http_uri_part *cur = &this->data->handlers;
        const size_t path_size = request_data->divided == -1 ? request_data->path.size() : request_data->divided;
        for (size_t i = 0; i <= path_size; i++) {
            if (cur->statics)
            {
                auto static_it = cur->statics->find(request_data->method);
                if (static_it != cur->statics->end()) {
                    handler_page->statics = &static_it->second;
                    handler_page->statics_parts_len = i;
                }
            }


            if (cur->layers)
            {
                auto shared_it = cur->layers->find(request_data->method);
                if (shared_it != cur->layers->end()) {
                    handler_page->layer.push_back(&shared_it->second);
                }
            }

            if (cur->errors)
            {
                auto error_it = cur->errors->find(request_data->method);
                if (error_it != cur->errors->end()) {
                    // find errors handlers for method!
                    handler_page->error->handler = &error_it->second;
                    error_layer_depth = handler_page->layer.size();
                }
            }

            if (i == path_size) {
                break;
            }

            if (cur->map) {
                auto const hmap = cur->map->find(request_data->path[i]);

                if (hmap != cur->map->end()) {
                    cur = hmap->second.get();

                    continue;
                }
            }

            if (cur->regexes) {
                std::smatch match;
                bool find = false;

                for (const auto &regex: *cur->regexes) {
                    // regex.first  <- regex string
                    // regex.second <- pair <regex, value (maybe next or handler)>

                    if (std::regex_match(request_data->path.at(i), match, regex.second.first)) {
                        cur     = regex.second.second.get();
                        find    = true;

                        if (cur->params == nullptr) {
                            // bug

                            MANAPIHTTP_LOG2("cur->regexes_title (params) is null.");
                            return handler_page;
                        }

                        const size_t expected_size = match.size() - 1;
                        if (cur->params->size() != expected_size) {
                            // bug

                            MANAPIHTTP_LOG("The expected number of parameters ({}) does not correspond of reality ({}). uri part: {}.",
                                       cur->params->size(), expected_size, request_data->path.at(i));
                            return handler_page;
                        }

                        // get params
                        for (size_t z = 0; z < cur->params->size(); z++)
                        { request_data->params.insert({cur->params->at(z), match.str(z + 1)}); }


                        break;
                    }
                }

                if (find) {
                    continue;
                }
            }

            not_found = true;

            break;
        }

        std::copy_n(handler_page->layer.begin(), error_layer_depth, std::back_inserter(handler_page->error->layer));

        // handler page

        if (not_found || cur->handlers == nullptr) {
            return handler_page;
        }

        auto it = cur->handlers->find (request_data->method);

        http_handler_function *handler{nullptr};

        if (it != cur->handlers->end()) {
            handler = &it->second;
        }

        if (handler == nullptr) {
            // TODO: handler error
        }

        handler_page->handler = handler;
        return std::move(handler_page);
    }
    catch (const std::exception &e) {
        MANAPIHTTP_LOG("routing: an error has occurred: {}", e.what());
    }
    return std::move(handler_page);
}

manapi::net::http::site::site(server_ctx sctx) {
    this->data = std::make_shared<data_t>(nullptr,
        std::make_shared<manapi::json>(manapi::json::object()), std::move(sctx), http_uri_part{nullptr, nullptr, nullptr, nullptr, nullptr,nullptr,nullptr});

    this->data->compressors_for_file = std::make_unique<decltype(this->data->compressors_for_file)::element_type>();
    this->data->compressors_for_string = std::make_unique<decltype(this->data->compressors_for_string)::element_type>();
    this->data->transport_protocol_workers = std::make_unique<decltype(this->data->transport_protocol_workers)::element_type>();
    this->data->http_protocol_workers = std::make_unique<decltype(this->data->http_protocol_workers)::element_type>();
}

manapi::net::http::site::~site() = default;

manapi::net::http::site::site(site &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::net::http::site & manapi::net::http::site::operator=(site &&n) noexcept {
    this->data = std::move(n.data);
    return *this;
}

manapi::net::http::site::site(const site &n) {
    this->data = n.data;
}

manapi::net::http::site & manapi::net::http::site::operator=(const site &n) = default;


manapi::net::http::http_uri_part *manapi::net::http::site::handler(std::string method, std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    size_t  type            = URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    http_handler_function functions;

    if (get_mask.is_enabled())
    {
        functions.get_mask = std::make_unique<json_mask> (std::move(get_mask));
    }

    if (post_mask.is_enabled())
    {
        functions.post_mask = std::make_unique<json_mask> (std::move(post_mask));
    }

    functions.handler = std::move(handler);

    switch (type) {
        case URI_PAGE_DEFAULT:
            if (cur->handlers == nullptr)
            {
                cur->handlers = std::make_unique<handlers_types_t> ();
            }
            check_exists_method_on_url(uri, cur->handlers, method);
            cur->handlers->insert({std::move(method), std::move(functions)});

            break;
        case URI_PAGE_ERROR:
            if (cur->errors == nullptr) {
                cur->errors = std::make_unique<handlers_types_t>();
            }
            check_exists_method_on_url(uri, cur->errors, method);
            cur->errors->insert({std::move(method), std::move(functions)});

            break;
        case URI_PAGE_LAYER:
            if (cur->layers == nullptr) {
                cur->layers = std::make_unique<handlers_types_t> ();
            }
            check_exists_method_on_url(uri, cur->layers, method);
            cur->layers->insert({std::move(method), std::move(functions)});

            break;
        default:
            return nullptr;
    }


    return cur;
}

manapi::net::http::http_uri_part *manapi::net::http::site::handler(std::string method, std::string uri, std::string folder, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    size_t  type            = URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    switch (type) {
        case URI_PAGE_DEFAULT: {
            if (cur->statics == nullptr) {
                cur->statics = std::make_unique<handlers_static_types_t> ();
            }

            check_exists_method_on_url(uri, cur->statics, method);
            auto it = cur->statics->insert({std::move(method), {std::move(folder), nullptr}});
            if (it.second) {
                if (handler) {
                    auto &layer = it.first->second.layer;
                    layer = std::make_unique<http_handler_function>(
                        std::move(handler), nullptr, nullptr);

                    if (get_mask.is_enabled()) {
                        layer->get_mask = std::make_unique<decltype(get_mask)>(std::move(get_mask));
                    }

                    if (post_mask.is_enabled()) {
                        layer->post_mask = std::make_unique<decltype(post_mask)>(std::move(post_mask));
                    }
                }
            }


            break;
        }
        default:
            THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "{}", "can not use the special pages with the static files");
    }


    return cur;
}

manapi::net::http::http_uri_part *manapi::net::http::site::build_uri_part(const std::string &uri, size_t &type)
{
    std::string                 buff;
    std::unique_ptr<handlers_regex_titles_t> regexes_title = nullptr;

    http_uri_part *cur      = &this->data->handlers;

    bool    is_regex        = false;

    // past params lists. To check for a match
    std::vector <std::unique_ptr<std::vector <std::string>> *> past_params_lists;

    for (size_t i = 0; i <= uri.size(); i++)
    {
        const bool is_last_part = (i == uri.size());

        if (uri[i] == '/' || is_last_part)
        {
            if (!buff.empty())
            {

                if (is_regex)
                {
                    if (cur->regexes == nullptr || !cur->regexes->contains(buff))
                    {
                        if (cur->regexes == nullptr)
                        {
                            cur->regexes = std::make_unique<handlers_regex_map_t>();
                        }

                        auto new_part   = std::make_unique<http_uri_part>();
                        auto new_part_lnk = new_part.get();

                        std::regex p(buff);

                        if (regexes_title != nullptr)
                        {
                            new_part->params = std::move(regexes_title);

                            // check for a match
                            for (const auto &past_params: past_params_lists)
                            {
                                for (const auto &param: *new_part->params)
                                {
                                    if (std::find(past_params->get()->begin(), past_params->get()->end(), param) !=
                                        past_params->get()->end())
                                        MANAPIHTTP_LOG("Warning: a param with a title '{}' is already in use. ({})",
                                                   param, uri);
                                }
                            }

                            // save params list
                            past_params_lists.push_back(&new_part->params);
                            cur->regexes->insert({buff, std::make_pair(p, std::move(new_part))});
                        }
                        //clean up
                        regexes_title   = nullptr;

                        cur             = new_part_lnk;
                    }
                    else
                    {
                        cur             = cur->regexes->at(buff).second.get();
                    }

                    is_regex        = false;
                }
                else
                {

                    if (buff[0] == '+' && is_last_part)
                    {
                        if (buff == "+error") { type = URI_PAGE_ERROR; }
                        else if (buff == "+layer") { type = URI_PAGE_LAYER; }
                        else { MANAPIHTTP_LOG("The first char '{}' is reserved for special pages in {}", '+', buff); }

                        break;
                    }

                    if (cur->map == nullptr)
                    {
                        cur->map = std::make_unique<handlers_map_t>();
                    }

                    if (!cur->map->contains(buff))
                    {
                        auto new_part       = std::make_unique<http_uri_part>();
                        auto new_part_lnk   = new_part.get();
                        cur->map->insert({buff, std::move(new_part)});

                        cur                 = new_part_lnk;
                    }
                    else
                    {
                        cur                 = cur->map->at(buff).get();
                    }
                }


                // clean up
                buff    = "";
            }

            continue;
        }

        if (uri[i] == '[')
        {
            std::string     title;
            const size_t    temp = i;

            for (i++; i < uri.size(); i++)
            {
                if (uri[i] == ']')
                {
                    break;
                }

                if (uri[i] == '\\') {
                    title = "";
                    break;
                }

                title += uri[i];
            }

            if (!title.empty())
            {
                if (!is_regex)
                {
                    is_regex = true;

                    //buff = manapi::unicode::escape_string(buff);
                }

                // if null -> create
                if (regexes_title == nullptr) {
                    regexes_title = std::make_unique<handlers_regex_titles_t> ();
                }

                regexes_title->push_back(title);

                buff += "(.+)";
                continue;
            }

            i = temp;
        }

        if (is_regex) {
            if (uri[i] == '\\' || uri[i] == '"')
                buff.push_back('\\');

            buff += uri[i];
            continue;
        }

        buff += uri[i];
    }

    return cur;
}