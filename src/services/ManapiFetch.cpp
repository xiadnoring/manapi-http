#include "services/ManapiFetch.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY

#include <exception>

#include "ManapiHttp.hpp"

#include "async/ManapiAsyncPromise.hpp"

// Utils

struct curl_data_t {
    manapi::net::fetch *fetch;
};

manapi::object_pool<manapi::bytebuffer, std::true_type> manapi::net::fetch::bufferpool {};

std::map <std::string, CURLoption> manapi::net::fetch::http_method_to_enum {
    {"POST", CURLOPT_POST},
    {"PUT", CURLOPT_UPLOAD},
    {"HEAD", CURLOPT_NOBODY}
};

size_t manapi::net::fetch::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    auto &f = *static_cast <curl_data_t *> (userdata)->fetch;
    /**
     * clear body send callback
     */
    f.data_->async_buffer_cursor = 0;

    std::string str (buffer, size * n_items);

    // delete \r\n at the end of the header
    if (str.size() >= 2)
    {
        str.pop_back(); // \n
        str.pop_back(); // \r
    }

    auto header = manapi::net::http::parse_header(str);
    f.data_->headers.insert(std::move(header));

    return n_items * size;
}

size_t manapi::net::fetch::curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p)
{
    auto &f = *static_cast <curl_data_t *> (user_p);
    try {
        // call user handler
        return static_cast<size_t>(f.fetch->data_->handler_recv_body (buffer, static_cast<ssize_t>(size * nitems)));
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("noexcept curl_write_handler(...): throw exception: {}", e.what());
        return -1;
    }
}

std::size_t manapi::net::fetch::curl_read_handler(char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto &f = *static_cast<curl_data_t *> (user_p);
    try {
        return static_cast<std::size_t>(f.fetch->data_->handler_send_body(buffer, static_cast<ssize_t> (size * nitems)));
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("noexcept curl_read_handler(...): throw exception: {}", e.what());
        return -1;
    }
}

void manapi::net::fetch::setup_parallel_task() {
    this->data_->parallel_task = [this] (CURLcode code) -> void {
        auto data = this->data_;
        curl_easy_getinfo(this->data_->curl.get(), CURLINFO_HTTP_CODE, &this->status_code_);
        data->parallel_task = [data] (CURLcode code) -> void {
            async::run(data->ctx, data->async_handler_recv_body(data, false));
        };

        data->parallel_task.value()(code);
    };
}

void manapi::net::fetch::_default_setup_curl() {
    this->timeout(5);
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
    this->mdata.insert({name, {.strdata = std::move(std::move(value)), .filedata = {}, .type = PARAM_DEFAULT}});
}

void manapi::net::curlformdata::setfile(const std::string &filename, std::string filepath) {
    this->mdata.insert({filename, {.strdata = std::move(filepath), .filedata = {}, .type = PARAM_FILE}});
}

