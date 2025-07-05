#include "services/ManapiFetch.hpp"

#include "string.h"

#include "services/ManapiFetch.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY

#include <exception>

#include "ManapiHttp.hpp"
#include "../include/ManapiUtils.hpp"

#include "async/ManapiAsyncPromise.hpp"
#include "ManapiString.hpp"

// Utils

enum status_flags {
    FLAG_TRANSFER_ENCODING = 1,
    FLAG_CONTENT_LENGTH = 2,
    FLAG_WAS_USED = 4,
    FLAG_STATUS_PASSED = 8,
    FLAG_POST = 16,
    FLAG_HEAD = 32,
    FLAG_PUT = 64,
    FLAG_DELETE = 128,
    FLAG_TRACE = 256
};
enum status_data_flags {
    FLAG_DATA_EOF = 1,
    FLAG_DATA_CLOSED = 2
};

struct manapi::net::fetch::shared_data {
    int flags{0};

    std::unique_ptr<async::mutex> async_run{nullptr};
    std::unique_ptr<std::move_only_function<void()>> parallel_task{nullptr};

    std::size_t async_buffer_size;
    manapi::slice async_buffer{};
    std::shared_ptr<CURL> curl {nullptr};
    std::unique_ptr<std::map<std::string, std::string>> headers{nullptr};
    std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers {nullptr};

    std::unique_ptr<std::move_only_function <ssize_t(char *, ssize_t)>> handler_recv_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)>> async_handler_recv_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<bool>(std::shared_ptr<shared_data> data, std::map <std::string, std::string>)>> async_handler_headers{nullptr};
    std::unique_ptr<std::move_only_function <bool(std::map <std::string, std::string>)>> handler_headers{nullptr};

    std::unique_ptr<std::move_only_function<manapi::future<>(std::shared_ptr<shared_data> data, bool)>> async_user_body_cb{nullptr};
    std::unique_ptr<std::move_only_function<ssize_t(char *buffer, ssize_t size)>> sync_user_body_cb{nullptr};
    std::unique_ptr<std::move_only_function <ssize_t(char *, ssize_t)>> handler_send_body{nullptr};
    std::unique_ptr<std::move_only_function <manapi::future<>(std::shared_ptr<shared_data> data, bool finish)>> async_handler_send_body{nullptr};

};

struct manapi::net::fetch::data_t {
    int flags{0};
    uint16_t status_code_{200};
    ssize_t content_length_{-1};

    manapi::async::cancellation_action cancellation{nullptr};


    std::shared_ptr<shared_data> data_{nullptr};

    body_type body_{BODY_NONE};

    std::string url_{};
    std::unique_ptr<std::string> body_default_{};
    std::unique_ptr<std::string> method_{};

    std::unique_ptr<curlformdata> body_formdata_{};
};

