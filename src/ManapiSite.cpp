#include "ManapiFilesystem.hpp"
#include "ManapiSite.hpp"

#include "ManapiUnicode.hpp"
#include "worker/Base.hpp"
#include "worker/TCP.hpp"
#include "worker/OpenSSL_TLS.hpp"
#include "worker/QUIC.hpp"
#include "worker/HTTPv2.hpp"

#include "services/ManapiTaskFunction.hpp"
#include "services/ManapiThreadPool.hpp"
#include "worker/HTTPv3_clouflare_quiche.hpp"
#include "worker/HTTPv3_tquic.hpp"
#include "worker/OpenSSL_QUIC.hpp"
#include "worker/WolfSSL_TLS.hpp"

namespace manapi::net {
    // default, +error, +layout in url
    enum uri_page_type {
        URI_PAGE_DEFAULT    = 0,
        URI_PAGE_ERROR      = 1,
        URI_PAGE_LAYER      = 2
    };
}

std::string manapi::net::site::default_cache_dir        = "/tmp/";
std::string manapi::net::site::default_config_name      = "config.json";

// ======================[ configs funcs]==========================

void manapi::net::site::set_compressor(const std::string &name, manapi::compress::TEMPLATE_INTERFACE handler) {
    this->compressors[name] = handler;

}

manapi::compress::TEMPLATE_INTERFACE manapi::net::site::get_compressor(const std::string &name) {
    if (!contains_compressor(name))
    {
        return nullptr;
    }

    return this->compressors.at(name);
}

bool manapi::net::site::contains_compressor(const std::string &name) const {
    return this->compressors.contains(name);
}

void manapi::net::site::set_transport_protocol_worker(const std::string &type, const std::string &name, const std::function<std::shared_ptr<worker::base>(net::site &site, std::shared_ptr<http::config> config)> &worker) {
    this->transport_protocol_workers[type][name] = [this, worker] (std::shared_ptr<http::config> &&config) {
        return worker (*this, std::forward<decltype(config)>(config));
    };
}

const std::map<std::string, std::function<std::shared_ptr<manapi::net::worker::base>(std::shared_ptr<manapi::net::http::config> config)>> &manapi::net::site::get_transport_protocol_worker(const std::string &type) {
    return this->transport_protocol_workers[type];
}

void manapi::net::site::custom_watcher_fd_async(ev::async &w, int revents) {
    if (this->adding_watcher_data.flag) {
        if (this->adding_watcher_data.w_io) {
            this->adding_watcher_data.w_io->start();
        }
        else if (this->adding_watcher_data.w_async) {
            this->adding_watcher_data.w_async->start();
            this->adding_watcher_data.w_async->send();
        }
    }
    else {
        if (this->adding_watcher_data.w_io) {
            this->stop_watcher_fd(*this->adding_watcher_data.w_io);
        }
        else if (this->adding_watcher_data.w_async) {
            this->stop_watcher_async(*this->adding_watcher_data.w_async);
        }
    }

    this->adding_watcher_mx.unlock();
}

manapi::future<std::shared_ptr<ev::io>> manapi::net::site::watch_fd(int fd, int flags, const std::function<void(ev::io &w, int revents)> &callback) {
    co_await this->adding_watcher_mx.lock();
    auto w = this->create_watcher_fd(fd, flags, callback);
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .fd = fd,
        .flags = flags,
        .w_io = w,
    };
    this->adding_watcher_async->send();
    co_return std::move(w);
}

manapi::future<> manapi::net::site::unwatch_fd(std::shared_ptr<ev::io> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = false,
        .w_io = std::move(w),
    };
    this->adding_watcher_async->send();
}

manapi::future<std::shared_ptr<ev::async>> manapi::net::site::watch_async(const std::function<void(ev::async &w, int revents)> &callback) {
    co_await this->adding_watcher_mx.lock();
    auto w = this->create_watcher_async(callback);
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .w_async = w
    };
    this->adding_watcher_async->send();
    co_return std::move(w);
}

