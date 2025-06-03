#include "ManapiFilesystem.hpp"
#include "ManapiSite.hpp"

#include "encoding/ManapiUnicode.hpp"
#include "worker/base_worker.hpp"
#include "worker/TCP.hpp"
#include "worker/OpenSSL_TLS.hpp"
#include "worker/QUIC.hpp"
#include "worker/HTTPv2.hpp"
#include "ManapiUtils.hpp"

#include "services/ManapiTaskFunction.hpp"
#include "services/ManapiThreadPool.hpp"
#include "worker/HTTPv3_clouflare_quiche.hpp"
#include "worker/HTTPv3_tquic.hpp"
#include "worker/WolfSSL_TLS.hpp"

#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"
#include "async/ManapiEasyCancellation.hpp"

namespace manapi::net {
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
        co_return resp.text(std::format("<html>\n\t<head>\n\t\t"
                            "<title>{0} {1}</title>\n\t</head>\n\t<body>\n\t\t<center>\n\t\t\t"
                            "<h1>{0} {1}</h1>\n\t\t</center>\n\t\t<hr>\n\t\t"
                            "<center>{3}/{2}</center>\n\t"
                            "</body>\n</html>", resp.status_code(),
                            resp.status_message(), MANAPIHTTP_VERSION, MANAPIHTTP_NAME));
    },
    .post_mask = nullptr,
    .get_mask = nullptr,
};

std::string manapi::net::http::site::default_config_name      = "config_.json";

// ======================[ configs funcs]==========================

void manapi::net::http::site::compressor_for_file(const std::string &name, std::move_only_function<future<void>(std::string src, std::string dest)> handler) {
    this->data->compressors_for_file[name] = std::move(handler);
}

void manapi::net::http::site::compressor_for_string(const std::string &name, std::move_only_function<std::string(std::string_view data)> handler) {
    this->data->compressors_for_string[name] = std::move(handler);
}

std::move_only_function<manapi::future<void>(std::string src, std::string dest)> & manapi::net::http::site::compressor_for_file(const std::string &name) {
    auto it = this->data->compressors_for_file.find(name);
    if (it == this->data->compressors_for_file.end()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FUNCTION_IS_NULL, "The compressor {} doesn't exists", name);
    }

    return it->second;
}

bool manapi::net::http::site::contains_compressor_for_file(const std::string &name) const {
    return this->data->compressors_for_file.contains(name);
}

std::move_only_function<std::string(std::string_view)> & manapi::net::http::site::compressor_for_string(const std::string &name) {
    auto it = this->data->compressors_for_string.find(name);
    if (it == this->data->compressors_for_string.end()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FUNCTION_IS_NULL, "The compress {} doesn't exists", name);
    }
    return it->second;
}

bool manapi::net::http::site::contains_compressor_for_string(const std::string &name) const {
    return this->data->compressors_for_string.contains(name);
}

void manapi::net::http::site::transport_protocol_worker(const std::string &type, const std::string &name, implement_create_cb worker) {
    this->data->transport_protocol_workers[type][name] = std::move(worker);
}

const std::map<std::string, manapi::net::http::site::implement_create_cb> &manapi::net::http::site::transport_protocol_worker(const std::string &type) {
    return this->data->transport_protocol_workers[type];
}

const std::string & manapi::net::http::site::config_cache_dir() {
    return this->data->config_cache_dir;
}

void manapi::net::http::site::setup() {
    // fast ios
    // std::ios_base::sync_with_stdio(false);
    // std::cout.tie(nullptr);

#if MANAPIHTTP_ZLIB_DEPENDENCY
    this->compressor_for_file("deflate", +[] (std::string src, std::string dest)
        -> future<void> { return manapi::compress::deflate_compress_file( std::move(src), std::move(dest)); });
    this->compressor_for_file("gzip", +[] (std::string src, std::string dest)
        -> future<void> { return manapi::compress::gzip_compress_file(std::move(src), std::move(dest)); });

    this->compressor_for_string("deflate", +[] (std::string_view data)
        -> std::string { return compress::deflate_compress_string(data); });
    this->compressor_for_string("gzip", +[] (std::string_view data)
        -> std::string { return compress::gzip_compress_string(data); });
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    this->compressor_for_file("br", +[] (std::string src, std::string dest)
        -> future<void> { return manapi::compress::brotli_compress_file(std::move(src), std::move(dest), 11, 22, 0); });
    this->compressor_for_string("br", +[] (std::string_view data)
        -> std::string { return compress::brotli_compress_string(data, 11, 22, 0); });
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    this->compressor_for_file("zstd", +[] (std::string src, std::string dest)
        -> future<void> { return manapi::compress::zstd_compress_file(std::move(src), std::move(dest), 1); });
    this->compressor_for_string("zstd", +[] (std::string_view data)
        -> std::string { return compress::zstd_compress_string(data, 1); });
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

#if MANAPIHTTP_TQUIC_DEPENDENCY
    this->transport_protocol_worker("quic", "tquic", worker::http_v3_tquic::create);
#endif

#ifdef MANAPIHTTP_DEFAULT_QUIC
    this->transport_protocol_worker("quic", "default", worker::quic::create);
#endif

}

