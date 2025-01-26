#include <exception>

#include "ManapiHttp.hpp"
#include "services/ManapiFetch.hpp"

#include "async/ManapiAsyncPromise.hpp"

// Utils

struct curl_data_t {
    std::function <size_t(char *, const size_t&)> handler_body;
    std::function <void(const std::map <std::string, std::string>&)> handler_headers;
    std::map <std::string, std::string> *headers;
    bool first_chunk_body;
};

size_t curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    std::string str (buffer, size * n_items);

    // delete \r\n at the end of the header
    if (str.size() >= 2)
    {
        // \n
        str.pop_back();
        // \r
        str.pop_back();
    }

    auto header = manapi::net::http::parse_header(str);
    static_cast <curl_data_t *> (userdata)->headers->insert(std::move(header));

    return n_items * size;
}

size_t curl_write_handler (char *buffer, size_t size, size_t n_mem_b, void *user_p)
{
    // check if it is the first chunk
    if (static_cast <curl_data_t *> (user_p)->first_chunk_body) {
        static_cast <curl_data_t *> (user_p)->first_chunk_body = false;
        if (static_cast <curl_data_t *> (user_p)->handler_headers)
        {
            static_cast <curl_data_t *> (user_p)->handler_headers (*static_cast <curl_data_t *> (user_p)->headers);
        }
    }
    // call user handler
    return static_cast <curl_data_t *> (user_p)->handler_body (buffer, size * n_mem_b);
}

size_t curl_send_formdata_cb_read (char *buffer, size_t size, size_t nitems, void *userp) {
    auto &func = *static_cast<decltype(manapi::net::curlformdata::multipart_param_value_file::callback) *> (userp);
    return func (buffer, size * nitems);
}

int curl_send_formdata_cb_seek (void *userp, curl_off_t offset, int origin) {
    return CURL_SEEKFUNC_OK;
}

void curl_send_formdata_cb_free (void *userp) {
    // pass
}


manapi::net::curlformdata::curlformdata() {}

manapi::net::curlformdata::~curlformdata() = default;

manapi::net::curlformdata::curlformdata(curlformdata &&fd) noexcept : mdata(std::move(fd.mdata)) {}

manapi::net::curlformdata & manapi::net::curlformdata::operator=(curlformdata &&fd) noexcept {
    this->mdata = std::move(fd.mdata);
    return *this;
}

void manapi::net::curlformdata::setdata(const std::string &name, std::string value) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new std::string(std::move(value))), [] (void *ptr) -> void {
        delete static_cast <std::string *> (ptr);
    });
    this->mdata.insert({name, {.data = std::move(udata), .type = PARAM_DEFAULT}});
}

void manapi::net::curlformdata::setfile(const std::string &filename, std::string filepath) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new std::string(std::move(filepath))), [] (void *ptr) -> void {
        delete static_cast <std::string *> (ptr);
    });
    this->mdata.insert({filename, {.data = std::move(udata), .type = PARAM_FILE}});
}

void manapi::net::curlformdata::setcallback(const std::string &name, const long long &size, const std::function<size_t(void *buff, size_t buff_size)> &cb) {
    std::unique_ptr<void, void (*)(void*)> udata (reinterpret_cast<void *> (new multipart_param_value_file({cb, size})), [] (void *ptr) -> void {
        delete static_cast <multipart_param_value_file *> (ptr);
    });
    this->mdata.insert({name, {.data = std::move(udata), .type = PARAM_CALLBACK}});
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::begin() {
    return this->mdata.begin();
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::end() {
    return this->mdata.end();
}


manapi::net::fetch::fetch(const std::shared_ptr<async::context> &ctx, const std::string &url) : event(ctx->eventloop()), timerpool(ctx->timerpool()) {
    this->url = url;
    this->method = "GET";
    this->curl.reset(curl_easy_init());
}

manapi::net::fetch::fetch(fetch &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() = default;

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    this->event = std::move(n.event);
    this->timerpool = std::move(n.timerpool);
    this->status_code = std::exchange(n.status_code, 200);
    this->headers_list = std::move(n.headers_list);
    this->url = std::move(n.url);
    this->handle_custom_setup = std::move(n.handle_custom_setup);
    this->handler_body = std::move(n.handler_body);
    this->handler_headers = std::move(n.handler_headers);
    this->body = std::exchange(n.body, BODY_NONE);
    this->body_default = std::move(n.body_default);
    this->method = std::move(n.method);
    this->body_formdata = std::move(n.body_formdata);
    this->curl = std::move(n.curl);
    this->curl_headers = std::move(n.curl_headers);

    return *this;
}

manapi::future<void> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (!this->curl)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url);
    }

    curl_data_t data {
        .handler_body = handler_body,
        .handler_headers = handler_headers,
        .headers = &headers_list,
        .first_chunk_body = true
    };

    CURLcode resp;

    curl_easy_setopt(this->curl.get(), CURLOPT_URL, url.data());
    curl_easy_setopt(this->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler);
    curl_easy_setopt(this->curl.get(), CURLOPT_HEADERDATA, &data);

    // if handler has been set
    if (this->handler_body)
    {
        curl_easy_setopt(this->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler);
        curl_easy_setopt(this->curl.get(), CURLOPT_WRITEDATA, &data);
    }

    if (this->curl_headers)
    {
        curl_easy_setopt(this->curl.get(), CURLOPT_HTTPHEADER, this->curl_headers.get());
    }

    curl_easy_setopt(this->curl.get(), CURLOPT_CUSTOMREQUEST, this->method.data());

    if (this->handle_custom_setup)
    {
        this->handle_custom_setup (this->curl.get());
    }

    // body of the request
    switch (body) {
        case BODY_PLAIN: {
            curl_easy_setopt(this->curl.get(), CURLOPT_POSTFIELDS, this->body_default.data());
            curl_easy_setopt(this->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->body_default.size());
            break;
        }
        case BODY_MULTIPART: {
            form.reset(curl_mime_init(this->curl.get()));
            curl_mimepart *field = nullptr;
            for (auto &param : this->body_formdata) {
                field = curl_mime_addpart(form.get());
                curl_mime_name(field, param.first.data());
                switch (param.second.type) {
                    case curlformdata::PARAM_DEFAULT: {
                        auto &strdata = *static_cast <std::string *> (param.second.data.get());
                        curl_mime_data(field, strdata.data(), strdata.size());
                        break;
                    }
                    case curlformdata::PARAM_FILE: {
                        auto &strdata = *static_cast <std::string *> (param.second.data.get());
                        curl_mime_filedata(field, strdata.data());
                        break;
                    }
                    case curlformdata::PARAM_CALLBACK: {
                        auto &cbdata = *static_cast <curlformdata::multipart_param_value_file *> (param.second.data.get());
                        curl_mime_data_cb(field, cbdata.filesize, curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback);
                        break;
                    }
                }
            }
            curl_easy_setopt(this->curl.get(), CURLOPT_MIMEPOST, form.get());
            break;
        }
        default:
            break;
    }

    try {
        resp = co_await async_curl_perform();
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("Exception: {}", e.what());
        resp = CURLE_AGAIN;
    }

    if (resp != CURLE_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->url, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    if (std::exchange(data.first_chunk_body, false) && data.handler_headers) {
        data.handler_headers (this->headers_list);
    }

    curl_easy_getinfo(this->curl.get(), CURLINFO_HTTP_CODE, &this->status_code);
}