manapi::future<> manapi::net::site::unwatch_async(std::shared_ptr<ev::async> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = false,
        .w_async = std::move(w)
    };
}

void manapi::net::site::setup() {
    // fast ios
    // std::ios_base::sync_with_stdio(false);
    // std::cout.tie(nullptr);

    this->config = manapi::json::object();
    this->cache_config = manapi::json::object();

    this->set_compressor("deflate", manapi::compress::deflate);
    this->set_compressor("gzip", manapi::compress::gzip);

    this->set_transport_protocol_worker("tcp", "default", worker::TCP::create);
#if MANAPIHTTP_OPENSSL_DEPENDENCY
    this->set_transport_protocol_worker("tls", "openssl", worker::OpenSSL_TLS::create);
# ifdef MANAPI_OPENSSL_QUIC_REALIZATION
    this->set_transport_protocol_worker("quic", "openssl", worker::openssl_quic::create);
# endif
#endif

#if MANAPIHTTP_WOLFSSL_DEPENDENCY
    this->set_transport_protocol_worker("tls", "wolfssl", worker::WolfSSL_TLS::create);
#endif

#if MANAPIHTTP_QUICHE_DEPENDENCY
    this->set_transport_protocol_worker("quic", "quiche", worker::http_v3_cloudflare_quiche::create);
#endif

#if MANAPIHTTP_TQUIC_DEPENDENCY
    this->set_transport_protocol_worker("quic", "tquic", worker::http_v3_tquic::create);
#endif

    this->set_transport_protocol_worker("quic", "default", worker::quic::create);
}

void manapi::net::site::timer_pool_stop() {
    this->timerpool->stop();
}

void manapi::net::site::set_config(const std::string &path) {
    this->config_path = path;

    if (!manapi::filesystem::exists(this->config_path))
    {
        manapi::filesystem::config::write(this->config_path, this->config);
        return;
    }

    this->config = manapi::filesystem::config::read (this->config_path);
    setup_config ();
}

void manapi::net::site::set_config_object(const json &config) {
    this->config = config;
    setup_config();
}

const manapi::json &manapi::net::site::get_config() {
    return this->config;
}

void manapi::net::site::setup_config() {
    if (this->config.contains("cache_dir"))
    {
        const auto b = config.at("cache_dir");
        this->config_cache_dir = b.as_string();
    }
    else
    {
        this->config_cache_dir = site::default_cache_dir;
    }

    manapi::filesystem::append_delimiter(this->config_cache_dir);

    if (!manapi::filesystem::exists(this->config_cache_dir))
    {
        manapi::filesystem::mkdir(this->config_cache_dir);
    }
    else
    {
        std::string path = this->config_cache_dir + site::default_config_name;
        if (manapi::filesystem::exists(path))
        {
            this->cache_config = manapi::filesystem::config::read(path);
        }
    }

    if (this->config.contains("save_config")) {
        if (this->config["save_config"].is_bool())
        {
            this->enabled_save_config = this->config["save_config"].get<bool>();
        }
    }
}

std::string manapi::net::site::get_compressed_cache_file(const std::string &file, const std::string &algorithm) {
    if (!this->cache_config.contains(algorithm))
    {
        return {};
    }


    auto &files = this->cache_config.at(algorithm);

    if (!files.contains(file))
    {
        return {};
    }

    auto &file_info = files[file];
    if (file_info.at("last-write").get<std::string>() == manapi::filesystem::last_time_write(file, true)) {
        auto &compressed = file_info.at("compressed").get<std::string>();

        if (manapi::filesystem::exists(compressed))
        {
            return compressed;
        }

        return {};
    }

    files.erase(file);

    return {};
}

