#include "services/ManapiFetch.hpp"

#include "string.h"

#include "services/ManapiFetch.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY

#include <exception>

#include "ManapiHttp.hpp"

#include "async/ManapiAsyncPromise.hpp"

// Utils

struct manapi::net::fetch::shared_data {
    int flags{0};
    std::unique_ptr<async::mutex> async_run{nullptr};
    ssize_t async_buffer_cursor{0};
    object_item_pool<manapi::bytebuffer> async_buffer{};
    std::unique_ptr<std::move_only_function <void(CURL *)>> handle_custom_setup{nullptr};
    std::unique_ptr<std::move_only_function <ssize_t(char *, ssize_t)>> handler_recv_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)>> async_handler_recv_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<bool>(std::shared_ptr<shared_data> data, std::map <std::string, std::string>)>> async_handler_headers{nullptr};
    std::unique_ptr<std::move_only_function <bool(std::map <std::string, std::string>)>> handler_headers{nullptr};
    std::shared_ptr<CURL> curl {nullptr};
    std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers {nullptr};
    std::unique_ptr<std::map<std::string, std::string>> headers{nullptr};
    std::unique_ptr<std::move_only_function<manapi::future<>(std::shared_ptr<shared_data> data, bool)>> async_user_body_cb{nullptr};
    std::unique_ptr<std::move_only_function<ssize_t(char *buffer, ssize_t size)>> sync_user_body_cb{nullptr};
    std::unique_ptr<std::move_only_function <ssize_t(char *, ssize_t)>> handler_send_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)>> async_handler_send_body{nullptr};
    std::unique_ptr<std::move_only_function<void()>> parallel_task{nullptr};
};

struct manapi::net::fetch::data_t {
    int flags{0};
    size_t status_code_{200};
    ssize_t content_length_{-1};
    manapi::async::cancellation_action cancellation{nullptr};

    std::string url_{};

    std::shared_ptr<shared_data> data_{nullptr};

    body_type body_{BODY_NONE};

    std::string body_default_{};
    std::string method_{};
    std::optional<curlformdata> body_formdata_{};
};

manapi::object_pool<manapi::bytebuffer, std::true_type> manapi::net::fetch::bufferpool {};

std::map <std::string, CURLoption> manapi::net::fetch::http_method_to_enum {
    {"POST", CURLOPT_POST},
    {"PUT", CURLOPT_UPLOAD},
    {"HEAD", CURLOPT_NOBODY}
};

size_t manapi::net::fetch::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    auto const f = static_cast <fetch::shared_data *> (userdata);
    try {
        /**
         * clear body send callback
         */
        f->async_buffer_cursor = 0;

        if (f->flags & FLAG_STATUS_PASSED) {
            std::string_view const str (buffer, size * n_items - 2);
            if (!str.empty()) {
                auto header = manapi::net::http::parse_header(str);
                f->headers->insert(std::move(header));
            }
        }
        else {
            f->flags |= FLAG_STATUS_PASSED;
        }

        return n_items * size;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("fetch error: {}", e.what());
    }
    return -1;
}

size_t manapi::net::fetch::curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p) {
    auto f = static_cast <shared_data *> (user_p);
    try {
        // call user handler
        return static_cast<size_t>(f->handler_recv_body->operator() (buffer, static_cast<ssize_t>(size * nitems)));
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("noexcept curl_write_handler(...): throw exception: {}", e.what());
        return -1;
    }
}

std::size_t manapi::net::fetch::curl_read_handler(char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto f = static_cast<shared_data *> (user_p);
    try {
        return static_cast<std::size_t>(f->handler_send_body->operator()(buffer, static_cast<ssize_t> (size * nitems)));
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("noexcept curl_read_handler(...): throw exception: {}", e.what());
        return -1;
    }
}