void manapi::net::curlformdata::setcallback(const std::string &name, const long long &size, std::move_only_function<size_t(void *buff, size_t buff_size)> cb) {
    this->mdata.insert({name, {.strdata = {}, .filedata = multipart_param_value_file({std::move(cb), size}), .type = PARAM_CALLBACK}});
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


manapi::net::fetch::fetch(const std::shared_ptr<async::context> &ctx, std::string url) {
    this->data_ = std::make_shared<shared_data>(ctx);
    this->url_ = std::move(url);
    this->method_ = "GET";
    this->data_->ctx = ctx;
    this->data_->curl.reset(curl_easy_init());
    this->content_length_ = -1;

    this->_default_setup_curl();
}

manapi::net::fetch::fetch(fetch &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::fetch::~fetch() = default;

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept {
    this->data_ = std::move(n.data_);
    this->status_code_ = std::exchange(n.status_code_, 200);
    this->url_ = std::move(n.url_);
    this->body_ = std::exchange(n.body_, BODY_NONE);
    this->body_default_ = std::move(n.body_default_);
    this->body_formdata_ = std::move(n.body_formdata_);
    this->method_ = std::move(n.method_);
    this->content_length_ = std::exchange(n.content_length_, -1);

    return *this;
}

manapi::future<void> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (!this->data_->curl)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", url_);
    }

    auto clear_ = before_delete ([this] ()
        -> void { this->clear(); });

    curl_data_t data {
        .fetch = this
    };

    CURLcode resp;

    curl_easy_setopt(this->data_->curl.get(), CURLOPT_URL, url_.data());
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler);
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_HEADERDATA, &data);

    // if handler has been set
    if (this->data_->handler_recv_body)
    {
        curl_easy_setopt(this->data_->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler);
        curl_easy_setopt(this->data_->curl.get(), CURLOPT_WRITEDATA, &data);
    }

    if (this->data_->curl_headers)
    {
        curl_easy_setopt(this->data_->curl.get(), CURLOPT_HTTPHEADER, this->data_->curl_headers.get());
    }

    {
        const auto optheader = net::fetch::http_method_to_enum.find(this->method_);
        if (optheader == net::fetch::http_method_to_enum.end()) {
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_CUSTOMREQUEST, this->method_.data());
        }
        else {
            curl_easy_setopt(this->data_->curl.get(), optheader->second, 1);
        }
    }

    if (this->data_->handle_custom_setup)
    {
        this->data_->handle_custom_setup (this->data_->curl.get());
    }

    // body of the request
    switch (this->body_) {
        case BODY_PLAIN: {
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_POSTFIELDS, this->body_default_.data());
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->body_default_.size());
            break;
        }
        case BODY_CALLBACK: {
            if (this->content_length_ < 0) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_CONFIG_ERROR, "ManapiFetch: content-length is required");
            }
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_READFUNCTION, curl_read_handler);
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_READDATA, &data);
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->content_length_);

            break;
        }
        case BODY_MULTIPART: {
            form.reset(curl_mime_init(this->data_->curl.get()));
            curl_mimepart *field = nullptr;
            for (auto &param : this->body_formdata_.value()) {
                field = curl_mime_addpart(form.get());
                curl_mime_name(field, param.first.data());
                switch (param.second.type) {
                    case curlformdata::PARAM_DEFAULT: {
                        auto str = param.second.strdata.value();
                        curl_mime_data(field, str.data(), str.size());
                        break;
                    }
                    case curlformdata::PARAM_FILE: {
                        auto str = param.second.strdata.value();
                        curl_mime_filedata(field, str.data());
                        break;
                    }
                    case curlformdata::PARAM_CALLBACK: {
                        auto &cbdata = param.second.filedata.value();
                        curl_mime_data_cb(field, cbdata.filesize, curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback);
                        break;
                    }
                }
            }
            curl_easy_setopt(this->data_->curl.get(), CURLOPT_MIMEPOST, form.get());
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
    co_await this->data_->async_run.lock_guard();

    if (resp != CURLE_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->url_, static_cast<int> (resp), curl_easy_strerror(resp));
    }

    if (!this->data_->headers.empty()) {
        if (this->data_->async_handler_headers) {
            co_await this->data_->async_handler_headers(this->data_, std::move(this->data_->headers));
        }
        if (this->data_->handler_headers) {
            this->data_->handler_headers (std::move(this->data_->headers));
        }
    }

    curl_easy_getinfo(this->data_->curl.get(), CURLINFO_HTTP_CODE, &this->status_code_);
}

std::map <std::string, std::string> manapi::net::fetch::headers() {
    return std::move(this->data_->headers);
}

void manapi::net::fetch::clear() {
    this->method_ = "GET";
    this->url_.clear();
    this->status_code_=200;
    this->body_ = BODY_NONE;
    this->body_formdata_.reset();
    this->body_default_.clear();
    this->data_->async_buffer = nullptr;
    this->data_->async_buffer_cursor=0;
    this->data_->headers.clear();
    this->data_->handler_headers=nullptr;
    this->data_->handler_recv_body=nullptr;
    this->data_->async_handler_recv_body=nullptr;
    this->data_->async_handler_headers=nullptr;
    this->data_->async_waiting.store(false);
    this->data_->parallel_task.reset();
    this->data_->sync_user_body_cb.reset();
    this->data_->async_user_body_cb.reset();
    this->data_->async_handler_send_body=nullptr;
    this->data_->handler_send_body=nullptr;

    this->_default_setup_curl();
}

