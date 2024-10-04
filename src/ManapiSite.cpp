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

void manapi::net::site::set_compressor(const std::string &name, manapi::net::utils::compress::TEMPLATE_INTERFACE handler) {
    compressors[name] = handler;
}

manapi::net::utils::compress::TEMPLATE_INTERFACE manapi::net::site::get_compressor(const std::string &name) {
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

    config                      = manapi::json::object();
    cache_config                = manapi::json::object();

    set_compressor("deflate", manapi::net::utils::compress::deflate);
    set_compressor("gzip", manapi::net::utils::compress::gzip);
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

    if (!manapi::net::filesystem::exists(config_path))
    {
        manapi::net::filesystem::config::write(config_path, config);
        return;
    }

    config = manapi::net::filesystem::config::read (config_path);
    setup_config ();
}

void manapi::net::site::set_config_object(const json &config) {
    this->config = config;
    setup_config();
}

const manapi::json &manapi::net::site::get_config() {
    return config;
}

void manapi::net::site::setup_config() {
    // =================[cache config]================= //
    if (config.contains("cache_dir"))
    {
        const auto b = config.at("cache_dir");
        config_cache_dir = b.as_string();
    }
    else
    {
        config_cache_dir = default_cache_dir;
    }

    manapi::net::filesystem::append_delimiter(config_cache_dir);

    if (!manapi::net::filesystem::exists(config_cache_dir))
    {
        manapi::net::filesystem::mkdir(config_cache_dir);
    }
    else
    {
        std::string path = config_cache_dir + default_config_name;
        if (manapi::net::filesystem::exists(path))
        {
            cache_config = manapi::net::filesystem::config::read(path);
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

const std::string *manapi::net::site::get_compressed_cache_file(const std::string &file, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
    {
        return nullptr;
    }


    auto &files = cache_config[algorithm];

    if (!files.contains(file))
    {
        return nullptr;
    }

    auto &file_info = files[file];

    if (file_info.at("last-write").get<std::string>() == manapi::net::filesystem::last_time_write(file, true)) {
        auto &compressed = file_info.at("compressed").get<std::string>();

        if (manapi::net::filesystem::exists(compressed))
        {
            return &compressed;
        }
    }

    files.erase(file);

    return nullptr;
}

void manapi::net::site::set_compressed_cache_file(const std::string &file, const std::string &compressed, const std::string &algorithm) {
    if (!cache_config.contains(algorithm))
    {
        cache_config.insert(algorithm, manapi::json::object());
    }

    manapi::json file_info = manapi::json::object();

    file_info.insert("last-write", manapi::net::filesystem::last_time_write(file, true));
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
    if (!manapi::net::filesystem::exists(config_path) && manapi::net::filesystem::is_file(config_path)) {
        // main config
        manapi::net::filesystem::config::write(config_path, config);
    }
    // cache config
    manapi::net::filesystem::config::write(config_cache_dir + default_config_name, cache_config);
}

void manapi::net::site::check_exists_method_on_url(const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPI_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the url {}", method, url); }
}

void manapi::net::site::check_exists_method_on_url(const std::string &url,
    const std::unique_ptr<handlers_static_types_t> &m, const std::string &method) {
    if (m->contains((method))) { THROW_MANAPI_EXCEPTION(ERR_HTTP_ADD_PAGE, "The method {} already contains in the static url {}", method, url); }
}

manapi::net::http_handler_page manapi::net::site::get_handler(request_data_t &request_data) const {
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
            THROW_MANAPI_EXCEPTION(ERR_HTTP_ADD_PAGE, "{}", "can not use the special pages with the static files");
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

                if (manapi::net::utils::escape_char_need(uri[i]))
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

                    buff = manapi::net::utils::escape_string(buff);
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
            if (manapi::net::utils::escape_char_need(uri[i]))
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