void manapi::net::fetch::setup_parallel_task() {
    this->data->data_->parallel_task = std::make_unique<std::move_only_function<void()>>([this] () -> void {
        auto data = this->data->data_;
        curl_easy_getinfo(this->data->data_->curl.get(), CURLINFO_HTTP_CODE, &this->data->status_code_);
        *data->parallel_task = [data] () -> void {
            manapi::async::current()->etaskpool()->append_task([data] () -> void {
                auto const ptr = data.get();
                async::run(ptr->async_handler_recv_body->operator()(std::move(data), false));
            });
        };
        data->parallel_task->operator()();
    });
}

void manapi::net::fetch::default_setup_curl_() {
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


manapi::net::fetch::fetch(std::string url, manapi::async::cancellation_action cancellation) {
    this->data = std::make_shared<fetch::data_t>(fetch::data_t{});
    this->data->data_ = std::make_shared<fetch::shared_data>();
    this->data->url_ = std::move(url);
    this->data->data_->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_free);
    this->data->data_->async_run = std::make_unique<async::mutex>();
    this->data->cancellation = std::move(cancellation);
    this->default_setup_curl_();
}

manapi::net::fetch::fetch(fetch &&n) noexcept = default;

manapi::net::fetch::~fetch() = default;

manapi::net::fetch & manapi::net::fetch::operator=(fetch &&n) noexcept = default;

manapi::future<void> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (this->data->flags & FLAG_WAS_USED) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "fetch was already used. create another fetch object or reinit current. {}", this->data->url_);
    }

    this->data->flags |= FLAG_WAS_USED;

    if (!this->data->data_->curl)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl can not be init: {}", this->data->url_);
    }

    std::exception_ptr err{nullptr};

    try {
        CURLcode resp;

        curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_NOSIGNAL, 1L);
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MAXLIFETIME_CONN, 1L);
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MAXAGE_CONN, 0);
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_TCP_KEEPALIVE, 0L);
        curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_URL, this->data->url_.data());
        curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler);
        curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HEADERDATA, this->data->data_.get());

        {
            const auto optheader = net::fetch::http_method_to_enum.find(this->data->method_);
            if (optheader == net::fetch::http_method_to_enum.end()) {
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_CUSTOMREQUEST, this->data->method_.data());
            }
            else {
                curl_easy_setopt(this->data->data_->curl.get(), optheader->second, 1);
            }
        }

        // if handler has been set
        if (this->data->data_->handler_recv_body)
        {
            curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler);
            curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_WRITEDATA, this->data->data_.get());
        }

        if (this->data->data_->handle_custom_setup)
        {
            this->data->data_->handle_custom_setup->operator() (this->data->data_->curl.get());
        }

        // body of the request
        switch (this->data->body_) {
            case BODY_PLAIN: {
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDS, this->data->body_default_.data());
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->body_default_.size());
                break;
            }
            case BODY_CALLBACK: {
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_READFUNCTION, curl_read_handler);
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_READDATA, this->data->data_.get());

                if (this->data->flags & FLAG_CONTENT_LENGTH) {
                    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->content_length_);
                }

                break;
            }
            case BODY_MULTIPART: {
                form.reset(curl_mime_init(this->data->data_->curl.get()));
                curl_mimepart *field = nullptr;
                for (auto &param : this->data->body_formdata_.value()) {
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
                curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MIMEPOST, form.get());
                break;
            }
            default:
                break;
        }

        if (this->data->data_->curl_headers)
        {
            curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTPHEADER, this->data->data_->curl_headers.get());
        }

        this->data->data_->headers = std::make_unique<decltype(this->data->data_->headers)::element_type>();

        try {
            if (this->data->cancellation.contains_cancel_callback()) {
                this->data->cancellation.cancel_callback([data = this->data->data_->curl] () mutable -> void {
                    if (data)
                        manapi::async::current()->eventloop()->unwatch_curl(std::move(data));
                });
            }

            resp = co_await async_curl_perform();
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("Exception: {}", e.what());
            resp = CURLE_AGAIN;
        }

        this->data->data_->flags |= FLAG_DATA_CLOSED;

        /* wait all jobs */
        auto lk = co_await this->data->data_->async_run->lock_guard();

        this->data->cancellation.disable_cancellation();
        this->data->cancellation = nullptr;

        if (resp != CURLE_OK)
        {
            THROW_MANAPIHTTP_EXCEPTION (ERR_FATAL, "Connection failed: {}. Error code: {}. Error msg: {}", this->data->url_, static_cast<int> (resp), curl_easy_strerror(resp));
        }

        if (this->data->data_->headers && !this->data->data_->headers->empty()) {
            if (this->data->data_->async_handler_headers) {
                co_await this->data->data_->async_handler_headers->operator()(this->data->data_, std::move(*this->data->data_->headers));
            }
            else if (this->data->data_->handler_headers) {
                this->data->data_->handler_headers->operator()(std::move(*this->data->data_->headers));
            }
            this->data->data_->headers = nullptr;
        }

        curl_easy_getinfo(this->data->data_->curl.get(), CURLINFO_HTTP_CODE, &this->data->status_code_);
    }
    catch (...) {
        err = std::current_exception();
    }

    this->clear_();

    if (err)
        std::rethrow_exception(std::move(err));
}