std::map <std::string, std::string> manapi::net::fetch::get_headers() {
    return std::move(this->headers_list);
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    this->timeout_token = std::make_shared<size_t>(0);

    *this->timeout_token = co_await this->timerpool->async_append_timer_async(std::chrono::milliseconds(500), [this] () -> manapi::future<> {
        return this->event->unwatch_curl(this->curl.get());
    }, this->timeout_token);

    CURLcode res;

    try {
        res = co_await async::promise<CURLcode> (this->event->get_task_pool(), [this] (async::promise<CURLcode>::resolve_t resolve, async::promise<CURLcode>::reject_t reject) -> future<> {
            try {
                co_await this->event->watch_curl(this->curl.get(), [resolve = std::move(resolve)] (CURLcode result)
                    -> void { resolve (result); });
            }
            catch (...) {
                reject (std::current_exception());
            }
        });
    }
    catch (...) {
        res = CURLE_AGAIN;
    }

    co_await this->timerpool->async_remove_timer(*this->timeout_token);

    co_return res;
}

void manapi::net::fetch::handle_body(const std::function<size_t(char *, const size_t&)> &_handler) {
    this->handler_body = _handler;
}

manapi::future<std::string> manapi::net::fetch::text() {
    std::string content;

    handle_body ([&content](char *buffer, const size_t &size) {
        content.append(buffer, size);

        return size;
    });

    co_await async_doit();

    co_return content;
}

manapi::future<manapi::json> manapi::net::fetch::json() {
    co_return std::move(manapi::json (co_await text(), true));
}


void manapi::net::fetch::handle_headers(const std::function<void(const std::map <std::string, std::string> &)> &_handler) {
    this->handler_headers = _handler;
}

void manapi::net::fetch::set_body(curlformdata params) {
    this->body = BODY_MULTIPART;
    this->body_formdata = std::move(params);
}


void manapi::net::fetch::set_method(std::string method) {
    this->method = std::move(method);
}

void manapi::net::fetch::set_body(std::string data) {
    this->body = BODY_PLAIN;
    this->body_default = std::move(data);
}

void manapi::net::fetch::set_headers(const std::map<std::string, std::string> &headers) {
    // headers
    for (const auto &header: headers)
    {
        this->curl_headers.reset(curl_slist_append(this->curl_headers.release(), manapi::net::http::stringify_header(header).data()));
    }
}

void manapi::net::fetch::set_custom_setup(const std::function<void(CURL *curl)> &func) {
    this->handle_custom_setup = func;
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    auto lstatus = static_cast<long> (status);
    curl_easy_setopt(this->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    curl_easy_setopt(this->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    curl_easy_setopt(this->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
}

void manapi::net::fetch::set_verbose(bool status) {
    curl_easy_setopt(this->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
}

void manapi::net::fetch::break_write_loop() {
    curl_easy_pause(this->curl.get(), CURLPAUSE_RECV);
}

void manapi::net::fetch::continue_write_loop() {
    curl_easy_pause(this->curl.get(), CURLPAUSE_CONT);
}

size_t manapi::net::fetch::get_status_code() const {
    return this->status_code;
}

