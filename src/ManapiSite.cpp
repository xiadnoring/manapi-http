#include <openssl/ssl.h>

#include "ManapiFilesystem.hpp"
#include "ManapiSite.hpp"
#include "ManapiThreadPool.hpp"

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

void manapi::net::site::set_compressor(const std::string &name, manapi::utils::compress::TEMPLATE_INTERFACE handler) {
    compressors[name] = handler;
}

manapi::utils::compress::TEMPLATE_INTERFACE manapi::net::site::get_compressor(const std::string &name) {
    if (!contains_compressor(name))
    {
        return nullptr;
    }

    return compressors.at(name);
}

bool manapi::net::site::contains_compressor(const std::string &name) const {
    return compressors.contains(name);
}

void manapi::net::site::setup() {
    SSL_library_init();

    // fast ios
    // std::ios_base::sync_with_stdio(false);
    // std::cout.tie(nullptr);

    config                      = manapi::utils::json::object();
    cache_config                = manapi::utils::json::object();

    set_compressor("deflate", manapi::utils::compress::deflate);
    set_compressor("gzip", manapi::utils::compress::gzip);
}

void manapi::net::site::timer_pool_setup(threadpool<task> *tasks_pool) {
    timerpool = std::make_unique<utils::timerpool>(*tasks_pool, 5);
    timerpool->to_delete = false;
    tasks_pool->append_task(timerpool.get());
}

void manapi::net::site::timer_pool_stop() {
    timerpool->stop();
}

void manapi::net::site::set_config(const std::string &path) {
    config_path = path;

    if (!manapi::filesystem::exists(config_path))
    {
        manapi::filesystem::config::write(config_path, config);
        return;
    }

    config = manapi::filesystem::config::read (config_path);
    setup_config ();
}

void manapi::net::site::set_config_object(const utils::json &config) {
    this->config = config;
    setup_config();
}

const manapi::utils::json &manapi::net::site::get_config() {
    return config;
}

void manapi::net::site::setup_config() {
    // =================[cache config]================= //
    if (config.contains("cache_dir"))
    {
        config_cache_dir = &config["cache_dir"].get<std::string>();
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

const std::string *manapi::net::site::get_compressed_cache_file(const std::string &file, const std::string &algorithm) const {
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

void manapi::net::site::set_compressed_cache_file(const std::string &file, const std::string &compressed, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
    {
        cache_config.insert(algorithm, manapi::utils::json::object());
    }

    manapi::utils::json file_info = manapi::utils::json::object();

    file_info.insert("last-write", manapi::filesystem::last_time_write(file, true));
    file_info.insert("compressed", compressed);

    cache_config[algorithm].insert(file, file_info);
}

const std::unique_ptr<manapi::net::threadpool<manapi::net::task>> & manapi::net::site::get_tasks_pool() const {
    return tasks_pool;
}

void manapi::net::site::tasks_pool_stop() {
    if (tasks_pool != nullptr)
    {
        // stop tasks
        tasks_pool->stop();

        while (!tasks_pool->all_tasks_stopped())
        {
            sched_yield();
        }
    }
}

void manapi::net::site::tasks_pool_init(const size_t &thread_num) {
    if (tasks_pool == nullptr)
    {
        tasks_pool = std::make_unique<threadpool<task> >(thread_num);
    }
    tasks_pool->start();
}

void manapi::net::site::save() {
    // close connections

    if (enabled_save_config)
    {
        // save configs
        save_config();
    }
}

void manapi::net::site::save_config() {
    if (!manapi::filesystem::exists(config_path) && manapi::filesystem::is_file(config_path)) {
        // main config
        manapi::filesystem::config::write(config_path, config);
    }
    // cache config
    manapi::filesystem::config::write(*config_cache_dir + default_config_name, cache_config);
}

void manapi::net::site::check_exists_method_on_url(const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPI_EXCEPTION("The method {} already contains in the url {}", method, url); }
}

void manapi::net::site::check_exists_method_on_url(const std::string &url,
    const std::unique_ptr<handlers_static_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPI_EXCEPTION("The method {} already contains in the static url {}", method, url); }
}

manapi::net::http_handler_page manapi::net::site::get_handler(request_data_t &request_data) const {
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

            if (cur->layers != nullptr && cur->layers->contains(request_data.method))
            {
                handler_page.layer.push_back(&cur->layers->at(request_data.method));
            }

            if (i == path_size)
            { break; }

            if (cur->map == nullptr || !cur->map->contains(request_data.path.at(i))) {
                if (cur->regexes == nullptr)
                { return handler_page; }

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
                        { request_data.params.insert({cur->params->at(z), match.str(z + 1)}); }


                        break;
                    }
                }

                if (find)
                { continue; }

                return handler_page;
            }

            cur = cur->map->at(request_data.path.at(i)).get();
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

manapi::net::site::site() = default;

manapi::net::site::~site() = default;

void manapi::net::site::append_task(task *t, const int &level) {
    tasks_pool->append_task(t, level);
}

size_t manapi::net::site::append_timer(const std::chrono::milliseconds &duration, const std::function<void()> &task) {
    return timerpool->append_timer(duration, task);
}

void manapi::net::site::remove_timer(const size_t &id) {
    timerpool->remove_timer(id);
}

manapi::net::http_uri_part *manapi::net::site::set_handler(const std::string &method, const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    size_t  type            = URI_PAGE_DEFAULT;

    http_uri_part *cur      = build_uri_part(uri, type);

    http_handler_functions functions;

    if (get_mask.is_enabled())
    {
        functions.get_mask = std::make_unique<utils::json_mask> (get_mask);
    }

    if (post_mask.is_enabled())
    {
        functions.post_mask = std::make_unique<utils::json_mask> (post_mask);
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
            THROW_MANAPI_EXCEPTION("{}", "can not use the special pages with the static files");
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
                                        MANAPI_LOG("Warning: a param with a title '{}' is already in use. ({})",
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
                        else { MANAPI_LOG("The first char '{}' is reserved for special pages in {}", '+', buff); }

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

                if (manapi::utils::escape_char_need(uri[i]))
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