std::map <std::string, std::string> manapi::net::fetch::headers() {
    return std::move(*std::exchange(this->data->data_->headers, nullptr));
}

void manapi::net::fetch::clear() {
    this->clear_();

    this->data->data_->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_free);
    this->data->data_->flags = 0;
}

void manapi::net::fetch::clear_() {
    *this->data = fetch::data_t{.url_ = std::move(this->data->url_), .data_ = std::move(this->data->data_)};
    *this->data->data_ = fetch::shared_data{};

    this->default_setup_curl_();
}

manapi::future<bool> manapi::net::fetch::handle_body_verify(std::shared_ptr<shared_data> data) {
    bool flg = false;
    if (data->async_handler_headers) {
        auto const cb = std::move(data->async_handler_headers);
        flg = co_await cb->operator()(data, std::move(*data->headers));
    }

    else if (data->handler_headers) {
        auto const cb = std::move(data->handler_headers);
        flg = cb->operator()(std::move(*data->headers));
    }
    else {
        flg = true;
    }

    data->headers = nullptr;

    if (!flg) {
        data->async_buffer_cursor = 0;
        async::current()->eventloop()->unwatch_curl(data->curl);
        data->async_run->unlock();
        co_return false;
    }
    co_return true;
}

manapi::future<void> manapi::net::fetch::handle_sync_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    data->handler_recv_body = std::move(data->sync_user_body_cb);

    if (finish) {
        co_return;
    }

    if (data->async_buffer_cursor > 0) {
        /* cached */
        ssize_t current = 0, rhs = 0;
        while (current < data->async_buffer_cursor) {
            if ((rhs = data->handler_recv_body->operator()(data->async_buffer->as<char>() + current, data->async_buffer_cursor - current)) < 0) {
                data->async_buffer_cursor = 0;
                manapi::async::current()->eventloop()->unwatch_curl(data->curl);
                data->async_run->unlock();
                co_return;
            }

            current += rhs;
        }

        data->async_buffer_cursor = 0;
    }

    data->async_handler_recv_body = nullptr;
    data->async_run->unlock();
    manapi::async::current()->eventloop()->unpause_watch_curl(data->curl);
}

manapi::future<void> manapi::net::fetch::handle_async_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    data->async_handler_recv_body = std::make_unique<decltype(data->async_handler_recv_body)::element_type>([] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        auto const ptr = data.get();
        return ptr->async_user_body_cb->operator()(std::move(data), finish);
    });

    co_await data->async_handler_recv_body->operator() (data, finish);
}

