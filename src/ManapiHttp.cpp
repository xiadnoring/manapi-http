#include <iostream>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>
#include "ManapiTaskFunction.h"
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiUtils.h"
#include "ManapiFilesystem.h"

#define MANAPI_HTTP_URI_PAGE_DEFAULT        0
#define MANAPI_HTTP_URI_PAGE_ERROR          1

std::string manapi::net::http::default_cache_dir        = "/tmp/";
std::string manapi::net::http::default_config_name      = "config.json";
bool        manapi::net::http::stopped_interrupt        = false;

std::vector <manapi::net::http *> manapi::net::http::running;

void handler_interrupt (int sig)
{
    manapi::net::http::stopped_interrupt = true;

    for (auto it = manapi::net::http::running.begin(); it != manapi::net::http::running.end(); )
    {
        (*it)->stop();
        it = manapi::net::http::running.erase(it);
    }

    if (sig == SIGFPE)
    {
        // TODO: MORE INFORMATION!!!
        exit (sig);
    }
}

manapi::net::http::~http() {
    destroy_uri_part (&handlers);
    MANAPI_LOG("destroy_uri_part() {}", "executed");
}

manapi::net::http::http() {
    setup ();
}

std::future<void> manapi::net::http::pool(const size_t &thread_num) {
    {
        m_running.lock();
        std::lock_guard <std::mutex> lk (m_initing);

        if (tasks_pool == nullptr)
        {
            tasks_pool = new threadpool<task>(thread_num);
            tasks_pool->start();
        }

        pool_promise = std::make_unique<std::promise<void>>();

        // init all pools
        if (config.contains("pools"))
        {
            for (auto it = config["pools"].begin<utils::json::ARRAY>(); it != config["pools"].end<utils::json::ARRAY>(); it++, next_pool_id++)
            {
                // TODO: ** <- resolve json iterator
                auto p = new http_pool (**it, this, next_pool_id);
                pools.insert({next_pool_id, p});
                p->run();
            }
        }

        std::thread stop_thread ([this] () -> void {
            utils::before_delete unwrap_stop_pool ([this] () -> void {
                pool_promise->set_value();
                m_running.unlock();
            });

            stop_pool();
        });
        stop_thread.detach();
    }

    return pool_promise->get_future();
}

void manapi::net::http::GET(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask) {
    this->set_handler("GET", uri, handler, false, get_mask);
}

void manapi::net::http::POST(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("POST", uri, handler, true, get_mask, post_mask);
}

void manapi::net::http::PUT(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("PUT", uri, handler);
}

void manapi::net::http::PATCH(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("PATCH", uri, handler);
}

void manapi::net::http::DELETE(const std::string &uri, const handler_template_t &handler) {
    this->set_handler("DELETE", uri, handler);
}

manapi::net::http_handler_page manapi::net::http::get_handler(request_data_t &request_data) const {
    http_handler_page handler_page;

    try
    {
        const http_uri_part *cur = &handlers;
        const size_t path_size = request_data.divided == -1 ? request_data.path.size() : request_data.divided;
        for (size_t i = 0; i <= path_size; i++)
        {
            if (cur->errors != nullptr && cur->errors->contains(request_data.method))
            {
                // find errors handlers for method!
                handler_page.error = &cur->errors->at(request_data.method);
            }

            if (cur->statics != nullptr && cur->statics->contains(request_data.method))
            {
                handler_page.statics = &cur->statics->at(request_data.method);
                handler_page.statics_parts_len = i;
            }

            if (i == path_size)
                break;

            if (cur->map == nullptr || !cur->map->contains(request_data.path.at(i))) {
                if (cur->regexes == nullptr)
                    return handler_page;

                std::smatch match;
                bool find = false;

                for (const auto &regex: *cur->regexes) {
                    // regex.first  <- regex string
                    // regex.second <- pair <regex, value (maybe next or handler)>

                    if (std::regex_match(request_data.path.at(i), match, regex.second.first)) {
                        cur     = regex.second.second;
                        find    = true;

                        if (cur->params == nullptr) {
                            // bug

                            MANAPI_LOG("{}", "cur->regexes_title (params) is null.");
                            return handler_page;
                        }

                        const size_t expected_size = match.size() - 1;
                        if (cur->params->size() != expected_size) {
                            // bug

                            MANAPI_LOG("The expected number of parameters ({}) does not correspond of reality ({}). uri part: {}.",
                                       cur->params->size(), expected_size, request_data.path.at(i));
                            return handler_page;
                        }

                        // get params
                        for (size_t z = 0; z < cur->params->size(); z++)
                            request_data.params.insert({cur->params->at(z), match.str(z + 1)});


                        break;
                    }
                }

                if (find)
                    continue;

                return handler_page;
            }

            cur = cur->map->at(request_data.path.at(i));
        }

        // handler page

        if (cur->handlers == nullptr) {
            // TODO: error

            return handler_page;
        }

        http_handler_functions *handler = &cur->handlers->at (request_data.method);

        if (handler == nullptr) {
            // TODO: handler error
        }

        handler_page.handler = handler;

        return handler_page;
    }
    catch (const std::exception &e)
    {
        return handler_page;
    }
}