manapi::future<bool> manapi::net::fetch::handle_body_verify(std::shared_ptr<shared_data> data) {
    bool flg = false;
    if (data->async_handler_headers) {
        flg = co_await data->async_handler_headers(data, std::move(data->headers));
    }

    else if (data->handler_headers) {
        flg = data->handler_headers(std::move(data->headers));
    }
    else {
        flg = true;
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
    data->handler_recv_body = std::move(data->sync_user_body_cb.value());
    data->sync_user_body_cb.reset();
    data->async_waiting.store(false);

    if (finish) {
        co_return;
    }

    if (data->async_buffer_cursor > 0) {
        /* cached */
        ssize_t current = 0, rhs = 0;
        while (current < data->async_buffer_cursor) {
            if ((rhs = data->handler_recv_body (data->async_buffer->as<char>() + current, data->async_buffer_cursor - current)) < 0) {
                data->async_buffer_cursor = 0;
                co_await data->ctx->eventloop()->unwatch_curl(data->curl.get());
                data->async_run.unlock();
                co_return;
            }

            current += rhs;
        }

        data->async_buffer_cursor = 0;
    }

    data->async_handler_recv_body = nullptr;
    data->async_run.unlock();
    co_await data->ctx->eventloop()->unpause_watch_curl(data->curl.get());
}

manapi::future<void> manapi::net::fetch::handle_async_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    data->async_handler_recv_body = [] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        return data->async_user_body_cb.value()(std::move(data), finish);
    };

    co_await data->async_handler_recv_body (data, finish);
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    CURLcode res;

    try {
        res = co_await async::promise<CURLcode> (this->data_->ctx->eventloop()->get_task_pool(), [this] (async::promise<CURLcode>::resolve_t resolve, async::promise<CURLcode>::reject_t reject) -> future<> {
            try {
                co_await this->data_->ctx->eventloop()->watch_curl(this->data_->curl.get(), [resolve = std::move(resolve)] (CURLcode result)
                    -> void {
                    resolve (result);
                });
            }
            catch (...) {
                reject (std::current_exception());
            }
        });


        if (this->data_->async_handler_recv_body && this->data_->async_buffer_cursor) {
            co_await this->data_->async_handler_recv_body(this->data_, true);
        }
    }
    catch (...) {
        res = CURLE_AGAIN;
    }

    co_return res;
}