size_t manapi::net::fetch::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    auto const f = static_cast <fetch::shared_data *> (userdata);
    try {
        /**
         * clear body send callback
         */
        f->async_buffer_size = 0;

        if (f->flags & FLAG_STATUS_PASSED) {
            std::string_view const str (buffer, size * n_items - 2);
            if (!str.empty()) {
                auto res = manapi::net::http::parse_header(str);
                if (res.ok()) {
                    auto header = res.unwrap();
                    auto key = std::string{header.first};
                    manapi::string::lower_ascii(key);
                    f->headers->insert({std::move(key), std::string{header.second}});
                }
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
            manapi::async::current()->etaskpool()->append_task([data] () mutable -> void {
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

manapi::future<manapi::error::status> manapi::net::fetch::async_doit() {
    std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

    if (this->data->flags & FLAG_WAS_USED)
        co_return error::status_failed_precondition("fetch was already used. create another fetch object or reinit current",
            {{"url", this->data->url_}});

    this->data->flags |= FLAG_WAS_USED;

    if (!this->data->data_->curl)
        co_return error::status_internal("curl can not be init", {{"url", this->data->url_}});

    CURLcode status;
    std::exception_ptr err{nullptr};

    try {
        CURLcode resp;

        if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_TCP_NODELAY, 1L)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_NOSIGNAL, 1L)) != CURLE_OK)
            goto errjmp;
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MAXLIFETIME_CONN, 1L);
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MAXAGE_CONN, 0);
        // curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_TCP_KEEPALIVE, 0L);
        if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_URL, this->data->url_.data())) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HEADERDATA, this->data->data_.get())) != CURLE_OK)
            goto errjmp;

        {
            bool optex = true;
            CURLoption opt;
            char const *method{nullptr};

            if (this->data->method_)
                method = this->data->method_->data();
            else {
                if (this->data->flags & FLAG_POST)
                    opt = CURLOPT_POST;
                else if (this->data->flags & FLAG_PUT)
                    opt = CURLOPT_UPLOAD;
                else if (this->data->flags & FLAG_HEAD)
                    opt = CURLOPT_NOBODY;
                else if (this->data->flags & FLAG_DELETE)
                    method = "DELETE";
                else if (this->data->flags & FLAG_TRACE)
                    method = "TRACE";
                else
                    optex = false;
            }

            if (method) {
                if((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_CUSTOMREQUEST, method))!=CURLE_OK)
                    goto errjmp;
            }
            else if (optex) {
                if ((status = curl_easy_setopt(this->data->data_->curl.get(), opt, 1))!=CURLE_OK)
                    goto errjmp;
            }
        }

        // if handler has been set
        if (this->data->data_->handler_recv_body)
        {
            if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler))!=CURLE_OK)
                goto errjmp;
            if ((status = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_WRITEDATA, this->data->data_.get()))!=CURLE_OK)
                goto errjmp;
        }

        // body of the request
        switch (this->data->body_) {
            case BODY_PLAIN: {
                if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDS, this->data->body_default_->data()))!=CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->body_default_->size()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            case BODY_CALLBACK: {
                if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_READFUNCTION, curl_read_handler)) != CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_READDATA, this->data->data_.get()))!=CURLE_OK)
                    goto errjmp;

                if (this->data->flags & FLAG_CONTENT_LENGTH) {
                    if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->content_length_)) != CURLE_OK)
                        goto errjmp;
                }

                break;
            }
            case BODY_MULTIPART: {
                form.reset(curl_mime_init(this->data->data_->curl.get()));
                curl_mimepart *field = nullptr;
                for (auto &param : *this->data->body_formdata_) {
                    field = curl_mime_addpart(form.get());
                    if ((status=curl_mime_name(field, param.first.data()))!=CURLE_OK)
                        goto errjmp;
                    switch (param.second.type) {
                        case curlformdata::PARAM_DEFAULT: {
                            auto str = param.second.strdata.value();
                            if ((status = curl_mime_data(field, str.data(), str.size())) != CURLE_OK)
                                goto errjmp;
                            break;
                        }
                        case curlformdata::PARAM_FILE: {
                            auto str = param.second.strdata.value();
                            if ((status=curl_mime_filedata(field, str.data()))!=CURLE_OK)
                                goto errjmp;
                            break;
                        }
                        case curlformdata::PARAM_CALLBACK: {
                            auto &cbdata = param.second.filedata.value();
                            if ((status=curl_mime_data_cb(field, cbdata.filesize, curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback))!=CURLE_OK)
                                goto errjmp;
                            break;
                        }
                    }
                }
                if ((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_MIMEPOST, form.get()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            default:
                break;
        }

        if (this->data->data_->curl_headers)
        {
            if((status=curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTPHEADER, this->data->data_->curl_headers.get()))!=CURLE_OK)
                goto errjmp;
        }

        this->data->data_->headers = std::make_unique<decltype(this->data->data_->headers)::element_type>();

        try {
            if (this->data->cancellation.contains_cancel_callback()) {
                this->data->cancellation.cancel_callback([data = this->data->data_->curl] () mutable -> void {
                    if (data) {
                        manapi::async::current()->eventloop()->unwatch_curl(std::move(data));
                    }
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

        if (resp != CURLE_OK) {
            this->clear_();
            co_return error::status_internal("Connection failed", {
                {"url", this->data->url_},
                {"code", static_cast<int>(resp)},
                {"msg", curl_easy_strerror(resp)}
            });
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

        if ((status=curl_easy_getinfo(this->data->data_->curl.get(), CURLINFO_HTTP_CODE, &this->data->status_code_))!=CURLE_OK)
            goto errjmp;
    }
    catch (...) {
        err = std::current_exception();
    }

    this->clear_();

    if (err)
        std::rethrow_exception(std::move(err));
    co_return error::status_ok();
errjmp:
    this->clear_();
    co_return error::status_invalid_argument(curl_easy_strerror(status));
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
    *this->data = fetch::data_t{.data_ = std::move(this->data->data_), .url_ = std::move(this->data->url_)};
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
        data->async_buffer_size = 0;
        async::current()->eventloop()->unwatch_curl(data->curl);
        data->async_run->unlock();
        co_return false;
    }
    co_return true;
}

manapi::future<void> manapi::net::fetch::handle_sync_body_finish(std::shared_ptr<shared_data> data, bool finish) {
    try {
        data->handler_recv_body = std::move(data->sync_user_body_cb);

        if (finish) {
            co_return;
        }

        if (data->async_buffer_size > 0) {
            auto res = data->async_buffer.subslice(0, data->async_buffer_size);
            data->async_buffer_size = 0;
            if (!res.ok())
                goto err;
            auto buffs = res.unwrap();
            for (auto it = buffs.begin(); it != buffs.end(); ++it) {
                /* cached */
                ssize_t current = 0, rhs = 0;
                auto const size = static_cast<ssize_t>(it.size());
                auto const buff = static_cast<char*>(it.buffer());

                while (current < size) {
                    if ((rhs = data->handler_recv_body->operator()(buff + current, size - current)) < 0) {
                        goto err;
                    }

                    current += rhs;
                }
            }
        }
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("body finish failed due to {}", e.what());
    }

    data->async_handler_recv_body = nullptr;
    data->async_run->unlock();
    manapi::async::current()->eventloop()->unpause_watch_curl(data->curl);
    co_return;
    err:
    data->async_buffer_size = 0;
    manapi::async::current()->eventloop()->unwatch_curl(data->curl);
    data->async_run->unlock();
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

        if (res == CURLE_OK && this->data->data_->async_handler_recv_body && this->data->data_->async_buffer_size) {
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

    auto res = co_await async_doit();
    res.unwrap();
    co_return content;
}

manapi::future<manapi::json> manapi::net::fetch::json() {
    co_return std::move(manapi::json (co_await text(), true));
}


void manapi::net::fetch::handle_async_body(std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool fin)> handler) {
    if (this->data->data_->async_buffer.empty())
        this->data->data_->async_buffer = manapi::async::current()->memory_fabric().slice(65536);

    this->data->data_->sync_user_body_cb.reset();
    this->data->data_->async_buffer_size = 0;

    this->setup_parallel_task();

    this->data->data_->async_user_body_cb = std::make_unique<decltype(this->data->data_->async_user_body_cb)::element_type>(
        [handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        ssize_t rhs = -1;
        bool fin = false;

        if (data->async_buffer_size) {
            try {
                rhs = co_await handler (data->async_buffer.subslice(0, data->async_buffer_size).unwrap(), fin);
            }
            catch (std::exception const &e) {
                // DEBUG
                MANAPIHTTP_LOG("handle_async_body(...) failed: {}", e.what());
            }

            if (data->flags & FLAG_DATA_CLOSED) {
                data->async_run->unlock();
                co_return;
            }

            if (rhs != data->async_buffer_size) {
                if (finish)
                    THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "handle_async_body(...): Write handler error : {}", rhs);

                data->async_buffer_size = 0;
                manapi::async::current()->eventloop()->unwatch_curl(data->curl);
                data->async_run->unlock();

                co_return;
            }

            data->async_buffer_size = 0;
        }

        if (finish)
            co_return;

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
        if (this->data->data_->async_buffer_size < this->data->data_->async_buffer.size()
            && this->data->data_->async_buffer.size() - this->data->data_->async_buffer_size >= size) {
            const auto copy = size;
            auto res = this->data->data_->async_buffer.copy_from(buffer, this->data->data_->async_buffer_size, copy);
            if (!res.ok())
                return CURL_WRITEFUNC_ERROR;

            this->data->data_->async_buffer_size += copy;

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

manapi::error::status manapi::net::fetch::enable_alpn(bool status) {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
    return res == CURLE_OK ? error::status_ok() : error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::enable_http3() {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    return res == CURLE_OK ? error::status_ok() : error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::enable_http2() {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    return res == CURLE_OK ? error::status_ok() : error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::enable_http1_1() {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    return res == CURLE_OK ? error::status_ok() : error::status_invalid_argument(curl_easy_strerror(res));
}

void manapi::net::fetch::body(curlformdata params) {
    this->data->body_ = BODY_MULTIPART;
    if (!this->data->body_formdata_)
        this->data->body_formdata_ = std::make_unique<curlformdata>(std::move(params));
    else
        *this->data->body_formdata_ = std::move(params);
}


void manapi::net::fetch::method(std::string_view method) {
    int flag = 0;
    if (manapi::string::equals("get", method, 0b01))
        return;
    if (string::equals("post", method, 0b01))
        flag = FLAG_POST;
    else if (string::equals("delete", method, 0b01))
        flag = FLAG_DELETE;
    else if (string::equals("put", method, 0b01))
        flag = FLAG_PUT;
    else if (string::equals("head", method, 0b01))
        flag = FLAG_HEAD;
    else if (string::equals("trace", method, 0b01))
        flag = FLAG_TRACE;
    if (flag) {
        this->data->flags |= flag;
    }
    else {
        if (this->data->method_)
            *this->data->method_ = method;
        else
            this->data->method_ = std::make_unique<std::string>((method));
    }
}

void manapi::net::fetch::body(std::string data) {
    this->data->body_ = BODY_PLAIN;
    if (!this->data->body_default_)
        this->data->body_default_ = std::make_unique<std::string>(std::move(data));
    else
        *this->data->body_default_ = std::move(data);
}

manapi::future<manapi::error::status> manapi::net::fetch::body(file_transfer_info file_info) {
    auto file = std::make_shared <manapi::filesystem::fstream> (file_info.filelocal());
    auto res = co_await file->open(ev::FS_O_RDONLY);
    if (!res.ok())
        co_return std::move(res);

    auto code = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDS, nullptr);
    if (code != CURLE_OK)
        goto err;

    code = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, co_await file->size());
    if (code != CURLE_OK)
        goto err;

    this->async_body([file = std::move(file), file_info = std::move(file_info)] (slice_view buffs, bool &fin) mutable
        -> manapi::future<ssize_t> {
        auto rhs = co_await file->fread(buffs);

        if (file->eof())
            fin = true;

        if (rhs < 0) {
            /* error */
            co_return -1;
        }

        co_return rhs;
    });
    co_return error::status_ok();
    err: co_return error::status_invalid_argument(curl_easy_strerror(code));
}

void manapi::net::fetch::async_body(std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> handler) {
    this->data->body_ = BODY_CALLBACK;

    if (this->data->data_->async_buffer.empty())
        this->data->data_->async_buffer = manapi::async::current()->memory_fabric().slice(65536);

    this->data->data_->async_buffer_size = 0;

    this->data->data_->async_handler_send_body
    = std::make_unique<decltype(this->data->data_->async_handler_send_body)::element_type>([handler = std::move(handler)] (std::shared_ptr<shared_data> data, bool finish) mutable -> manapi::future<> {
        while (!data->async_buffer_size) {
            ssize_t rhs = -1;
            bool fin = false;
            try {
                rhs = co_await handler (data->async_buffer, fin);
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG( "set_async_body(...) failed: {}", e.what());
            }

            if (rhs < 0) {
                /* error */
                if (finish)
                    THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "set_async_body(...): read handler error : {}", rhs);


                data->async_buffer_size = 0;
                manapi::async::current()->eventloop()->unwatch_curl(data->curl);
                data->async_run->unlock();

                co_return;
            }

            if (fin || !rhs) {
                /* eof */
                data->flags |= FLAG_DATA_EOF;

                if (!rhs)
                    break;
            }

            data->async_buffer_size += rhs;
        }

        if (finish)
            co_return;


        data->async_run->unlock();
        manapi::async::current()->eventloop()->unpause_watch_curl(data->curl);
    });

    this->data->data_->handler_send_body
    = std::make_unique<decltype(this->data->data_->handler_send_body)::element_type>([this, index = ssize_t{0}, result = 0] (char *buffer, ssize_t size) mutable -> ssize_t {
        size = std::min<ssize_t>(this->data->data_->async_buffer_size-index, size);
        if (size > 0) {
            this->data->data_->async_buffer.copy_to(buffer, index, size);
            index += size;

            if (this->data->data_->async_buffer_size == index) {
                this->data->data_->async_buffer_size = 0;
                index = 0;
            }

            //MANAPIHTTP_LOG("SEND {}", size);
            result += size;
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

const std::shared_ptr<CURL> & manapi::net::fetch::custom() {
    return this->data->data_->curl;
}

manapi::error::status manapi::net::fetch::enable_verify_peer(bool status) {
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    if (res != CURLE_OK)
        goto err;
    res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
    if (res != CURLE_OK)
        goto err;
    return error::status_ok();
    err:
    return error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::enable_verify_host(bool status) {
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    if (res != CURLE_OK)
        goto err;
    res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
    if (res != CURLE_OK)
        goto err;
    return error::status_ok();
err:
    return error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::verbose(bool status) {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
    if (res == CURLE_OK)
        return error::status_ok();
    return error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::timeout(std::size_t seconds) {
    auto const res = curl_easy_setopt(this->data->data_->curl.get(), CURLOPT_CONNECTTIMEOUT_MS, seconds * 1000);
    if (res == CURLE_OK)
        return error::status_ok();
    return error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::break_write_loop() {
    auto const res = curl_easy_pause(this->data->data_->curl.get(), CURLPAUSE_RECV);
    if (res == CURLE_OK)
        return error::status_ok();

    return error::status_invalid_argument(curl_easy_strerror(res));
}

manapi::error::status manapi::net::fetch::continue_write_loop() {
    auto const res = curl_easy_pause(this->data->data_->curl.get(), CURLPAUSE_CONT);
    if (res == CURLE_OK)
        return error::status_ok();

    return error::status_invalid_argument(curl_easy_strerror(res));
}

size_t manapi::net::fetch::status_code() const {
    return this->data->status_code_;
}

#endif