manapi::future<CURLcode> manapi::net::fetch::async_curl_perform() {
    CURLcode res;

    try {
        using promise = async::promise<CURLcode, std::false_type>;
        res = co_await promise ([this] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            manapi::async::current()->eventloop()->watch_curl(this->data->data_->curl, std::move(resolve));
        });

        if (res == CURLE_OK && this->data->data_->async_handler_recv_body && this->data->data_->async_buffer_cursor) {
            co_await this->data->data_->async_handler_recv_body->operator()(this->data->data_, true);
        }
    }
    catch (...) {
        res = CURLE_AGAIN;
    }

    co_return res;
}

void manapi::net::fetch::handle_body(std::move_only_function<ssize_t(char *, ssize_t)> handler) {
    if (this->data->data_->async_handler_recv_body) {
        this->data->data_->async_handler_recv_body = nullptr;
        this->data->data_->async_user_body_cb.reset();
    }

    this->data->data_->sync_user_body_cb = std::make_unique<decltype(handler)>(std::move(handler));

    this->setup_parallel_task();

    this->data->data_->async_handler_recv_body
        = std::make_unique<decltype(this->data->data_->async_handler_recv_body)::element_type>(
        [](std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        if (!co_await fetch::handle_body_verify(data)) {
            co_return;
        }

        if (!data->sync_user_body_cb) {
            /* handler is async for now */
            co_await handle_async_body_finish(data, finish);
            co_return;
        }

        co_await handle_sync_body_finish(data, finish);
    });

    this->data->data_->handler_recv_body
    = std::make_unique<decltype(this->data->data_->handler_recv_body)::element_type>(
        [this] (char *buffer, size_t buffer_size) -> ssize_t {
            auto data = this->data->data_;


            if (!this->data->data_->async_run->try_to_lock()) {
                /** something get wrong **/
                return CURL_WRITEFUNC_ERROR;
            }

            data->parallel_task->operator()();

            return CURL_WRITEFUNC_PAUSE;
    });
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
    if (!this->data->data_->async_buffer) {
        this->data->data_->async_buffer = fetch::bufferpool.get();
    }
    this->data->data_->async_buffer->resize(65536);
    this->data->data_->sync_user_body_cb.reset();
    this->data->data_->async_buffer_cursor = 0;

    this->setup_parallel_task();

    this->data->data_->async_user_body_cb = std::make_unique<decltype(this->data->data_->async_user_body_cb)::element_type>(
        [handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
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

            if (data->flags & FLAG_DATA_CLOSED) {
                data->async_run->unlock();
                co_return;
            }

            if (rhs < 0) {
                if (finish) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "handle_async_body(...): Write handler error : {}", rhs);
                }

                data->async_buffer_cursor = 0;
                manapi::async::current()->eventloop()->unwatch_curl(data->curl);
                data->async_run->unlock();

                co_return;
            }
            total += rhs;
        }

        data->async_buffer_cursor = 0;

        if (finish) {
            co_return;
        }

        data->async_run->unlock();
        manapi::async::current()->eventloop()->unpause_watch_curl(data->curl);
    });

    this->data->data_->async_handler_recv_body
        = std::make_unique<decltype(this->data->data_->async_handler_recv_body)::element_type>([] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        if (!co_await fetch::handle_body_verify(data)) {
            co_return;
        }

        if (!data->async_user_body_cb) {
            /* handler body isn't async for now */
            co_return co_await handle_sync_body_finish (data, finish);
        }

        co_await handle_async_body_finish (data, finish);
    });

    this->data->data_->handler_recv_body
    = std::make_unique<decltype(this->data->data_->handler_recv_body)::element_type>([this] (char *buffer, ssize_t size) -> ssize_t {
        if (this->data->data_->async_buffer_cursor < this->data->data_->async_buffer->size()
            && this->data->data_->async_buffer->size() - this->data->data_->async_buffer_cursor >= size) {
            const auto copy = size;
            memcpy(this->data->data_->async_buffer->as<char>() + this->data->data_->async_buffer_cursor, buffer, copy);
            this->data->data_->async_buffer_cursor += copy;

            return copy;
        }

        /* in the loop event */
        if (!this->data->data_->async_run->try_to_lock()) {
            /** something get wrong **/
            return CURL_WRITEFUNC_ERROR;
        }

        this->data->data_->parallel_task->operator()();

        return CURL_WRITEFUNC_PAUSE;
    });
}

