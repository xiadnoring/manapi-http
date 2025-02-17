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
    static_cast <curl_data_t *> (userdata)->fetch->data->headers.insert(std::move(header));

    return n_items * size;
}

size_t manapi::net::fetch::curl_write_handler (char *buffer, size_t size, size_t n_mem_b, void *user_p)
{
    auto &f = *static_cast <curl_data_t *> (user_p);
    const auto result = static_cast<ssize_t>(size * n_mem_b);
    f.total_write.fetch_add(result);
    // call user handler
    return static_cast<size_t>(f.fetch->data->handler_body (buffer, result));
}

void manapi::net::fetch::setup_parallel_task() {
    this->data->parallel_task = [this] (CURLcode code) -> void {
        auto data = this->data;
        curl_easy_getinfo(this->data->curl.get(), CURLINFO_HTTP_CODE, &this->status_code);
        data->parallel_task = [data] (CURLcode code) -> void {
            async::run(data->ctx, data->async_handler_body(data, false));
        };

        data->parallel_task.value()(code);
    };
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


manapi::net::fetch::fetch(const std::shared_ptr<async::context> &ctx, const std::string &url) {
    this->data = std::make_shared<shared_data>(ctx);
    this->url = url;
    this->method = "GET";
    this->data->ctx = ctx;
    this->data->curl.reset(curl_easy_init());
}

manapi::net::fetch::fetch(fetch &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() = default;

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    this->data = std::move(n.data);
    this->status_code = std::exchange(n.status_code, 200);
    this->url = std::move(n.url);
    this->body = std::exchange(n.body, BODY_NONE);
    this->body_default = std::move(n.body_default);
    this->method = std::move(n.method);
    this->body_formdata = std::move(n.body_formdata);

    return *this;
}

manapi::future<void> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (!this->data->curl)
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

    curl_easy_setopt(this->data->curl.get(), CURLOPT_URL, url.data());
    curl_easy_setopt(this->data->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler);
    curl_easy_setopt(this->data->curl.get(), CURLOPT_HEADERDATA, &data);

    // if handler has been set
    if (this->data->handler_body)
    {
        curl_easy_setopt(this->data->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler);
        curl_easy_setopt(this->data->curl.get(), CURLOPT_WRITEDATA, &data);
    }

    if (this->data->curl_headers)
    {
        curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTPHEADER, this->data->curl_headers.get());
    }

    curl_easy_setopt(this->data->curl.get(), CURLOPT_CUSTOMREQUEST, this->method.data());

    if (this->data->handle_custom_setup)
    {
        this->data->handle_custom_setup (this->data->curl.get());
    }

    // body of the request
    switch (this->body) {
        case BODY_PLAIN: {
            curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDS, this->body_default.data());
            curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->body_default.size());
            break;
        }
        case BODY_MULTIPART: {
            form.reset(curl_mime_init(this->data->curl.get()));
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
            curl_easy_setopt(this->data->curl.get(), CURLOPT_MIMEPOST, form.get());
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
    co_await this->data->async_run.lock_guard();

    if (resp != CURLE_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->url, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    if (!this->data->headers.empty()) {
        if (this->data->async_handler_headers) {
            co_await this->data->async_handler_headers(this->data, std::move(this->data->headers));
        }
        if (this->data->handler_headers) {
            this->data->handler_headers (std::move(this->data->headers));
        }
    }

    curl_easy_getinfo(this->data->curl.get(), CURLINFO_HTTP_CODE, &this->status_code);
}

std::map <std::string, std::string> manapi::net::fetch::get_headers() {
    return std::move(this->data->headers);
}

void manapi::net::fetch::clear() {
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
    this->data->async_buffer.clear();
    this->data->async_buffer_cursor=0;
    this->data->headers.clear();
    this->data->handler_headers=nullptr;
    this->data->handler_body=nullptr;
    this->data->async_handler_body=nullptr;
    this->data->async_handler_headers=nullptr;
    this->data->async_waiting.store(false);
    this->data->parallel_task.reset();
    this->data->sync_user_body_cb.reset();
    this->data->async_user_body_cb.reset();
}

manapi::future<bool> manapi::net::fetch::handle_body_verify(std::shared_ptr<shared_data> data) {
    bool flg = false;
    if (data->async_handler_headers) {
        flg = co_await data->async_handler_headers(data, std::move(data->headers));
    }
    if (data->handler_headers) {
        flg = data->handler_headers(std::move(data->headers));
    }

    if (!flg) {
        data->async_buffer_cursor = 0;
        co_await data->ctx->eventloop()->unwatch_curl(data->curl.get());
        data->async_run.unlock();
        co_return false;
    }
    co_return true;
}

manapi::future<void> manapi::net::fetch::handle_sync_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    data->handler_body = std::move(data->sync_user_body_cb.value());
    data->sync_user_body_cb.reset();
    data->async_waiting.store(false);

    if (finish) {
        co_return;
    }

    if (data->async_buffer_cursor > 0) {
        /* cached */
        ssize_t current = 0, rhs = 0;
        while (current < data->async_buffer_cursor) {
            if ((rhs = data->handler_body (data->async_buffer.data() + current, data->async_buffer_cursor - current)) < 0) {
                data->async_buffer_cursor = 0;
                co_await data->ctx->eventloop()->unwatch_curl(data->curl.get());
                data->async_run.unlock();
                co_return;
            }

            current += rhs;
        }

        data->async_buffer_cursor = 0;
    }

    data->async_handler_body = nullptr;
    data->async_run.unlock();
    co_await data->ctx->eventloop()->unpause_watch_curl(data->curl.get());
}