manapi::future<> manapi::net::http::site::config(std::string path) {
    this->data->server_config = this->data->sctx.server_config(this->data->server_config_notifier);
    this->data->config_path = std::move(path);

    {
        auto lk = co_await this->data->server_config->config_mx->lock_guard();

        if (this->data->server_config->config.is_null()) {

            try {
                if (!co_await manapi::filesystem::async_exists(this->data->config_path))
                {
                    std::string data = manapi::json::object().dump(4);
                    co_await manapi::filesystem::async_write(this->data->config_path, std::move(data), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                }
                this->data->server_config->config = manapi::json(co_await manapi::filesystem::async_read ( this->data->config_path), true);
                if (!this->data->server_config->config.is_object())
                    this->data->server_config->config = manapi::json::object();
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("server router: config read failed due to {}", e.what());
            }
            co_await this->setup_config ();
        }

        this->data->config_ = std::make_shared<manapi::json>(this->data->server_config->config);
    }
    {
        auto lk = co_await this->data->server_config->cache_mx->lock_guard();
        if (this->data->server_config->cache.is_null()) {
            this->data->server_config->cache = manapi::json::object();
        }
        this->data->cache_config = std::make_shared<manapi::json>(this->data->server_config->cache);
    }
    this->data->server_config_notifier->send();
}

manapi::future<> manapi::net::http::site::config_object(json config) {
    this->data->server_config = this->data->sctx.server_config(this->data->server_config_notifier);
    {
        auto lk = co_await this->data->server_config->config_mx->lock_guard();

        if (this->data->server_config->config.is_null()) {
            this->data->server_config->config = std::move(config);
            co_await this->setup_config();
        }

        this->data->config_ = std::make_shared<manapi::json>(this->data->server_config->config);
    }

    {
        auto lk = co_await this->data->server_config->cache_mx->lock_guard();
        if (this->data->server_config->cache.is_null()) {
            this->data->server_config->cache = manapi::json::object();
        }
        this->data->cache_config = std::make_shared<json>(this->data->server_config->cache);
    }
    this->data->server_config_notifier->send();
}

manapi::future<> manapi::net::http::site::setup_config() {
    auto const server_config = this->data->server_config_notifier;
    //auto lk = co_await this->data->server_config->config_mx->lock_guard();

    if (this->data->server_config->config.contains("cache_dir"))
    {
        const auto b = this->data->server_config->config.at("cache_dir");
        this->data->config_cache_dir = b.as_string();
    }

    if (this->data->server_config->config.contains("save_config")) {
        if (this->data->server_config->config["save_config"].is_bool())
        {
            this->data->enabled_save_config = this->data->server_config->config["save_config"].as_bool();
        }
    }

    try {
        if (this->data->config_cache_dir.empty()) {
            this->data->config_cache_dir = manapi::filesystem::path::join(std::filesystem::temp_directory_path().string(), MANAPIHTTP_NAME, "cache");
        }

        co_await manapi::filesystem::async_mkdir(this->data->config_cache_dir, ev::IRUSR|ev::IWUSR|ev::IRGRP|ev::IXUSR|ev::IXGRP, true);

        manapi::filesystem::path::append_delimiter(this->data->config_cache_dir);
        auto path = this->data->config_cache_dir + site::default_config_name;
        try {
            if (co_await manapi::filesystem::async_exists(path)) {
                this->data->server_config->cache = manapi::json(co_await manapi::filesystem::async_read(path), true);
            }
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_CONFIG_ERROR, "cached data couldn't be loaded from the config due to {}", e.what());
        }
    }
    catch (manapi::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service, e.err_num(), "The configuration directory ({}) couldn't be created due to {}.",
         this->data->config_cache_dir, e.what());
    }

    manapi::net::http::server_ctx::next_time(&this->data->server_config->cache_time);
    manapi::net::http::server_ctx::next_time(&this->data->server_config->config_time);
}

manapi::future<std::pair<int, std::string>> manapi::net::http::site::get_compressed_cache_file(std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    while (true) {
        if (!this->data->cache_config->contains(algorithm))
        {
            co_return {1, std::string{}};
        }


        auto *files = &this->data->cache_config->at(algorithm);

        if (!files->contains(file))
        {
            co_return {1, std::string{}};
        }

        auto file_info = &files->operator[](file);

        if (file_info->is_bool() && *file_info == false) {
            co_await this->data->cache_cv->wait([&] ()
                -> bool {
                files = &this->data->cache_config->at(algorithm);
                file_info = &files->operator[](file);

                return !file_info->is_bool();
            });
        }

        if (file_info->at("last-write").as_string() == std::format("{:%Y-%m-%d-%H-%M-%S}", filetime)) {
            co_return {0, file_info->at("compressed").as_string()};
        }

        {
            {
                auto lk = co_await this->data->server_config->cache_mx->lock_guard();
                file_info = &this->data->server_config->cache[algorithm][file];

                if (file_info->is_bool() && *file_info == false) {
                    lk.call();

                    co_await this->data->cache_cv->wait([&] ()
                        -> bool {
                        return !this->data->cache_config->at(algorithm)[file].is_bool();
                    });

                    continue;
                }

                if (file_info->is_object() && file_info->at("last-write").as_string() == std::format("{:%Y-%m-%d-%H-%M-%S}", filetime)) {
                    co_return {0, file_info->at("compressed").as_string()};
                }

                (*file_info) = false;
            }

            manapi::net::http::server_ctx::next_time(&this->data->server_config->cache_time);
            this->data->sctx.server_notify_subs();
        }

        break;
    }

    co_return {1, std::string{}};
}