manapi::net::http_uri_part *manapi::net::http::set_handler(const std::string &method, const std::string &uri, const handler_template_t &handler, bool has_body, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    size_t  type            = MANAPI_HTTP_URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    http_handler_functions functions;

    switch (type) {
        case MANAPI_HTTP_URI_PAGE_DEFAULT:
            if (cur->handlers == nullptr)
            {
                cur->handlers = new handlers_types_t ();
            }

            functions.handler = handler;
            cur->has_body = has_body;

            if (get_mask.is_enabled())
            {
                functions.get_mask = new utils::json_mask (get_mask);
            }

            if (post_mask.is_enabled())
            {
                functions.post_mask = new utils::json_mask (post_mask);
            }

            cur->handlers->insert({method, functions});

            break;
        case MANAPI_HTTP_URI_PAGE_ERROR:
            if (cur->errors == nullptr)
            {
                cur->errors = new handlers_types_t ();
            }

            functions.handler = handler;

            if (get_mask.is_enabled())
            {
                functions.get_mask = new utils::json_mask (get_mask);
            }

            if (post_mask.is_enabled())
            {
                functions.post_mask = new utils::json_mask (post_mask);
            }

            cur->errors->insert({method, functions});

            break;
        default:
            break;
    }

    return cur;
}

void manapi::net::http::GET(const std::string &uri, const std::string &folder) {
    set_handler ("GET", uri, folder, false);
}

manapi::net::http_uri_part *manapi::net::http::set_handler(const std::string &method, const std::string &uri, const std::string &folder, bool has_body) {
    size_t  type            = MANAPI_HTTP_URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    switch (type) {
        case MANAPI_HTTP_URI_PAGE_DEFAULT:
            if (cur->statics == nullptr)
                cur->statics = new handlers_static_types_t ();

            cur->statics->insert({method, folder});

            cur->has_body = has_body;

            break;
        default:
            throw manapi::utils::exception ("can not use the special pages with the static files");
    }

    return cur;
}