void manapi::net::fetch::handle_headers(std::move_only_function<bool(std::map <std::string, std::string>)> handler) {
    if (this->data->data_->async_handler_headers) { this->data->data_->async_handler_headers = {nullptr}; }
    this->data->data_->handler_headers = std::make_unique<decltype(handler)>(std::move(handler));
}

void manapi::net::fetch::handle_async_headers(std::move_only_function<manapi::future<bool>(std::map<std::string, std::string>)> handler) {
    if (this->data->data_->handler_headers) { this->data->data_->handler_headers = {nullptr}; }
    this->data->data_->async_handler_headers
    = std::make_unique<decltype(this->data->data_->async_handler_headers)::element_type>(
        [handler = std::move(handler)] (std::shared_ptr<shared_data> data, std::map<std::string, std::string> headers) mutable
        -> manapi::future<bool> { return handler(std::move(headers)); });
}

void manapi::net::fetch::enable_alpn(bool status) {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
}

void manapi::net::fetch::enable_http3() {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
}

void manapi::net::fetch::enable_http2() {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
}

void manapi::net::fetch::enable_http1_1() {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
}

void manapi::net::fetch::body(curlformdata params) {
    this->data->body_ = BODY_MULTIPART;
    this->data->body_formdata_ = std::move(params);
}


void manapi::net::fetch::method(std::string method) {
    this->data->method_ = std::move(method);
}

void manapi::net::fetch::body(std::string data) {
    this->data->body_ = BODY_PLAIN;
    this->data->body_default_ = std::move(data);
}

manapi::future<> manapi::net::fetch::body(file_transfer_info file_info) {
    auto file = std::make_shared <manapi::filesystem::fstream> (file_info.filelocal());
    co_await file->open(ev::FS_O_RDONLY);
    if (!file->is_open()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to open the file: {}", file_info.filelocal());
    }

    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, co_await file->size());

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
    this->data->body_ = BODY_CALLBACK;

    if (!this->data->data_->async_buffer) {
        this->data->data_->async_buffer = fetch::bufferpool.get();
    }
    this->data->data_->async_buffer->resize(65536);
    this->data->data_->async_buffer_cursor = 0;

    this->data->data_->async_handler_send_body
    = std::make_unique<decltype(this->data->data_->async_handler_send_body)::element_type>([handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        while (data->async_buffer_cursor < data->async_buffer->size()) {
            ssize_t rhs = -1;
            try {
                rhs = co_await handler (data->async_buffer->as<char>() + data->async_buffer_cursor, static_cast<ssize_t>(data->async_buffer->size() - data->async_buffer_cursor));
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG( "set_async_body(...) failed: {}", e.what());
            }
            if (rhs < 0) {
                /* error */
                if (finish) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "set_async_body(...): read handler error : {}", rhs);
                }

                data->async_buffer_cursor = 0;
                manapi::async::current()->eventloop()->unwatch_curl(data->curl);
                data->async_run->unlock();

                co_return;
            }

            if (rhs == 0) {
                /* eof */
                data->flags |= FLAG_DATA_EOF;
                break;
            }

            data->async_buffer_cursor += rhs;
        }

        if (finish) {
            co_return;
        }

        data->async_run->unlock();
        manapi::async::current()->eventloop()->unpause_watch_curl(data->curl);
    });

    this->data->data_->handler_send_body
    = std::make_unique<decltype(this->data->data_->handler_send_body)::element_type>([this, index = ssize_t{0}, result = 0] (char *buffer, ssize_t size) mutable -> ssize_t {
        size = std::min(this->data->data_->async_buffer_cursor-index, size);
        if (size > 0) {
            memcpy(buffer, this->data->data_->async_buffer->as<char>() + index, size);
            index += size;

            if (this->data->data_->async_buffer_cursor == index) {
                this->data->data_->async_buffer_cursor = 0;
                index = 0;
            }

            //MANAPIHTTP_LOG("SEND {}", size);
            result+= size;
            return size;
        }

        if (this->data->data_->flags & FLAG_DATA_EOF) {
            if (!(this->data->flags & FLAG_CONTENT_LENGTH)) {

            }
            //std::cout << "fetch send result: " << result << "\n";
            /* end of stream */
            return 0;
        }

        /* in the loop event */
        if (!this->data->data_->async_run->try_to_lock()) {
            /** something get wrong **/
            return CURL_READFUNC_ABORT;
        }

        manapi::async::current()->etaskpool()->append_task([data = this->data->data_] ()
            -> void { async::run(data->async_handler_send_body->operator()(data, false)); });

        return CURL_READFUNC_PAUSE;
    });
}

