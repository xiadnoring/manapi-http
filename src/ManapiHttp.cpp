#include <iostream>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>
#include <memory.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <regex>

#include "ManapiHttp.hpp"
#include "std/ManapiAsyncPromise.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "./include/ManapiHttpInternal.hpp"
#include "./include/ManapiUtils.hpp"
#include "ManapiThreadPool.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "worker/ManapiSite.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "worker/ManapiBaseWorker.hpp"
#include "worker/ManapiTcp.hpp"
#include "worker/ManapiOpenSslOverTcp.hpp"
#include "worker/ManapiHttp3Cloudflare.hpp"
#include "worker/ManapiWolfSslOverTcp.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "./include/ManapiUtils.hpp"
#include "./include/ManapiHttpInternal.hpp"
#include "./include/http/ManapiNgHttp2Interface.hpp"
#include "./include/http/ManapiNgHttp3Interface.hpp"
#include "./include/http/ManapiHttp1Interface.hpp"
#include "./include/http/ManapiHttp2Interface.hpp"
#include "./include/worker/ManapiQuicOpenSsl.hpp"

enum manapi_http_server_flags {
    MANAPI_HTTP_SERVER_FLAG_RUNNING = 1<<0,
    MANAPI_HTTP_SERVER_FLAG_STOPPING = 1<<1
};


// default, +error, +layout in url
enum uri_page_type {
    URI_PAGE_DEFAULT = 0,
    URI_PAGE_ERROR,
    URI_PAGE_LAYER,
    URI_PAGE_CUSTOM
};

enum handler_template_types {
    HANDLER_TEMPLATE_NONE_TYPE = 0,
    HANDLER_TEMPLATE_SYNC_CB_TYPE,
    HANDLER_TEMPLATE_ASYNC_CB_TYPE
};

std::shared_ptr<manapi::net::http::http_handler_function> http_default_error_handler = std::make_shared<manapi::net::http::http_handler_function>(
    [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        co_return resp.text(manapi::net::http::internal::generate_default_page(resp.status_code(), resp.status_message())).unwrap();
    }
);

static std::string_view http_default_config_name = "config.json";

manapi::net::http::server::~server() = default;

struct manapi::net::http::server::data_t {
    pools_t pools;
    std::size_t next_pool_id;
    std::shared_ptr<ev::async> init_watcher;
    uint8_t flags;
    std::shared_ptr<multithread_storage::worker_t> server_config;
    std::shared_ptr<manapi::json> config_;
    std::shared_ptr<server_ctx> sctx;
    std::size_t event_id;
    std::size_t clean_up_id;
    http_uri_part handlers{};
    std::unique_ptr<std::map <std::string, compress_file_cb_t, std::less<>>> compressors_for_file{};
    std::unique_ptr<std::map <std::string, compress_str_cb_t, std::less<>>> compressors_for_string{};
    std::unique_ptr<std::map <std::string, std::map <std::string, implement_create_cb>>> transport_protocol_workers{};
    std::unique_ptr<std::map <http::versions::http, std::map <std::string, implemenet_http_cb>>> http_protocol_workers{};
    std::mutex loopmx{};
};

static manapi::status_or<std::unique_ptr<manapi::net::worker::wrk_interface_global_t>> create_http_protocol_worker (manapi::net::worker::interface_worker *w, manapi::status (*init_global_cb)(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w)) {
    auto p = std::make_unique<manapi::net::worker::wrk_interface_global_t>();
    auto res = init_global_cb (p.get(), (w));
    if (!res.ok())
        return std::move(res);
    return std::move(p);
}


static void http_server_on_config_update(manapi::net::http::server::data_t *data, const manapi::json &n) MANAPIHTTP_NOEXCEPT {
    try {
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

        data->config_ = std::make_shared<manapi::json>();
        *data->config_ = n;

        assert(*data->config_ == n);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "on_config_update", e.what());
    }
}

static void http_server_clean_up(manapi::net::http::server::data_t *data) {
    // clean
    data->pools.clear();
    // reset
    data->next_pool_id = 0;
}

static manapi::future<> http_server_setup_config(manapi::json &n) {
    using namespace manapi;

    if (!n.contains("cache") || !n.is_object())
        n["cache"] = manapi::json::object();

    auto &cache = n["cache"];
    auto &csite = n["site"];

    auto it = n.find("cache_path");
    std::string cache_path;

    try {
        if (it != n.end<manapi::json::OBJECT>())
            cache_path = it->second.as_string();
    }
    catch (std::exception const &e) {
        manapi_log_trace("http:cache_path invalid: %s", e.what());
    }

    try {
        if (cache_path.empty())
            cache_path = manapi::fs::path::join(std::filesystem::temp_directory_path().string(), MANAPIHTTP_NAME, "cache");

        {
            auto mkdir_res = co_await manapi::fs::async_mkdir(cache_path, ev::IRUSR|ev::IWUSR|ev::IRGRP|ev::IXUSR|ev::IXGRP, true);
            mkdir_res.unwrap();
        }

        manapi::fs::path::append_delimiter(cache_path);
        auto path = manapi::fs::path::join(cache_path, std::string{http_default_config_name});
        try {
            auto exists = co_await manapi::fs::async_exists(path);
            if (!exists.ok() || exists.unwrap()) {
                auto res = co_await manapi::fs::async_read(path);
                cache = manapi::json(res.unwrap(), true);
            }
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->error(ERR_FAILED_PRECONDITION, "cached data couldn't be loaded from the config due to {}", e.what());
        }
    }
    catch (manapi::exception const &e) {
        manapi::async::current()->logger()->error(e.err_num(), "The configuration directory ({}) couldn't be created due to {}.",
         cache_path, e.what());
    }

    n["cache_path"] = std::move(cache_path);
}