manapi::net::http_uri_part *manapi::net::http::build_uri_part(const std::string &uri, size_t &type)
{
    std::string                 buff;
    handlers_regex_titles_t     *regexes_title = nullptr;

    http_uri_part *cur      = &handlers;

    bool    is_regex        = false;

    // past params lists. To check for a match
    std::vector <std::vector <std::string> *> past_params_lists;

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
                            cur->regexes = new handlers_regex_map_t();
                        }

                        auto new_part   = new http_uri_part;

                        std::regex p(buff);
                        cur->regexes->insert({buff, std::make_pair(p, new_part)});

                        if (regexes_title != nullptr)
                        {
                            new_part->params = regexes_title;

                            // check for a match
                            for (const auto &past_params: past_params_lists)
                            {
                                for (const auto &param: *regexes_title)
                                {
                                    if (std::find(past_params->begin(), past_params->end(), param) !=
                                        past_params->end())
                                        MANAPI_LOG("Warning: a param with a title '{}' is already in use. ({})",
                                                   param, uri);
                                }
                            }

                            // save params list
                            past_params_lists.push_back(regexes_title);
                        }
                        //clean up
                        regexes_title   = nullptr;

                        cur             = new_part;
                    }
                    else
                    {
                        cur             = cur->regexes->at(buff).second;
                    }

                    is_regex        = false;
                }
                else
                {

                    if (buff[0] == '+' && is_last_part)
                    {
                        if (buff == "+error")
                        {
                            type = MANAPI_HTTP_URI_PAGE_ERROR;

                            break;
                        }

                        else
                        {
                            MANAPI_LOG("First char '{}' is reserved for special pages", '+');
                        }
                    }

                    if (cur->map == nullptr)
                    {
                        cur->map = new handlers_map_t();
                    }

                    if (!cur->map->contains(buff))
                    {
                        auto new_part       = new http_uri_part;

                        cur->map->insert({buff, new_part});

                        cur                 = new_part;
                    }
                    else
                    {
                        cur                 = cur->map->at(buff);
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

                else if (manapi::utils::escape_char_need(uri[i]))
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

                    buff = manapi::utils::escape_string(buff);
                }

                // if null -> create
                if (regexes_title == nullptr)
                    regexes_title = new handlers_regex_titles_t ();

                regexes_title->push_back(title);

                buff += "(.+)";
                continue;
            }

            i = temp;
        }

        if (is_regex)
        {
            if (manapi::utils::escape_char_need(uri[i]))
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


// ======================[ configs funcs]==========================

void manapi::net::http::set_compressor(const std::string &name, manapi::utils::compress::TEMPLATE_INTERFACE handler) {
    compressors[name] = handler;
}

manapi::utils::compress::TEMPLATE_INTERFACE manapi::net::http::get_compressor(const std::string &name) {
    if (!contains_compressor(name))
    {
        return nullptr;
    }

    return compressors.at(name);
}

bool manapi::net::http::contains_compressor(const std::string &name) const {
    return compressors.contains(name);
}

void manapi::net::http::setup() {
    SSL_library_init();

    signal  (SIGPIPE, SIG_IGN);

    //signal  (SIGFPE, handler_interrupt);
    signal  (SIGABRT, handler_interrupt);
    signal  (SIGKILL, handler_interrupt);
    signal  (SIGTERM, handler_interrupt);

    // fast ios
    // std::ios_base::sync_with_stdio(false);
    // std::cout.tie(nullptr);

    config                      = manapi::utils::json::object();
    cache_config                = manapi::utils::json::object();

    stopping = false;

    set_compressor("deflate", manapi::utils::compress::deflate);
    set_compressor("gzip", manapi::utils::compress::gzip);
}

void manapi::net::http::set_config(const std::string &path) {
    config_path = path;

    if (!manapi::filesystem::exists(config_path))
    {
        manapi::filesystem::config::write(config_path, config);
        return;
    }

    config = manapi::filesystem::config::read (config_path);

    setup_config ();
}

const manapi::utils::json &manapi::net::http::get_config() {
    return config;
}

void manapi::net::http::setup_config() {
    // =================[cache config]================= //
    if (config.contains("cache_dir"))
    {
        config_cache_dir = config["cache_dir"].get_ptr<std::string>();
    }
    else
    {
        config_cache_dir = &default_cache_dir;
    }

    manapi::filesystem::append_delimiter(*config_cache_dir);

    if (!manapi::filesystem::exists(*config_cache_dir))
    {
        manapi::filesystem::mkdir(*config_cache_dir);
    }
    else
    {
        std::string path = *config_cache_dir + default_config_name;
        if (manapi::filesystem::exists(path))
        {
            cache_config = manapi::filesystem::config::read(path);
        }
    }

    // =================[save config            ]================= //
    if (config.contains("save_config")) {
        if (config["save_config"].is_bool())
        {
            enabled_save_config = config["save_config"].get<bool>();
        }
    }
}

const std::string *manapi::net::http::get_compressed_cache_file(const std::string &file, const std::string &algorithm) const {
    if (!cache_config.contains(algorithm))
    {
        return nullptr;
    }


    auto files = &cache_config[algorithm];

    if (!files->contains(file))
    {
        return nullptr;
    }

    auto file_info = &files->at(file);

    if (*file_info->at("last-write").get_ptr<std::string>() == manapi::filesystem::last_time_write(file, true)) {
        auto compressed = file_info->at("compressed").get_ptr<std::string>();

        if (manapi::filesystem::exists(*compressed))
        {
            return compressed;
        }
    }

    files->erase(file);

    return nullptr;
}

void manapi::net::http::set_compressed_cache_file(const std::string &file, const std::string &compressed, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
    {
        cache_config.insert(algorithm, manapi::utils::json::object());
    }

    manapi::utils::json file_info = manapi::utils::json::object();

    file_info.insert("last-write", manapi::filesystem::last_time_write(file, true));
    file_info.insert("compressed", compressed);

    cache_config[algorithm].insert(file, file_info);
}

void manapi::net::http::save() {
    // close connections

    if (enabled_save_config)
    {
        // save configs
        save_config();
    }
}

void manapi::net::http::save_config() {
    // main config
    manapi::filesystem::config::write(config_path, config);

    // cache config
    manapi::filesystem::config::write(*config_cache_dir + default_config_name, cache_config);
}

void manapi::net::http::stop(bool wait) {
    {
        std::lock_guard <std::mutex> lk (m_initing);
        stopping = true;
    }
    {
        cv_stopping.notify_one();

        if (wait)
        {
            std::lock_guard <std::mutex> lk (m_running);
        }
    }
}

manapi::net::threadpool<manapi::net::task> *manapi::net::http::get_tasks_pool() const {
    return tasks_pool;
}

void manapi::net::http::append_task(task *t, bool important) {
    if (tasks_pool == nullptr)
    {
        THROW_MANAPI_EXCEPTION("{}", "tasks_pool = nullptr");
    }

    tasks_pool->append_task(t, important);
}

void manapi::net::http::destroy_uri_part(http_uri_part *cur) {
    // clean up errors
    if (cur->errors != nullptr)
    {
        for (const auto &handler : *cur->errors)
        {
            delete handler.second.get_mask;
            delete handler.second.post_mask;
        }
    }

    // clean up handlers
    if (cur->handlers != nullptr)
    {
        for (const auto &handler: *cur->handlers)
        {
            delete handler.second.get_mask;
            delete handler.second.post_mask;
        }
    }

    // clean up params, static pages, regexes
    delete cur->params;
    delete cur->statics;
    delete cur->regexes;

    // clean up map (next part of the path)
    if (cur->map != nullptr)
    {
        for (const auto &handler: *cur->map)
        {
            destroy_uri_part(handler.second);
        }
    }
}

void manapi::net::http::stop_pool() {
    std::unique_lock <std::mutex> lk (m_stopping);

    http::running.push_back(this);

    cv_stopping.wait(lk, [this] () -> bool { return stopping; });

    ////////////////////////
    //////// Stopping //////
    ////////////////////////

    if (tasks_pool != nullptr)
    {
        // stop tasks
        tasks_pool->stop();

        while (!tasks_pool->all_tasks_stopped())
        {
            sched_yield();
        }

    }

    // stop all pools
    for (const auto &pool: pools)
    {
        MANAPI_LOG ("pool #{} is stopping...", pool.first);
        pool.second->stop();
        delete pool.second;
        MANAPI_LOG ("pool #{} stopped successfully", pool.first);
    }

    pools.clear();
    next_pool_id = 0;
    stopping = false;

    this->save();

    if (tasks_pool != nullptr)
    {
        // clean tasks pool
        delete tasks_pool;
        tasks_pool = nullptr;
    }

    // do we need to delete it?
    if (!http::stopped_interrupt)
    {
        for (auto it = http::running.begin(); it != http::running.end(); it++)
        {
            if (*it == this)
            {
                http::running.erase(it);
                break;
            }
        }
    }

    MANAPI_LOG("{}", "all tasks are closed");
}