manapi::future<> manapi::net::http::site::set_compressed_cache_file(std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime) {
    {
        auto lk = co_await this->data->server_config->cache_mx->lock_guard();

        try {
            if (this->data->server_config->cache.contains(algorithm)) {
                auto &alghs = this->data->server_config->cache[algorithm];
                auto fileit = alghs.as_object().find(file);
                if (fileit != alghs.as_object().end()
                    && fileit->second.is_object()) {
                    auto const compressedit = fileit->second.as_object().find("compressed");
                    if (compressedit != fileit->second.as_object().end()) {
                        manapi::async::run(manapi::filesystem::async_unlink(compressedit->second.as_string(),
                            manapi::async::timeout_cancellation(5000)), [] (std::exception_ptr err) -> void {
                                if (err) {
                                    /* ignore :) */
                                }
                            });
                    }
                }
            }
            else {
                this->data->server_config->cache.insert(algorithm, manapi::json::object());
            }


            manapi::json file_info = manapi::json::object();

            file_info.insert("last-write", std::format("{:%Y-%m-%d-%H-%M-%S}", filetime));
            file_info.insert("compressed", compressed);

            this->data->server_config->cache[algorithm][file] = file_info;
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("Fatal error in caching: {}", e.what());
            this->data->server_config->cache[algorithm].erase(file);
        }

    }

    manapi::net::http::server_ctx::next_time(&this->data->server_config->cache_time);
    this->data->sctx.server_notify_subs();
}

manapi::future<> manapi::net::http::site::save_config(std::shared_ptr<data_t> data) {
    auto const &config = (data->server_config->config);

    if (config.is_object()
        && config.contains("save_config")
        && config["save_config"].as_bool()) {

        // main config
        co_await manapi::filesystem::async_write(data->config_path,
            config.dump(4), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
    }
}

void manapi::net::http::site::check_exists_method_on_url(const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the url {}", method, url); }
}

void manapi::net::http::site::check_exists_method_on_url(const std::string &url,
    const std::unique_ptr<handlers_static_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the static url {}", method, url); }
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

        http_handler_function *handler = &cur->handlers->at (request_data->method);

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
    this->data = std::make_shared<data_t>(nullptr, nullptr, nullptr,
        std::make_shared<manapi::json>(manapi::json::object()),
        std::make_shared<manapi::json>(manapi::json::object()), 0, 0, std::string{}, std::string{}, std::move(sctx), false, http_uri_part{nullptr, nullptr, nullptr, nullptr, nullptr,nullptr,nullptr});
    this->data->cache_cv = std::make_unique<async::condition_variable>();
    this->data->server_config_notifier = async::current()->eventloop()->create_watcher_async([this] (ev::shared_async &w)
        -> void {
        manapi::async::run ([this] ()
            -> manapi::future<> {
            if (this->data->server_config->config_time != this->data->config_time) {
                auto lk = co_await this->data->server_config->config_mx->lock_guard();
                this->data->config_ = std::make_shared<manapi::json>(this->data->server_config->config);
                this->data->config_time = this->data->server_config->config_time;

            }

            if (this->data->server_config->cache_time != this->data->cache_time) {
                auto lk = co_await this->data->server_config->cache_mx->lock_guard();
                this->data->cache_config = std::make_shared<manapi::json>(this->data->server_config->cache);
                this->data->cache_time = this->data->server_config->cache_time;

                auto const cachedirit = this->data->server_config->config.as_object().find("cache_dir");
                if (cachedirit != this->data->server_config->config.as_object().end() && cachedirit->second.is_string()) {
                    this->data->config_cache_dir = cachedirit->second.as_string();
                }
                else {
                    this->data->config_cache_dir = manapi::filesystem::path::join(std::filesystem::temp_directory_path().string(), MANAPIHTTP_NAME, "cache");
                }

                manapi::filesystem::path::append_delimiter(this->data->config_cache_dir);
            }
        });
    });
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
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "{}", "can not use the special pages with the static files");
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

                if (manapi::unicode::escape_char_need(uri[i]))
                {
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

                    buff = manapi::unicode::escape_string(buff);
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

        if (is_regex)
        {
            if (manapi::unicode::escape_char_need(uri[i]))
            {
                buff.push_back('\\');
            }

            buff += uri[i];
            continue;
        }

        buff += uri[i];
    }

    return cur;
}