static manapi::future<> http_server_save(std::shared_ptr<manapi::net::http::server> srv, manapi::net::http::server::data_t* data) {
    using namespace manapi;
    auto &config = *data->config_;
    auto path = config["site_path"].as_string();
    if (path.empty())
        co_return;
    co_await manapi::fs::async_write(std::move(path),
            config["site"].dump(4), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
}

static manapi::net::http::http_uri_part *http_server_build_uri_part(manapi::net::http::server::data_t *m_data, std::string_view uri, size_t &type) {
    using namespace manapi::net::http;
    using namespace manapi;

    std::string buff;
    std::unique_ptr<handlers_regex_titles_t> regexes_title = nullptr;

    http_uri_part *cur      = &m_data->handlers;

    bool    is_regex        = false;

    // past params lists. To check for a match
    std::vector <std::unique_ptr<std::vector <std::string>> *> past_params_lists;

    for (size_t i = 0; i <= uri.size(); i++)
    {
        const bool is_last_part = (i == uri.size());

        if (is_last_part || uri[i] == '/')
        {
            if (!buff.empty())
            {

                if (is_regex)
                {
                    if (!cur->regexes || !cur->regexes->contains(buff))
                    {
                        if (!cur->regexes)
                        {
                            cur->regexes = std::make_unique<handlers_regex_map_t>();
                        }

                        auto new_part   = std::make_unique<http_uri_part>();
                        auto new_part_lnk = new_part.get();

                        std::regex p(buff);

                        if (regexes_title)
                        {
                            new_part->params = std::move(regexes_title);

                            // check for a match
                            for (const auto &past_params: past_params_lists)
                            {
                                for (const auto &param: *new_part->params)
                                {
                                    if (std::find(past_params->get()->begin(), past_params->get()->end(), param) !=
                                        past_params->get()->end())
                                        manapi_log_error("warning: a param with a title %.*s is already in use. (%.*s)",
                                                   param.size(), param.data(), uri.size(), uri.data());
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
                        else if (buff == "+custom") { type = URI_PAGE_CUSTOM; }
                        else { manapi_log_error("first char %c is reserved for special pages in %.*s", '+', buff.size(), buff.data()); }

                        break;
                    }

                    if (!cur->map)
                    {
                        cur->map = std::make_unique<handlers_map_t>();
                    }

                    if (!cur->map->contains(buff))
                    {
                        auto new_part       = std::make_unique<http_uri_part>();
                        auto new_part_lnk   = new_part.get();
                        cur->map->insert({buff, std::move(new_part)});

                        cur = new_part_lnk;
                    }
                    else
                    {
                        cur = cur->map->at(buff).get();
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

static manapi::future<> http_server_init_pool(std::shared_ptr<manapi::net::http::server> srv, manapi::net::http::server::data_t *m_data) {
    using namespace manapi::net::http;
    using namespace manapi::net;
    using namespace manapi;

    auto &pool = m_data->pools[std::this_thread::get_id()];
    // init all pools
    if (m_data->config_->at("site").contains("pools"))
    {
        auto bb = m_data->config_;
        auto &pools = bb->at("site")["pools"];

        {
            auto &pools_data = m_data->server_config->as<server_ctx::worker_data_t>()->pools;
            while (pools_data.size() < pools.size()) {
                pools_data.push_back(server_ctx::pool_t({},
                    std::make_unique<std::mutex>()));
            }
        }

        for (auto it = pools.begin<json::ARRAY>(); it != pools.end<json::ARRAY>(); ++it, m_data->next_pool_id++)
        {
            std::unique_ptr<http_pool> p;

            try {
                p = std::make_unique<http_pool> (*it, m_data->server_config, srv, m_data->next_pool_id);
                auto res = co_await p->run();
                if (res.ok())
                    pool.insert({m_data->next_pool_id, std::move(p)});
                else
                    res.log();
            }
            catch (std::exception const &e) {
                manapi_log_error("http:init pool failed due to %s", e.what());
            }

            if (p) {
                auto err = co_await p->stop();
                err.log();
            }
        }
    }
}

static manapi::status http_server_pool(std::shared_ptr<manapi::net::http::server> srv, manapi::net::http::server::data_t *m_data, std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto wres = manapi::async::current()->eventloop()->create_watcher_async(
            [srv, m_data, cb = std::move(cb)] (const std::shared_ptr<manapi::ev::async> &w) mutable
            -> void {
            cb();

            auto wz = std::move(m_data->init_watcher);
            manapi::async::current()->eventloop()->stop_watcher(std::move(wz));
        });

        if (!wres)
            return manapi::status{wres.code(), wres.status_msg()};

        m_data->init_watcher = wres.unwrap();

        if (auto rhs = m_data->init_watcher->send())
            manapi_log_error("%s due to %s", "http:Failed", manapi::ev::strerror(rhs));

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:Failed", e.what());
        return manapi::status_internal("http:Failed");
    }
}


manapi::net::http::server::server(std::shared_ptr<server_ctx> sctx) {
    this->m_data = std::make_unique <data_t>();
    this->m_data->config_ = std::make_shared<manapi::json>(manapi::json::object());
    this->m_data->sctx = std::move(sctx);
    this->m_data->compressors_for_file = std::make_unique<decltype(this->m_data->compressors_for_file)::element_type>();
    this->m_data->compressors_for_string = std::make_unique<decltype(this->m_data->compressors_for_string)::element_type>();
    this->m_data->transport_protocol_workers = std::make_unique<decltype(this->m_data->transport_protocol_workers)::element_type>();
    this->m_data->http_protocol_workers = std::make_unique<decltype(this->m_data->http_protocol_workers)::element_type>();



    #if MANAPIHTTP_ZLIB_DEPENDENCY
    this->compressor_for_file("deflate", +[] (std::string src, std::string dest)
        -> future<manapi::status> { return manapi::compress::deflate_compress_file( std::move(src), std::move(dest)); });
    this->compressor_for_file("gzip", +[] (std::string src, std::string dest)
        -> future<manapi::status> { return manapi::compress::gzip_compress_file(std::move(src), std::move(dest)); });

    this->compressor_for_string("deflate", +[] (std::string_view data)
        -> status_or<std::string> { return compress::deflate_compress_string(data); });
    this->compressor_for_string("gzip", +[] (std::string_view data)
        -> status_or<std::string> { return compress::gzip_compress_string(data); });
#endif

#if MANAPIHTTP_BROTLI_DEPENDENCY
    this->compressor_for_file("br", +[] (std::string src, std::string dest)
        -> future<manapi::status> { return manapi::compress::brotli_compress_file(std::move(src), std::move(dest), 11, 22, 0); });
    this->compressor_for_string("br", +[] (std::string_view data)
        -> status_or<std::string> { return compress::brotli_compress_string(data, 11, 22, 0); });
#endif

#if MANAPIHTTP_ZSTD_DEPENDENCY
    this->compressor_for_file("zstd", +[] (std::string src, std::string dest)
        -> future<manapi::status> { return manapi::compress::zstd_compress_file(std::move(src), std::move(dest), 1); });
    this->compressor_for_string("zstd", +[] (std::string_view data)
        -> status_or<std::string> { return compress::zstd_compress_string(data, 1); });
#endif

    this->transport_protocol_worker("tcp", "default", worker::TCP::create);
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    this->transport_protocol_worker("tls", "openssl", worker::OpenSSL_TLS::create);
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY
    this->transport_protocol_worker("tls", "wolfssl", worker::WolfSSL_TLS::create);
#endif

#if MANAPIHTTP_QUICHE_DEPENDENCY
    this->transport_protocol_worker("quic", "quiche", worker::http_v3_cloudflare_quiche::create);
#endif

#ifdef MANAPIHTTP_OPENSSL_QUIC_SUPPORT
    this->transport_protocol_worker("quic", "openssl", worker::openssl_quic::create);
#endif

    this->http_protocol_worker(http::versions::HTTP_v1_1, "default", [] (worker::interface_worker *w)
        { return create_http_protocol_worker (w, worker::default_wrk_http1_global_init); });
    this->http_protocol_worker(http::versions::HTTP_v2, "default", [] (worker::interface_worker *w)
        { return create_http_protocol_worker (w, worker::default_wrk_http2_global_init); });

#if MANAPIHTTP_NGHTTP2_DEPENDENCY
    this->http_protocol_worker(http::versions::HTTP_v2, "nghttp", [] (worker::interface_worker *w)
        { return create_http_protocol_worker (w, worker::ng_wrk_http2_global_init); });
#endif

#if MANAPIHTTP_NGHTTP3_DEPENDENCY
    this->http_protocol_worker(http::versions::HTTP_v3, "nghttp", [] (worker::interface_worker *w)
        { return create_http_protocol_worker (w, worker::ng_wrk_http3_global_init); });
#endif
}

manapi::status_or<std::shared_ptr<manapi::net::http::server>> manapi::net::http::server::create(std::shared_ptr<server_ctx> sctx) MANAPIHTTP_NOEXCEPT {
    try {
        return std::shared_ptr<server> (new http::server(std::move(sctx)));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::net::http::server * manapi::net::http::server::cast(worker::site *serv) {
    return dynamic_cast<http::server *> (serv);
}

manapi::future<manapi::status> manapi::net::http::server::config(std::string path) {
    try {
        if (this->m_data->event_id)
            co_return status_already_exists("http:config already exists");

        this->m_data->event_id = async::current()->eventloop()->subscribe_finish(-1, [p = this->shared_from_this()] () mutable
            -> future<> {
            auto res = co_await p->stop();
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:Stop status=%.*s", res.msg().size(), res.msg().data());
        });

        this->m_data->clean_up_id = async::current()->eventloop()->subscribe_clean_up([p = this->shared_from_this()] () mutable
            -> void {
            http_server_clean_up(p->m_data.get());
        });

        this->m_data->server_config = co_await this->m_data->sctx->storage().subscribe([p = this->shared_from_this()] (auto &&f1)
            -> void { http_server_on_config_update(p->m_data.get(), std::forward<decltype(f1)>(f1)); });

        auto res = co_await this->m_data->sctx->storage().edit_async (this->m_data->server_config,
            [this, path = std::move(path)] (manapi::json &config) mutable -> manapi::future<bool> {
                if (!config.is_object())
                    config = manapi::json::object();

                if (!config.contains("site") || !config["site"].is_object()) {

                    try {
                        auto exists = co_await manapi::fs::async_exists(path);
                        if (!exists.ok() || !exists.unwrap())
                        {
                            std::string data = manapi::json::object().dump(4);
                            auto res = co_await manapi::fs::async_write(path, std::move(data), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                            res.unwrap();
                        }

                        auto res = co_await manapi::fs::async_read (path);
                        auto obj = manapi::json(res.unwrap(), true);

                        if (!obj.is_object())
                            obj = manapi::json::object();

                        config["site"] = std::move(obj);
                    }
                    catch (std::exception const &e) {
                        manapi_log_trace("%s failed due to %s", "http:router:config read", e.what());
                    }

                    config["site_time"] = 0;
                    config["site_path"] = std::move(path);
                    config["cache_time"] = 0;

                    co_await http_server_setup_config(config);
                    *this->m_data->config_ = config;
                    co_return true;
                }
                *this->m_data->config_ = config;
                co_return false;
        });
        res.unwrap();
        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:config() failed", e.what());
    }

    auto res = manapi::status_ok();
    try {
        res = co_await this->stop();
        http_server_clean_up(this->m_data.get());
    }
    catch (std::exception const &e) {
       res = manapi::status_unknown(std::string{e.what()});
    }

    if (!res.ok())
        manapi_log_ferror("http:failed to stop due to %s", res.msg().data());

    co_return status_internal("http:config() failed");
}

manapi::future<manapi::status> manapi::net::http::server::config_object(json config) {
    try {
        if (this->m_data->event_id)
            co_return status_already_exists("http:config already exists");

        this->m_data->event_id = async::current()->eventloop()->subscribe_finish(-1, [p = this->shared_from_this()] () mutable
            -> future<> {
            auto res = co_await p->stop();
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:Stop status=%.*s", res.msg().size(), res.msg().data());
        });

        this->m_data->clean_up_id = async::current()->eventloop()->subscribe_clean_up([p = this->shared_from_this()] () mutable
            -> void {
            http_server_clean_up(p->m_data.get());
        });

        this->m_data->server_config = co_await this->m_data->sctx->storage().subscribe([p = this->shared_from_this()] (auto &&f1)
            -> void { http_server_on_config_update(p->m_data.get(), std::forward<decltype(f1)>(f1)); });

        auto res = co_await this->m_data->sctx->storage().edit_async (this->m_data->server_config,
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
                        manapi_log_trace("%s failed due to %s", "http:router:config read", e.what());
                    }
                }

                config["site_time"] = 0;
                config["site_path"] = "";
                config["cache_time"] = 0;

                co_await http_server_setup_config(config);
                co_return true;
        });
        res.unwrap();
        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:config() failed", e.what());
    }

    auto res = manapi::status_ok();
    try {
        res = co_await this->stop();
        http_server_clean_up(this->m_data.get());
    }
    catch (std::exception const &e) {
        res = manapi::status_unknown(std::string{e.what()});
    }

    if (!res.ok())
        manapi_log_ferror("http:failed to stop due to %s", res.msg().data());

    co_return status_internal("http:config() failed");
}

manapi::future<manapi::status> manapi::net::http::server::start() {
    auto res = manapi::status_ok();

    try {

        if (this->m_data->flags & MANAPI_HTTP_SERVER_FLAG_STOPPING) {
            co_return status_unavailable("http:is stopping");
        }

        if (!this->m_data->event_id) {
            co_return status_unavailable("http:wasn't configured");
        }

        if (this->m_data->flags & MANAPI_HTTP_SERVER_FLAG_RUNNING) {
            co_return status_already_exists("http:already running");
        }

        this->m_data->flags |= MANAPI_HTTP_SERVER_FLAG_RUNNING;

        co_await http_server_init_pool(this->shared_from_this(), this->m_data.get());

        typedef async::promise_sync<manapi::status> promise;
        res = co_await promise([srv = this->shared_from_this()] (const promise::resolve_t& resolve, const promise::reject_t& reject) -> void {
            auto res = http_server_pool(srv, srv->m_data.get(), [resolve] () mutable -> void {
                resolve (status_ok());
            });

            if (!res)
                resolve(std::move(res));
        });

        if (!res)
            goto err;

        co_return std::move(res);
    }
    catch (std::bad_alloc const &) {
        res = status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "start() failed", e.what());
        res = status_internal("start() failed");
    }
err:
    this->m_data->flags ^= MANAPI_HTTP_SERVER_FLAG_RUNNING;
    co_return std::move(res);
}

manapi::status manapi::net::http::server::GET(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("GET", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::POST(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("POST", std::move(uri), std::move(handler),  std::move(params)).err();
}

manapi::status manapi::net::http::server::OPTIONS(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("OPTIONS", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::PUT(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PUT", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::PATCH(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PATCH", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::GET(std::string uri, std::string folder, handler_template_t handler) MANAPIHTTP_NOEXCEPT {
    return this->handler ("GET", std::move(uri), std::move(folder), std::move(handler)).err();
}

manapi::future<manapi::status> manapi::net::http::server::stop() {
    auto res = manapi::status_ok();

    try {

        if ((this->m_data->flags & MANAPI_HTTP_SERVER_FLAG_STOPPING)) {
            co_return status_already_exists("http:already stopping");
        }

        if (!this->m_data->event_id) {
            co_return status_not_found("http:wasn't configured");
        }

        this->m_data->flags |= MANAPI_HTTP_SERVER_FLAG_STOPPING;

        // короч. мне лень. это проблема не сегодняшнего меня
        // АААА. Я делаю рефакторинг в рандомный день 30 Apr 2026


        auto &pools = this->m_data->pools[std::this_thread::get_id()];

        // stop all pools
        for (const auto &pool: pools)
        {
            auto config = pool.second->config();
            manapi_log_trace (manapi::debug::LOG_TRACE_HIGH, "pool %.*s:%.*s #%zu is stopping...",
                config->address.size(), config->address.data(), config->port.size(), config->port.data(), pool.first);
            auto res= co_await pool.second->stop();
            if (!res.ok())
                res.log();
            manapi_log_trace (manapi::debug::LOG_TRACE_HIGH, "pool #%zu stopped successfully", pool.first);
        }

        if (this->m_data->server_config) {
            try {
                manapi::unwrap(co_await this->m_data->sctx->storage().edit_async(this->m_data->server_config, [p = this->shared_from_this()] (json &data) -> manapi::future<bool> {
                    if (!data.contains("saved") || data["saved"] != true) {
                        data["saved"] = true;
                        if (data.contains("site_path")
                            && data["site"].contains("save")
                            && data["site"]["save"] == true)
                            co_await http_server_save(p, p->m_data.get());

                        // cache config
                        co_await manapi::fs::async_write(fs::path::join(data["cache_path"].as_string(), std::string{http_default_config_name}),
                            data["cache"].dump(),
                            ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                    }

                    co_return false;
                }));
            }
            catch (std::exception const &e) {
                async::current()->logger()->ferror(ERR_UNKNOWN,
                    "http: couldn't save the configuration file due to %s", e.what());
            }

            co_await this->m_data->sctx->storage().unsubscribe(std::move(this->m_data->server_config));
        }

        if (this->m_data->init_watcher) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:unwatch_async(this->m_data->init_watcher)");
            async::current()->eventloop()->stop_watcher(std::move(this->m_data->init_watcher));
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:finish unwatch_async(this->m_data->init_watcher)");
        }


        http_server_clean_up(this->m_data.get());

        if (this->m_data->flags & MANAPI_HTTP_SERVER_FLAG_RUNNING)
            this->m_data->flags ^= MANAPI_HTTP_SERVER_FLAG_RUNNING;

        async::current()->eventloop()->unsubscribe_finish(std::exchange(this->m_data->event_id, 0));
        async::current()->eventloop()->unsubscribe_clean_up(std::exchange(this->m_data->clean_up_id, 0));

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stop() failed", e.what());
        res = status_internal("stop() failed");
    }
err:
    this->m_data->flags ^= MANAPI_HTTP_SERVER_FLAG_STOPPING;
    co_return std::move(res);
}

manapi::net::http::handler_template_t::handler_template_t() : handler_template_t(nullptr) {}

manapi::net::http::handler_template_t::handler_template_t(const nullptr_t &n) {
    this->m_type = HANDLER_TEMPLATE_NONE_TYPE;
    this->m_data = nullptr;
}

manapi::net::http::handler_template_t::handler_template_t(handler_template_t &&n) MANAPIHTTP_NOEXCEPT {
    this->m_type = n.m_type;
    this->m_data = n.m_data;

    n.m_data = nullptr;
    n.m_type = HANDLER_TEMPLATE_NONE_TYPE;
}

manapi::net::http::handler_template_t & manapi::net::http::handler_template_t::operator=( handler_template_t &&n) MANAPIHTTP_NOEXCEPT {
    this->m_type = n.m_type;
    this->m_data = n.m_data;

    n.m_data = nullptr;
    n.m_type = HANDLER_TEMPLATE_NONE_TYPE;
    return *this;
}

manapi::net::http::handler_template_t::handler_template_t(http::async_handler_t cb) {
    auto s = std::make_unique<async_handler_t>(std::move(cb));
    this->m_type = HANDLER_TEMPLATE_ASYNC_CB_TYPE;
    this->m_data = s.release();
}

manapi::net::http::handler_template_t::handler_template_t(http::sync_handler_t cb) {
    auto s = std::make_unique<sync_handler_t>(std::move(cb));
    this->m_type = HANDLER_TEMPLATE_SYNC_CB_TYPE;
    this->m_data = s.release();
}

manapi::net::http::handler_template_t::~handler_template_t() {
    switch (this->m_type) {
        case HANDLER_TEMPLATE_NONE_TYPE: break;
        case HANDLER_TEMPLATE_SYNC_CB_TYPE:
            delete static_cast<sync_handler_t *> (this->m_data);
        break;
        case HANDLER_TEMPLATE_ASYNC_CB_TYPE:
            delete static_cast<async_handler_t *>(this->m_data);
        break;
        default:
            break;
    }
}

bool manapi::net::http::handler_template_t::is_async_cb() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == HANDLER_TEMPLATE_ASYNC_CB_TYPE;
}

bool manapi::net::http::handler_template_t::is_sync_cb() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == HANDLER_TEMPLATE_SYNC_CB_TYPE;
}

manapi::status_or<manapi::net::http::async_handler_t *> manapi::net::http::handler_template_t::async_cb() MANAPIHTTP_NOEXCEPT {
    if (this->m_type == HANDLER_TEMPLATE_ASYNC_CB_TYPE)
        return static_cast<async_handler_t *>(this->m_data);
    return status_not_found("async cb not found");
}

manapi::status_or<manapi::net::http::sync_handler_t *> manapi::net::http::handler_template_t::sync_cb() MANAPIHTTP_NOEXCEPT {
    if (this->m_type == HANDLER_TEMPLATE_SYNC_CB_TYPE)
        return static_cast<sync_handler_t *>(this->m_data);
    return status_not_found("sync cb not found");
}

manapi::net::http::handler_template_t::operator bool() const MANAPIHTTP_NOEXCEPT {
    return this->m_type != HANDLER_TEMPLATE_NONE_TYPE && this->m_data;
}


// ======================[ configs funcs]==========================

void manapi::net::http::server::compressor_for_file(const std::string &name, compress_file_cb_t handler) {
    this->m_data->compressors_for_file->insert_or_assign(name, std::move(handler));
}

void manapi::net::http::server::compressor_for_string(const std::string &name, compress_str_cb_t handler) {
    this->m_data->compressors_for_string->insert_or_assign(name, std::move(handler));
}

manapi::net::http::server::compress_file_cb_t * manapi::net::http::server::compressor_for_file(std::string_view name) {
    auto it = this->m_data->compressors_for_file->find(name);
    if (it == this->m_data->compressors_for_file->end())
        return nullptr;

    return &it->second;
}

bool manapi::net::http::server::contains_compressor_for_file(std::string_view name) const {
    return this->m_data->compressors_for_file->find(name) != this->m_data->compressors_for_file->end();
}

manapi::net::http::server::compress_str_cb_t * manapi::net::http::server::compressor_for_string(std::string_view name) {
    auto it = this->m_data->compressors_for_string->find(name);
    if (it == this->m_data->compressors_for_string->end())
        return nullptr;

    return &it->second;
}

bool manapi::net::http::server::contains_compressor_for_string(std::string_view name) const {
    return this->m_data->compressors_for_string->find(name) != this->m_data->compressors_for_string->end();
}

void manapi::net::http::server::transport_protocol_worker(const std::string &type, const std::string &name, implement_create_cb worker) {
    (*this->m_data->transport_protocol_workers)[type][name] = std::move(worker);
}

const std::map<std::string, manapi::net::http::server::implement_create_cb> &manapi::net::http::server::transport_protocol_worker(const std::string &type) {
    return (*this->m_data->transport_protocol_workers)[type];
}

void manapi::net::http::server::http_protocol_worker(http::versions::http type, const std::string &name, implemenet_http_cb worker) {
    (*this->m_data->http_protocol_workers)[type][name] = std::move(worker);
}

const std::map<std::string, manapi::net::http::server::implemenet_http_cb> & manapi::net::http::server::http_protocol_worker(http::versions::http type) {
    return (*this->m_data->http_protocol_workers)[type];
}

const std::string & manapi::net::http::server::config_cache_dir() const {
    return this->m_data->config_->at("cache_path").as_string();
}

manapi::future<manapi::status_or<std::string>> manapi::net::http::server::get_compressed_cache_file(std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    try {
        while (true) {

            auto *cache = &this->m_data->config_->at("cache");
            if (!cache->contains(algorithm))
                break;


            auto *files = &cache->at(algorithm);

            auto it = files->find(file);
            if (it == files->end<json::OBJECT>())
                break;


            auto file_info = &it->second;

            if (!file_info->is_object()) {
                if (*file_info == false)
                    co_return manapi::status_unavailable("compress:Busy");

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

        co_return status_not_found("compress:Cache file not found");
    }
    catch (std::exception const &e) {
        manapi_log_error("compress:Get compressed file failed due to %s", e.what());
    }
    co_return status_internal("compress:Get compressed failed");
}

manapi::future<manapi::status> manapi::net::http::server::set_compressed_cache_file(std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    try {
        auto res = co_await this->m_data->sctx->storage().edit (this->m_data->server_config,
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
                    manapi_log_error("%s: %s failed due to %s",
                        "http", "caching", e.what());
                    cache->at(algorithm).erase(file);
                }

                if (!del.empty()) {
                    manapi::async::run<manapi::ev::status>(manapi::fs::async_unlink(std::move(del),
                        manapi::ctokens::timeout(5000)), [] (std::exception_ptr err, manapi::ev::status *s) -> void {
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

        res.unwrap();

        co_return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("set compressed failed due to %s", e.what());
    }

    co_return manapi::status_internal("compress:Set cache file failed");
}

manapi::future<manapi::status> manapi::net::http::server::set_locked_cache_file(std::string file, bool lock, std::string algorithm) {
    try {
        bool busy = false;
        if (this->m_data->config_ && this->m_data->config_->is_object()) {
            auto cache_config = this->m_data->config_->find("cache");
            if (cache_config != this->m_data->config_->end<manapi::json::OBJECT>()) {
                auto algoit = cache_config->second.find(algorithm);
                if (algoit != cache_config->second.end<manapi::json::OBJECT>()) {
                    auto it = algoit->second.find(file);
                    if (it == algoit->second.end<manapi::json::OBJECT>()) {
                        if (!lock)
                            co_return status_ok();
                    }
                    else {
                        if (lock) {
                            if (it->second.is_bool()
                                && it->second == false)
                                co_return status_unavailable("compress:Busy");
                        }
                        else {
                            if (it->second.is_object())
                                co_return status_ok();
                        }
                    }
                }
            }
        }

        auto edit_res = co_await this->m_data->sctx->storage().edit(this->m_data->server_config,
            [&] (manapi::json &all) -> bool {
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
                            manapi::async::run<manapi::ev::status>(manapi::fs::async_unlink(compressit->second.as_string(),
                                manapi::ctokens::timeout(5000)), [] (std::exception_ptr err, manapi::ev::status *s) -> void {
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
                if (fit != it->second.end<json::OBJECT>()) {
                    if (!it->second.is_object())
                        it->second.erase(fit);
                }
            }
            return true;
        });

        edit_res.unwrap();

        if (busy)
            co_return status_unavailable("compress:Busy");

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("set locked compressed failed due to %s", e.what());
    }

    co_return status_internal("compress:Set cache file failed");
}

std::unique_ptr<manapi::net::http::http_handler_page> manapi::net::http::server::handler(http::request_data_t *request_data) const {
    auto handler_page = std::make_unique<http_handler_page>();

    handler_page->error = std::make_unique<http_handler_page>();
    handler_page->error->handler = http_default_error_handler;


    decltype(decltype(this->m_data->handlers)::handlers)::element_type::iterator it;

    // how much we will take the layers from handler_page.layers at the start to the handler_page.error.layer
    size_t error_layer_depth = 0;
    bool not_found = false;
    bool page_found = false;

    try
    {
        const http_uri_part *cur = &this->m_data->handlers;
        const size_t path_size = request_data->divided < 0 ? request_data->path.size() : static_cast<std::size_t>(request_data->divided);
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
                    handler_page->layer.push_back(shared_it->second);
                }
            }

            if (cur->errors)
            {
                auto error_it = cur->errors->find(request_data->method);
                if (error_it != cur->errors->end()) {
                    // find errors handlers for method!
                    handler_page->error->handler = error_it->second;
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

            if (cur->handlers) {
                auto const hhandler = cur->handlers->find(request_data->method);
                if (hhandler != cur->handlers->end()) {
                    if (hhandler->second->flags & HTTP_HANDLER_FUNC_FLAG_CUSTOM) {
                        it = hhandler;
                        page_found = true;

                        break;
                    }
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

                            manapi_log_error("cur->regexes_title (params) is null.");
                            return handler_page;
                        }

                        const size_t expected_size = match.size() - 1;
                        if (cur->params->size() != expected_size) {
                            // bug

                            auto const &s = request_data->path.at(i);
                            manapi_log_error("The expected number of parameters (%zu) does not correspond of reality (%zu). uri part: %.*s.",
                                       cur->params->size(), expected_size, s.size(), s.data());
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

        if (not_found || !cur->handlers) {
            return handler_page;
        }

        if (!page_found)
            it = cur->handlers->find (request_data->method);

        std::shared_ptr<http_handler_function> handler{nullptr};

        if (it != cur->handlers->end()) {
            handler = it->second;
        }

        if (!handler) {
            // TODO: handler error
        }

        handler_page->handler = handler;

        return std::move(handler_page);
    }
    catch (const std::exception &e) {
        manapi_log_trace("%s failed due to %s", "routing", e.what());
    }
    return std::move(handler_page);
}


manapi::status_or<manapi::net::http::http_uri_part *> manapi::net::http::server::handler(std::string method, std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    manapi::status status;
    try {
        size_t type = URI_PAGE_DEFAULT;

        http_uri_part *cur = http_server_build_uri_part(this->m_data.get(), uri, type);

        auto functions = std::make_shared<http_handler_function>();

        functions->handler = std::move(handler);

        if (params.is_object()) {
            auto it = params.find("trailers");
            if (it != params.as_object().end() && it->second.is_array()) {
                for (auto &i : it->second.each()) {
                    if (i.is_string()) {
                        auto &s = i.as_string();
                        for (auto &c : s) {
                            c = static_cast<char>(std::tolower(c));
                        }
                        functions->trailers.insert(std::move(s));
                    }
                }
            }

            it = params.find("trailers_size");
            if (it != params.as_object().end() && it->second.is_integer()) {
                functions->trailers_size = it->second.as_integer();
            }
        }

        switch (type) {
            case URI_PAGE_DEFAULT: {
                if (!cur->handlers)
                    cur->handlers = std::make_unique<handlers_types_t> ();

                auto res = cur->handlers->insert({std::move(method), std::move(functions)});
                if (!res.second) {
                    status = manapi::status_already_exists("http:handler exists");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%.*s url=%.*s method=%.*s",
                        status.msg().size(), status.msg().data(), uri.size(), uri.data(),
                        res.first->first.size(), res.first->first.data());
                    goto err;
                }
                break;
            }
            case URI_PAGE_ERROR: {
                if (!cur->errors) {
                    cur->errors = std::make_unique<handlers_types_t>();
                }
                auto res = cur->errors->insert({std::move(method), std::move(functions)});
                if (!res.second) {
                    status = manapi::status_already_exists("http:error handler exists");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%.*s url=%.*s method=%.*s",
                        status.msg().size(), status.msg().data(), uri.size(), uri.data(),
                        res.first->first.size(), res.first->first.data());
                    goto err;
                }

                break;
            }
            case URI_PAGE_LAYER: {
                if (!cur->layers) {
                    cur->layers = std::make_unique<handlers_types_t> ();
                }

                auto res = cur->layers->insert({std::move(method), std::move(functions)});
                if (!res.second) {
                    status = manapi::status_already_exists("http:layer handler exists");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%.*s url=%.*s method=%.*s",
                        status.msg().size(), status.msg().data(), uri.size(), uri.data(),
                        res.first->first.size(), res.first->first.data());
                    goto err;
                }

                break;
            }
            case URI_PAGE_CUSTOM: {
                if (!cur->handlers)
                    cur->handlers = std::make_unique<handlers_types_t> ();

                functions->flags |= HTTP_HANDLER_FUNC_FLAG_CUSTOM;
                auto res = cur->handlers->insert({std::move(method), std::move(functions)});

                if (!res.second) {
                    status = manapi::status_already_exists("http:handler exists");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%.*s url=%.*s method=%.*s",
                        status.msg().size(), status.msg().data(), uri.size(), uri.data(),
                        res.first->first.size(), res.first->first.data());
                    goto err;
                }

                break;
            }
            default:
                return nullptr;
        }


        return cur;
    }
    catch (std::bad_alloc const &) {
        status = status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "http:handler failed", e.what());
        status = status_internal("http:handler failed");
    }
err:
    return std::move(status);
}

manapi::status_or<manapi::net::http::http_uri_part *> manapi::net::http::server::handler(std::string method, std::string uri, std::string folder, handler_template_t handler) MANAPIHTTP_NOEXCEPT {
    manapi::status status;
    try {
        size_t type = URI_PAGE_DEFAULT;

        http_uri_part *cur = http_server_build_uri_part(this->m_data.get(), uri, type);

        switch (type) {
            case URI_PAGE_DEFAULT: {
                if (cur->statics == nullptr) {
                    cur->statics = std::make_unique<handlers_static_types_t> ();
                }


                http_static_handler_function func_static_hdl{};
                func_static_hdl.folder = manapi::fs::path::serialize(folder);
                auto res = cur->statics->insert({std::move(method), std::move(func_static_hdl)});

                if (res.second) {
                    if (handler) {
                        auto &layer = res.first->second.layer;
                        layer = std::make_unique<http_handler_function>(
                            std::move(handler));

                    }
                }
                else {
                    status = manapi::status_already_exists("http:static handler exists");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%.*s url=%.*s method=%.*s",
                        status.msg().size(), status.msg().data(), uri.size(), uri.data(),
                        res.first->first.size(), res.first->first.data());
                    goto err;
                }


                break;
            }
            default:
                status = manapi::status_invalid_argument("http:can not use the special pages with the static files");
            goto err;
        }

        return cur;
    }
    catch (std::bad_alloc const &) {
        status = manapi::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "http:handler failed", e.what());
        status = manapi::status_internal("http:handler failed");
    }
err:
    return std::move(status);
}