manapi::future<void> manapi::net::fetch::handle_async_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    data->async_handler_body = [] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        return data->async_user_body_cb.value()(finish);
    };

    co_await data->async_handler_body (data, finish);
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    ssize_t total_read_prev = 0;
    ssize_t total_write_prev = 0;
    ssize_t again = 10;

    manapi::async::parallel_run<void> unwatch_curl_action (this->data->ctx);

    auto timeout_token = co_await this->data->ctx->timerpool()->async_append_interval_sync(200, [&] (manapi::timer t) -> void {
        if (again >= 0 && !this->data->async_waiting && this->total_read - total_read_prev + this->total_write - total_write_prev < 8 * 1024) {
            if (!(again--)) {
                unwatch_curl_action.run(this->data->ctx->eventloop()->unwatch_curl(this->data->curl.get()));
            }
        }
    });

    CURLcode res;

    try {
        res = co_await async::promise<CURLcode> (this->data->ctx->eventloop()->get_task_pool(), [this] (async::promise<CURLcode>::resolve_t resolve, async::promise<CURLcode>::reject_t reject) -> future<> {
            try {
                co_await this->data->ctx->eventloop()->watch_curl(this->data->curl.get(), [resolve = std::move(resolve)] (CURLcode result)
                    -> void { resolve (result); });
            }
            catch (...) {
                reject (std::current_exception());
            }
        });


        if (this->data->async_handler_body && this->data->async_buffer_cursor) {
            co_await this->data->async_handler_body(this->data, true);
        }
    }
    catch (...) {
        res = CURLE_AGAIN;
    }

    co_await timeout_token.async_stop(this->data->ctx);
    co_await unwatch_curl_action.get ();

    co_return res;
}