void manapi::net::fetch::body(std::move_only_function<ssize_t(char *, ssize_t)> handler) {
    this->data->body_ = BODY_CALLBACK;
    this->data->data_->async_handler_send_body = nullptr;
    this->data->data_->handler_send_body = std::make_unique<decltype(handler)>(std::move(handler));
}

void manapi::net::fetch::headers(std::map<std::string, std::string> headers) {
    {
        auto content_length = headers.find(http::HEADER.CONTENT_LENGTH);
        if (content_length != headers.end()) {
            this->data->flags |= FLAG_CONTENT_LENGTH;
            this->data->content_length_ = std::stoll(content_length->second);
            headers.erase(content_length);
        }
    }

    if (headers.contains(http::HEADER.TRANSFER_ENCODING)) {
        this->data->flags |= FLAG_TRANSFER_ENCODING;
    }

    // headers
    for (auto &header: headers)
    {
        this->data->data_->curl_headers.reset(curl_slist_append(this->data->data_->curl_headers.release(), manapi::net::http::stringify_header(header).data()));
    }
}

void manapi::net::fetch::header_(std::string key, std::string value) {
    this->data->data_->curl_headers.reset(curl_slist_append(this->data->data_->curl_headers.release(), manapi::net::http::stringify_header({std::move(key), std::move(value)}).data()));
}

void manapi::net::fetch::json_headers(manapi::json headers) {
    {
        auto &m = headers.entries();
        auto content_length = m.find(http::HEADER.CONTENT_LENGTH);
        if (content_length != m.end()) {
            this->data->flags |= FLAG_CONTENT_LENGTH;
            this->data->content_length_ = content_length->second.as_integer_cast();
            headers.erase(content_length);
        }
    }

    if (headers.contains(http::HEADER.TRANSFER_ENCODING)) {
        this->data->flags |= FLAG_TRANSFER_ENCODING;
    }

    // headers
    for (auto &header: headers.entries())
    {
        this->data->data_->curl_headers.reset(curl_slist_append(this->data->data_->curl_headers.release(), manapi::net::http::stringify_header({header.first, std::move(header.second.as_string_cast())}).data()));
    }
}

void manapi::net::fetch::custom_setup(std::move_only_function<void(CURL *curl)> func) {
    this->data->data_->handle_custom_setup = std::make_unique<decltype(func)>(std::move(func));
}

void manapi::net::fetch::enable_ssl_verify(const bool &status) {
    auto lstatus = static_cast<long> (status);
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
}

void manapi::net::fetch::verbose(bool status) {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
}

void manapi::net::fetch::timeout(const std::size_t &seconds) {
    curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_CONNECTTIMEOUT_MS, seconds * 1000);
}

void manapi::net::fetch::break_write_loop() {
    curl_easy_pause(this->data->data_->curl.get(), CURLPAUSE_RECV);
}

void manapi::net::fetch::continue_write_loop() {
    curl_easy_pause(this->data->data_->curl.get(), CURLPAUSE_CONT);
}

size_t manapi::net::fetch::status_code() const {
    return this->data->status_code_;
}

#endif