void manapi::net::site::set_compressed_cache_file(const std::string &file, const std::string &compressed, const std::string &algorithm) {
    if (!this->cache_config.contains(algorithm))
    {
        this->cache_config.insert(algorithm, manapi::json::object());
    }

    manapi::json file_info = manapi::json::object();

    file_info.insert("last-write", manapi::filesystem::last_time_write(file, true));
    file_info.insert("compressed", compressed);

    this->cache_config[algorithm].insert(file, file_info);
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::net::site::get_task_pool() const {
    return this->taskpool;
}


std::shared_ptr<ev::io> manapi::net::site::create_watcher_fd(int fd, int flags,const std::function<void(ev::io &w, int revents)> &callback) {
    auto w = std::make_shared<ev::io>(this->loop);
    ev_io_init(w.get(), site::custom_watcher_fd, fd, flags);
    w->data = new custom_watcher_data_t<ev::io> {.w = w, .cb = callback};
    return std::move(w);
}

std::shared_ptr<ev::async> manapi::net::site::create_watcher_async(const std::function<void(ev::async &w, int revents)> &callback) {
    auto w = std::make_shared<ev::async>(this->loop);
    ev_async_init(w.get(), site::custom_watcher_async);
    w->data = new custom_watcher_data_t<ev::async> {.w = w, .cb = callback};
    return std::move(w);
}

void manapi::net::site::stop_watcher_fd(ev::io &w) {
    auto data = static_cast<custom_watcher_data_t<ev::io> *>(std::exchange(w.data, nullptr));
    data->w.reset();
    delete data;
    w.stop();
}

void manapi::net::site::stop_watcher_async(ev::async &w) {
    auto data = static_cast<custom_watcher_data_t<ev::async> *>(std::exchange(w.data, nullptr));
    data->w.reset();
    delete data;
    w.stop();
}

void manapi::net::site::save() {
    if (this->enabled_save_config)
    {
        this->save_config();
    }
}

void manapi::net::site::save_config() {
    if (!manapi::filesystem::exists(this->config_path) && manapi::filesystem::is_file(this->config_path)) {
        // main config
        manapi::filesystem::config::write(this->config_path, this->config);
    }
    // cache config
    manapi::filesystem::config::write(this->config_cache_dir + site::default_config_name, this->cache_config);
}

void manapi::net::site::custom_watcher_fd(struct ev_loop *loop, ev_io *w, int revents) {
    auto &data = *static_cast<custom_watcher_data_t<ev::io> *> (w->data);
    data.cb(*data.w, revents);
}

void manapi::net::site::custom_watcher_async(struct ev_loop *loop, ev_async *w, int revents) {
    auto &data = *static_cast<custom_watcher_data_t<ev::async> *> (w->data);
    data.cb(*data.w, revents);
}

void manapi::net::site::check_exists_method_on_url(const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the url {}", method, url); }
}

void manapi::net::site::check_exists_method_on_url(const std::string &url,
    const std::unique_ptr<handlers_static_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the static url {}", method, url); }
}

manapi::net::http_handler_page manapi::net::site::get_handler(http::request_data_t &request_data) const {
    http_handler_page handler_page;
    handler_page.error = std::make_unique<http_handler_page>();
    // how much we will take the layers from handler_page.layers at the start to the handler_page.error.layer
    size_t error_layer_depth = 0;
    bool not_found = false;
    try
    {
        const http_uri_part *cur = &handlers;
        const size_t path_size = request_data.divided == -1 ? request_data.path.size() : request_data.divided;
        for (size_t i = 0; i <= path_size; i++)
        {

            if (cur->statics != nullptr && cur->statics->contains(request_data.method))
            {
                handler_page.statics = &cur->statics->at(request_data.method);
                handler_page.statics_parts_len = i;
            }

            if (cur->layers != nullptr && cur->layers->contains(request_data.method))
            {
                handler_page.layer.push_back(&cur->layers->at(request_data.method));
            }

            if (cur->errors != nullptr && cur->errors->contains(request_data.method))
            {
                // find errors handlers for method!
                handler_page.error->handler = &cur->errors->at(request_data.method);
                error_layer_depth = handler_page.layer.size();
            }

            if (i == path_size)
            { break; }

            if (cur->map == nullptr || !cur->map->contains(request_data.path.at(i))) {
                if (cur->regexes != nullptr) {
                    std::smatch match;
                    bool find = false;

                    for (const auto &regex: *cur->regexes) {
                        // regex.first  <- regex string
                        // regex.second <- pair <regex, value (maybe next or handler)>

                        if (std::regex_match(request_data.path.at(i), match, regex.second.first)) {
                            cur     = regex.second.second.get();
                            find    = true;

                            if (cur->params == nullptr) {
                                // bug

                                MANAPIHTTP_LOG("{}", "cur->regexes_title (params) is null.");
                                return handler_page;
                            }

                            const size_t expected_size = match.size() - 1;
                            if (cur->params->size() != expected_size) {
                                // bug

                                MANAPIHTTP_LOG("The expected number of parameters ({}) does not correspond of reality ({}). uri part: {}.",
                                           cur->params->size(), expected_size, request_data.path.at(i));
                                return handler_page;
                            }

                            // get params
                            for (size_t z = 0; z < cur->params->size(); z++)
                            { request_data.params.insert({cur->params->at(z), match.str(z + 1)}); }


                            break;
                        }
                    }

                    if (find)
                    { continue; }
                }
                not_found = true;
                break;
            }

            cur = cur->map->at(request_data.path.at(i)).get();
        }

        std::copy_n(handler_page.layer.begin(), error_layer_depth, std::back_inserter(handler_page.error->layer));

        // handler page

        if (not_found || cur->handlers == nullptr) {
            return handler_page;
        }

        http_handler_functions *handler = &cur->handlers->at (request_data.method);

        if (handler == nullptr) {
            // TODO: handler error
        }

        handler_page.handler = handler;
        return std::move(handler_page);
    }
    catch (const std::exception &e)
    {
        return std::move(handler_page);
    }
}

manapi::net::site::site(std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool)
    : taskpool(taskpool), timerpool(timerpool), adding_watcher_mx(taskpool), cache_config_mx(taskpool) {}

manapi::net::site::~site() = default;

manapi::async_delay manapi::net::site::delay(const std::chrono::seconds &n) {
    return manapi::async_delay{*this->timerpool, n};
}


manapi::net::http_uri_part *manapi::net::site::set_handler(const std::string &method, const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    size_t  type            = URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    http_handler_functions functions;

    if (get_mask.is_enabled())
    {
        functions.get_mask = std::make_unique<json_mask> (get_mask);
    }

    if (post_mask.is_enabled())
    {
        functions.post_mask = std::make_unique<json_mask> (post_mask);
    }

    functions.handler = handler;

    switch (type) {
        case URI_PAGE_DEFAULT:
            if (cur->handlers == nullptr)
            {
                cur->handlers = std::make_unique<handlers_types_t> ();
            }
            check_exists_method_on_url(uri, cur->handlers, method);
            cur->handlers->insert({method, std::move(functions)});

            break;
        case URI_PAGE_ERROR:
            if (cur->errors == nullptr) {
                cur->errors = std::make_unique<handlers_types_t>();
            }
            check_exists_method_on_url(uri, cur->errors, method);
            cur->errors->insert({method, std::move(functions)});

            break;
        case URI_PAGE_LAYER:
            if (cur->layers == nullptr) {
                cur->layers = std::make_unique<handlers_types_t> ();
            }
            check_exists_method_on_url(uri, cur->layers, method);
            cur->layers->insert({method, std::move(functions)});

            break;
        default:
            return nullptr;
    }


    return cur;
}

manapi::net::http_uri_part *manapi::net::site::set_handler(const std::string &method, const std::string &uri, const std::string &folder) {
    size_t  type            = URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    switch (type) {
        case URI_PAGE_DEFAULT:
            if (cur->statics == nullptr) {
                cur->statics = std::make_unique<handlers_static_types_t> ();
            }

            check_exists_method_on_url(uri, cur->statics, method);
            cur->statics->insert({method, folder});

            break;
        default:
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_ADD_PAGE, "{}", "can not use the special pages with the static files");
    }


    return cur;
}

manapi::net::http_uri_part *manapi::net::site::build_uri_part(const std::string &uri, size_t &type)
{
    std::string                 buff;
    std::unique_ptr<handlers_regex_titles_t> regexes_title = nullptr;

    http_uri_part *cur      = &handlers;

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