void manapi::net::fetch::handle_body(std::function<ssize_t(char *, ssize_t)> handler) {
    if (this->data->async_handler_body) {
        this->data->async_handler_body = nullptr;
        this->data->async_user_body_cb.reset();
    }

    this->data->sync_user_body_cb = std::move(handler);

    this->setup_parallel_task();

    this->data->async_handler_body = [](std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        if (!co_await fetch::handle_body_verify(data)) {
            co_return;
        }

        if (!data->sync_user_body_cb.has_value()) {
            /* handler is async for now */
            co_await handle_async_body_finish(data, finish);
            co_return;
        }

        co_await handle_sync_body_finish(data, finish);
    };

    this->data->handler_body = [this] (char *buffer, size_t buffer_size) -> ssize_t {
        auto data = this->data;
        data->async_waiting.store(true);

        /* in the loop event */
        manapi::async::run(data->ctx, data->ctx->eventloop()->custom_cb_curl(data->curl.get(), data->parallel_task.value()));

        assert(data->async_run.try_to_lock());

        return CURL_WRITEFUNC_PAUSE;
    };
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
    this->data->async_buffer.resize(65536);
    this->data->sync_user_body_cb.reset();

    this->setup_parallel_task();

    this->data->async_user_body_cb = [handler = std::move(handler), data = this->data] (bool finish) -> manapi::future<> {
        ssize_t total = 0;

        while (total < data->async_buffer_cursor) {
            const auto rhs = co_await handler (data->async_buffer.data() + total, data->async_buffer_cursor - total);
            if (rhs < 0) {
                if (finish) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "handle_async_body(...): Write handler error : {}", rhs);
                }

                data->async_buffer_cursor = 0;
                co_await data->ctx->eventloop()->unwatch_curl(data->curl.get());
                data->async_run.unlock();

                co_return;
            }
            total += rhs;
        }

        data->async_buffer_cursor = 0;
        data->async_waiting.store(false);

        if (finish) {
            co_return;
        }

        data->async_run.unlock();
        co_await data->ctx->eventloop()->unpause_watch_curl(data->curl.get());
    };

    this->data->async_handler_body = [] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        if (!co_await fetch::handle_body_verify(data)) {
            co_return;
        }

        if (!data->async_user_body_cb.has_value()) {
            /* handler body isn't async for now */
            co_return co_await handle_sync_body_finish (data, finish);
        }

        co_await handle_async_body_finish (data, finish);
    };

    this->data->handler_body = [this] (char *buffer, ssize_t size) -> ssize_t {
        if (this->data->async_buffer_cursor < this->data->async_buffer.size()
            && this->data->async_buffer.size() - this->data->async_buffer_cursor >= size) {
            const auto copy = size;
            memcpy(this->data->async_buffer.data() + this->data->async_buffer_cursor, buffer, copy);
            this->data->async_buffer_cursor += copy;

            return copy;
        }

        this->data->async_waiting.store(true);

        /* in the loop event */
        assert(this->data->async_run.try_to_lock());

        manapi::async::run(this->data->ctx, this->data->ctx->eventloop()->custom_cb_curl(this->data->curl.get(), this->data->parallel_task.value()));

        return CURL_WRITEFUNC_PAUSE;
    };
}

void manapi::net::fetch::handle_headers(std::function<bool(std::map <std::string, std::string>)> handler) {
    if (this->data->async_handler_headers) { this->data->async_handler_headers = {nullptr}; }
    this->data->handler_headers = std::move(handler);
}

void manapi::net::fetch::handle_async_headers(std::function<manapi::future<bool>(std::map<std::string, std::string>)> handler) {
    if (this->data->handler_headers) { this->data->handler_headers = {nullptr}; }
    this->data->async_handler_headers = [handler = std::move(handler)] (std::shared_ptr<shared_data> data, std::map<std::string, std::string> headers) mutable
        -> manapi::future<bool> { return handler(std::move(headers)); };
}

void manapi::net::fetch::enable_alpn(bool status) {
    curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
}

void manapi::net::fetch::enable_http3() {
    curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
}

void manapi::net::fetch::enable_http2() {
    curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
}

void manapi::net::fetch::enable_http1_1() {
    curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
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
        this->data->curl_headers.reset(curl_slist_append(this->data->curl_headers.release(), manapi::net::http::stringify_header(header).data()));
    }
}

void manapi::net::fetch::set_custom_setup(std::function<void(CURL *curl)> func) {
    this->data->handle_custom_setup = std::move(func);
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    auto lstatus = static_cast<long> (status);
    curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
}

void manapi::net::fetch::set_verbose(bool status) {
    curl_easy_setopt(this->data->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
}

void manapi::net::fetch::break_write_loop() {
    curl_easy_pause(this->data->curl.get(), CURLPAUSE_RECV);
}

void manapi::net::fetch::continue_write_loop() {
    curl_easy_pause(this->data->curl.get(), CURLPAUSE_CONT);
}

size_t manapi::net::fetch::get_status_code() const {
    return this->status_code;
}

