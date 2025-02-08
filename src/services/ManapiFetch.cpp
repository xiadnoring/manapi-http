#include <exception>

#include "ManapiHttp.hpp"
#include "services/ManapiFetch.hpp"

#include "async/ManapiAsyncPromise.hpp"

// Utils

struct curl_data_t {
    manapi::net::fetch *fetch;
    std::atomic<ssize_t> &total_write;
};

size_t manapi::net::fetch::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
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
    static_cast <curl_data_t *> (userdata)->fetch->headers.insert(std::move(header));

    return n_items * size;
}

size_t manapi::net::fetch::curl_write_handler (char *buffer, size_t size, size_t n_mem_b, void *user_p)
{
    auto &f = *static_cast <curl_data_t *> (user_p);
    const auto result = static_cast<ssize_t>(size * n_mem_b);
    f.total_write.fetch_add(result);
    // call user handler
    return static_cast<size_t>(f.fetch->handler_body (buffer, result));
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


manapi::net::curlformdata::curlformdata() = default;

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

void manapi::net::curlformdata::clear() {
    this->mdata.clear();
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::begin() {
    return this->mdata.begin();
}

manapi::net::curlformdata::tdata::iterator manapi::net::curlformdata::end() {
    return this->mdata.end();
}


manapi::net::fetch::fetch(const std::shared_ptr<async::context> &ctx, const std::string &url) : ctx(ctx), async_run(ctx) {
    this->url = url;
    this->method = "GET";
    this->curl.reset(curl_easy_init());
}

manapi::net::fetch::fetch(fetch &&n) noexcept : async_run(n.async_run) {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() = default;

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    this->ctx = std::move(n.ctx);
    this->status_code = std::exchange(n.status_code, 200);
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
    this->headers = std::move(n.headers);

    return *this;
}

manapi::future<void> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (!this->curl)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url);
    }

    auto clear_ = before_delete ([this] ()
        -> void { this->clear(); });

    curl_data_t data {
        .fetch = this,
        .total_write = this->total_write,
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
    switch (this->body) {
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


    /* wait all jobs */
    co_await this->async_run.get();

    if (resp != CURLE_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->url, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    if (!this->headers.empty()) {
        if (this->async_handler_headers) {
            co_await this->async_handler_headers(std::move(this->headers));
        }
        if (this->handler_headers) {
            this->handler_headers (std::move(this->headers));
        }
    }

    curl_easy_getinfo(this->curl.get(), CURLINFO_HTTP_CODE, &this->status_code);
}

std::map <std::string, std::string> manapi::net::fetch::get_headers() {
    return std::move(this->headers);
}

void manapi::net::fetch::clear() {
    this->headers.clear();
    this->curl.reset(curl_easy_init());
    this->curl_headers.reset();
    this->async_buffer.clear();
    this->async_buffer_cursor = 0;
    this->attempts = 5;
    this->attempt_delay = std::chrono::milliseconds(200);
    this->method = "GET";
    this->total_read.store(0);
    this->total_write.store(0);
    this->url.clear();
    this->status_code=200;
    this->body = BODY_NONE;
    this->body_formdata.clear();
    this->body_default.clear();
    this->handler_body=nullptr;
    this->async_handler_body=nullptr;
    this->handle_custom_setup=nullptr;
    this->handler_headers=nullptr;
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    ssize_t total_read_prev = 0;
    ssize_t total_write_prev = 0;
    ssize_t again = 10;

    manapi::async::parallel_run<void> unwatch_curl_action (this->ctx);

    size_t timeout_token = co_await this->ctx->timerpool()->async_append_interval_sync(200, [&] () -> void {
        if (again >= 0 && this->total_read - total_read_prev + this->total_write - total_write_prev < 8 * 1024) {
            if (!(again--)) {
                unwatch_curl_action.run(this->ctx->eventloop()->unwatch_curl(this->curl.get()));
            }
        }
    });

    CURLcode res;

    try {
        res = co_await async::promise<CURLcode> (this->ctx->eventloop()->get_task_pool(), [this] (async::promise<CURLcode>::resolve_t resolve, async::promise<CURLcode>::reject_t reject) -> future<> {
            try {
                co_await this->ctx->eventloop()->watch_curl(this->curl.get(), [resolve = std::move(resolve)] (CURLcode result)
                    -> void { resolve (result); });
            }
            catch (...) {
                reject (std::current_exception());
            }
        });


        if (this->async_handler_body && this->async_buffer_cursor) {
            co_await this->async_handler_body(true);
        }
    }
    catch (...) {
        res = CURLE_AGAIN;
    }

    co_await this->ctx->timerpool()->async_remove_timer(timeout_token);
    co_await unwatch_curl_action.get ();

    co_return res;
}

void manapi::net::fetch::handle_body(std::function<ssize_t(char *, ssize_t)> handler) {
    if (this->async_handler_body) { this->async_handler_body = nullptr; }

    if (this->async_handler_headers) {
        this->async_handler_body = [this, handler = std::move(handler)] (bool finish) mutable -> manapi::future<> {
            if (!co_await this->async_handler_headers(std::move(this->headers))) {
                co_await this->ctx->eventloop()->unwatch_curl(this->curl.get());
                co_return;
            }

            this->handler_body = std::move(handler);

            if (finish) {
                co_return;
            }

            auto curl = this->curl.get();
            auto eventloop = this->ctx->eventloop();
            this->async_handler_body = nullptr;

            co_await eventloop->unpause_watch_curl(curl);
        };

        this->handler_body = [this] (char *buffer, size_t buffer_size) -> ssize_t {
            /* in the loop event */
            async::run(this->ctx->eventloop()->get_task_pool(), this->ctx->eventloop()->custom_cb_curl(this->curl.get(), [this] (CURLcode code)
                -> void { this->async_run.run(this->async_handler_body(false)); }));

            return CURL_WRITEFUNC_PAUSE;
        };
    }
    else {
        this->handler_body = std::move(handler);
    }
}

manapi::future<std::string> manapi::net::fetch::text() {
    std::string content;

    handle_body ([&content](char *buffer, size_t size) {
        content.append(buffer, size);

        return size;
    });

    co_await async_doit();

    co_return content;
}

manapi::future<manapi::json> manapi::net::fetch::json() {
    co_return std::move(manapi::json (co_await text(), true));
}


void manapi::net::fetch::handle_async_body(std::function<manapi::future<ssize_t>(char *, ssize_t)> handler) {
    this->async_buffer.resize(65536);

    std::function<future<>(bool finish)> async_write_cb = [this, handler = std::move(handler)] (bool finish) -> manapi::future<> {
        ssize_t total = 0;

        while (total < this->async_buffer_cursor) {
            const auto rhs = co_await handler (this->async_buffer.data() + total, this->async_buffer_cursor - total);
            if (rhs < 0) {
                if (finish) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "handle_async_body(...): Write handler error : {}", rhs);
                }

                this->async_buffer_cursor = 0;
                co_await this->ctx->eventloop()->unwatch_curl(this->curl.get());

                co_return;
            }
            total += rhs;
        }

        this->async_buffer_cursor = 0;

        if (finish) {
            co_return;
        }

        co_await this->ctx->eventloop()->unpause_watch_curl(this->curl.get());
    };

    this->async_handler_body = [this, async_write_cb = std::move(async_write_cb)] (bool finish) mutable -> manapi::future<> {
        if (this->async_handler_headers) {
            co_await this->async_handler_headers (std::move(this->headers));
        }
        if (this->handler_headers) {
            this->handler_headers (std::move(this->headers));
        }

        this->async_handler_body = std::move(async_write_cb);
        co_await this->async_handler_body(finish);
    };

    this->handler_body = [this] (char *buffer, ssize_t size) -> ssize_t {
        if (this->async_buffer_cursor < this->async_buffer.size()
            && this->async_buffer.size() - this->async_buffer_cursor >= size) {
            const auto copy = size;
            memcpy(this->async_buffer.data() + this->async_buffer_cursor, buffer, copy);
            this->async_buffer_cursor += copy;

            return copy;
        }

        /* in the loop event */
        async::run(this->ctx->eventloop()->get_task_pool(), this->ctx->eventloop()->custom_cb_curl(this->curl.get(), [this] (CURLcode code)
            -> void { this->async_run.run(this->async_handler_body(false)); }));

        return CURL_WRITEFUNC_PAUSE;
    };
}

void manapi::net::fetch::handle_headers(std::function<bool(std::map <std::string, std::string>)> handler) {
    if (this->async_handler_headers) { this->async_handler_headers = {nullptr}; }
    this->handler_headers = std::move(handler);
}

void manapi::net::fetch::handle_async_headers(std::function<manapi::future<bool>(std::map<std::string, std::string>)> handler) {
    if (this->handler_headers) { this->handler_headers = {nullptr}; }
    this->async_handler_headers = std::move(handler);

    /* if we will be use 'sync body parse' then we must to make it async for one time */
    if (this->handler_body && !this->async_handler_body) {
        /* rebuild sync parser */
        this->handle_body(std::move(this->handler_body));
    }
}

void manapi::net::fetch::enable_alpn(bool status) {
    curl_easy_setopt(this->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
}

void manapi::net::fetch::enable_http3() {
    curl_easy_setopt(this->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
}

void manapi::net::fetch::enable_http2() {
    curl_easy_setopt(this->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
}

void manapi::net::fetch::enable_http1_1() {
    curl_easy_setopt(this->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
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

void manapi::net::fetch::set_headers(std::map<std::string, std::string> headers) {
    // headers
    for (auto &header: headers)
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