void manapi::net::fetch::handle_body(std::move_only_function<ssize_t(char *, ssize_t)> handler) {
    if (this->data_->async_handler_recv_body) {
        this->data_->async_handler_recv_body = nullptr;
        this->data_->async_user_body_cb.reset();
    }

    this->data_->sync_user_body_cb = std::move(handler);

    this->setup_parallel_task();

    this->data_->async_handler_recv_body = [](std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
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

    this->data_->handler_recv_body = [this] (char *buffer, size_t buffer_size) -> ssize_t {
        auto data = this->data_;
        data->async_waiting.store(true);

        /* in the loop event */
        manapi::async::run(data->ctx, data->ctx->eventloop()->custom_cb_curl(data->curl.get(), std::move(data->parallel_task.value())));

        if (!this->data_->async_run.try_to_lock()) {
            /** something get wrong **/
            return CURL_WRITEFUNC_ERROR;
        }

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


void manapi::net::fetch::handle_async_body(std::move_only_function<manapi::future<ssize_t>(char *, ssize_t)> handler) {
    if (!this->data_->async_buffer) {
        this->data_->async_buffer = fetch::bufferpool.get();
    }
    this->data_->async_buffer->resize(65536);
    this->data_->sync_user_body_cb.reset();
    this->data_->async_buffer_cursor = 0;

    this->setup_parallel_task();

    this->data_->async_user_body_cb = [handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        ssize_t total = 0;

        while (total < data->async_buffer_cursor) {
            ssize_t rhs = -1;

            try {
                rhs = co_await handler (data->async_buffer->as<char>() + total, data->async_buffer_cursor - total);
            }
            catch (std::exception const &e) {
                // DEBUG
                MANAPIHTTP_LOG("handle_async_body(...) failed: {}", e.what());
            }

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

    this->data_->async_handler_recv_body = [] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        if (!co_await fetch::handle_body_verify(data)) {
            co_return;
        }

        if (!data->async_user_body_cb.has_value()) {
            /* handler body isn't async for now */
            co_return co_await handle_sync_body_finish (data, finish);
        }

        co_await handle_async_body_finish (data, finish);
    };

    this->data_->handler_recv_body = [this] (char *buffer, ssize_t size) -> ssize_t {
        if (this->data_->async_buffer_cursor < this->data_->async_buffer->size()
            && this->data_->async_buffer->size() - this->data_->async_buffer_cursor >= size) {
            const auto copy = size;
            memcpy(this->data_->async_buffer->as<char>() + this->data_->async_buffer_cursor, buffer, copy);
            this->data_->async_buffer_cursor += copy;

            return copy;
        }

        this->data_->async_waiting.store(true);

        /* in the loop event */
        if (!this->data_->async_run.try_to_lock()) {
            /** something get wrong **/
            return CURL_WRITEFUNC_ERROR;
        }

        manapi::async::run(this->data_->ctx, this->data_->ctx->eventloop()->custom_cb_curl(this->data_->curl.get(), this->data_->parallel_task.value()));

        return CURL_WRITEFUNC_PAUSE;
    };
}

void manapi::net::fetch::handle_headers(std::move_only_function<bool(std::map <std::string, std::string>)> handler) {
    if (this->data_->async_handler_headers) { this->data_->async_handler_headers = {nullptr}; }
    this->data_->handler_headers = std::move(handler);
}

void manapi::net::fetch::handle_async_headers(std::move_only_function<manapi::future<bool>(std::map<std::string, std::string>)> handler) {
    if (this->data_->handler_headers) { this->data_->handler_headers = {nullptr}; }
    this->data_->async_handler_headers = [handler = std::move(handler)] (std::shared_ptr<shared_data> data, std::map<std::string, std::string> headers) mutable
        -> manapi::future<bool> { return handler(std::move(headers)); };
}

void manapi::net::fetch::enable_alpn(bool status) {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
}

void manapi::net::fetch::enable_http3() {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
}

void manapi::net::fetch::enable_http2() {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
}

void manapi::net::fetch::enable_http1_1() {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
}

void manapi::net::fetch::body(curlformdata params) {
    this->body_ = BODY_MULTIPART;
    this->body_formdata_ = std::move(params);
}


void manapi::net::fetch::method(std::string method) {
    this->method_ = std::move(method);
}

void manapi::net::fetch::body(std::string data) {
    this->body_ = BODY_PLAIN;
    this->body_default_ = std::move(data);
}

manapi::future<> manapi::net::fetch::body(file_transfer_info file_info) {
    auto file = std::make_shared <manapi::filesystem::async::fstream> (this->data_->ctx, file_info.filelocal());
    co_await file->open(file->FILE_READ);
    if (!file->is_open()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to open the file: {}", file_info.filelocal());
    }

    curl_easy_setopt(this->data_->curl.get(), CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, file->total_size());

    this->async_body([file = std::move(file), file_info = std::move(file_info)] (char *buffer, ssize_t size) mutable
        -> manapi::future<ssize_t> {
        ssize_t cursor = 0;
        while (cursor < size) {
            auto rhs = co_await file->read(buffer + cursor, size - cursor);

            if (rhs == 0) {
                /* eof */
                break;
            }

            if (rhs < 0) {
                /* error */
                co_return -1;
            }

            cursor += rhs;
        }

        co_return cursor;
    });
}

void manapi::net::fetch::async_body(std::move_only_function<manapi::future<ssize_t>(char *, ssize_t)> handler) {
    this->body_ = BODY_CALLBACK;

    if (!this->data_->async_buffer) {
        this->data_->async_buffer = fetch::bufferpool.get();
    }
    this->data_->async_buffer->resize(65536);
    this->data_->async_buffer_cursor = 0;

    this->data_->async_handler_send_body = [handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        while (data->async_buffer_cursor < data->async_buffer->size()) {
            ssize_t rhs = -1;
            try {
                rhs = co_await handler (data->async_buffer->as<char>() + data->async_buffer_cursor, static_cast<ssize_t>(data->async_buffer->size() - data->async_buffer_cursor));
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("set_async_body(...) failed: {}", e.what());
            }
            if (rhs < 0) {
                /* error */
                if (finish) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "set_async_body(...): read handler error : {}", rhs);
                }

                data->async_buffer_cursor = 0;
                co_await data->ctx->eventloop()->unwatch_curl(data->curl.get());
                data->async_run.unlock();

                co_return;
            }

            if (rhs == 0) {
                /* eof */
                break;
            }

            data->async_buffer_cursor += rhs;
        }

        data->async_waiting.store(false);

        if (finish) {
            co_return;
        }

        data->async_run.unlock();
        co_await data->ctx->eventloop()->unpause_watch_curl(data->curl.get());
    };

    this->data_->handler_send_body = [this, index = ssize_t{0}] (char *buffer, ssize_t size) mutable -> ssize_t {
        size = std::min(this->data_->async_buffer_cursor-index, size);
        if (size > 0) {
            memcpy(buffer, this->data_->async_buffer->as<char>() + index, size);
            index += size;

            if (this->data_->async_buffer_cursor == index) {
                this->data_->async_buffer_cursor = 0;
                index = 0;
            }

            return size;
        }

        this->data_->async_waiting.store(true);

        /* in the loop event */
        if (!this->data_->async_run.try_to_lock()) {
            /** something get wrong **/
            return CURL_READFUNC_ABORT;
        }

        manapi::async::run(this->data_->ctx, this->data_->ctx->eventloop()->custom_cb_curl(this->data_->curl.get(), [data = this->data_] (CURLcode code)
            -> void { async::run(data->ctx, data->async_handler_send_body(data, false)); }));

        return CURL_READFUNC_PAUSE;
    };
}

void manapi::net::fetch::body(std::move_only_function<ssize_t(char *, ssize_t)> handler) {
    this->body_ = BODY_CALLBACK;
    this->data_->async_handler_send_body = nullptr;
    this->data_->handler_send_body = std::move(handler);
}

void manapi::net::fetch::headers(std::map<std::string, std::string> headers) {
    auto content_length = headers.find(http::HEADER.CONTENT_LENGTH);
    if (content_length != headers.end()) {
        this->content_length_ = std::stoll(content_length->second);
        headers.erase(content_length);
    }

    // headers
    for (auto &header: headers)
    {
        this->data_->curl_headers.reset(curl_slist_append(this->data_->curl_headers.release(), manapi::net::http::stringify_header(header).data()));
    }
}

void manapi::net::fetch::json_headers(manapi::json headers) {
    auto &m = headers.entries();
    auto content_length = m.find(http::HEADER.CONTENT_LENGTH);
    if (content_length != m.end()) {
        this->content_length_ = content_length->second.as_integer_cast();
        headers.erase(content_length);
    }
    // headers
    for (auto &header: headers.entries())
    {
        this->data_->curl_headers.reset(curl_slist_append(this->data_->curl_headers.release(), manapi::net::http::stringify_header({header.first, std::move(header.second.as_string_cast())}).data()));
    }
}

void manapi::net::fetch::custom_setup(std::move_only_function<void(CURL *curl)> func) {
    this->data_->handle_custom_setup = std::move(func);
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    auto lstatus = static_cast<long> (status);
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
}

void manapi::net::fetch::verbose(bool status) {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
}

void manapi::net::fetch::timeout(const std::size_t &seconds) {
    curl_easy_setopt(this->data_->curl.get(), CURLOPT_TIMEOUT, seconds);
}

void manapi::net::fetch::break_write_loop() {
    curl_easy_pause(this->data_->curl.get(), CURLPAUSE_RECV);
}

void manapi::net::fetch::continue_write_loop() {
    curl_easy_pause(this->data_->curl.get(), CURLPAUSE_CONT);
}

size_t manapi::net::fetch::status_code() const {
    return this->status_code_;
}

#endif