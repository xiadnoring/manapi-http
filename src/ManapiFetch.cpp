#include "ManapiFetch.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY

#   include <exception>
#   include <cstring>

#   include <curl/curl.h>

#   include "ManapiHttp.hpp"
#   include "ManapiString.hpp"
#   include "std/ManapiAsyncPromise.hpp"
#   include "./include/ManapiUtils.hpp"

#   define MANAPIHTTP_CURL_VERSION_REQUIRE(major, minor, patch) MANAPIHTTP_SINCE_AT_CUSTOM(LIBCURL_VERSION_MAJOR,LIBCURL_VERSION_MINOR,LIBCURL_VERSION_PATCH, major, minor, patch)

// Utils

enum body_type {
    BODY_NONE = 0,
    BODY_PLAIN = 1,
    BODY_MULTIPART = 2,
    BODY_CALLBACK = 3
};

enum status_flags {
    FLAG_POST = 1<<0,
    FLAG_HEAD = 1<<1,
    FLAG_PUT = 1<<2,
    FLAG_DELETE = 1<<3,
    FLAG_TRACE = 1<<4,
    FLAG_TRANSFER_ENCODING = 1<<5,
    FLAG_CONTENT_LENGTH = 1<<6,
    FLAG_WAS_USED = 1<<7,
    FLAG_STATUS_PASSED = 1<<8,
    FLAG_DATA_EOF = 1<<9,
    FLAG_DATA_CLOSED = 1<<10,
    FLAG_CALLBACK_SYNC = 1<<11
};

static constexpr uint32_t status_flags_methods = 0xFFFFFFE0;

struct curl_deleter {
    void operator() (CURL *curl)
    { curl_easy_cleanup(curl); }
};
struct curl_slist_deleter {
    void operator() (curl_slist *list)
    { curl_slist_free_all(list); }
};

struct curl_mime_deleter {
    void operator() (curl_mime *mime)
    { curl_mime_free(mime); }
};

struct manapi::net::fetch::data_t {
    int flags;
    ssize_t content_length_;

    manapi::ctoken cancellation;

    body_type body_{BODY_NONE};

    std::string url_;
    std::unique_ptr<std::string> body_default_;
    std::unique_ptr<std::string> method_;

    std::unique_ptr<fetch_formdata> body_formdata_;

    async::mutex async_run;

    uint16_t status_code_;
    std::size_t async_buffer_size;
    std::size_t async_buffer_index;
    manapi::slice async_buffer;

    std::shared_ptr<data_t> data;

    std::shared_ptr<CURL> curl;
    std::unique_ptr<std::map<std::string, std::string, std::less<>>> headers;
    std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers;

    std::size_t(*handler_recv_body)(const std::shared_ptr<data_t> &data, char *, ssize_t);
    manapi::status (*parallel_task)(const std::shared_ptr<data_t> &data);
    manapi::future<manapi::status> (*async_handler_recv_body) (std::shared_ptr<data_t> data, bool finish);

    std::unique_ptr<std::move_only_function<ssize_t(char *buffer, ssize_t size)>> sync_user_body_cb;
    std::unique_ptr<std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool finish)>> async_user_body_cb;
    std::unique_ptr<std::move_only_function <manapi::future<bool>(std::map <std::string, std::string, std::less<>>)>> async_handler_headers;
    std::unique_ptr<std::move_only_function <bool(std::map <std::string, std::string, std::less<>>)>> handler_headers;
    std::unique_ptr<std::move_only_function <ssize_t(char *, ssize_t)>> handler_send_body;
    std::unique_ptr<std::move_only_function <manapi::future<ssize_t>(slice_view buffs, bool &fin)>> async_handler_send_body;
};


static manapi::future<manapi::status> curl_send_async_body (std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) {
    while (!data->async_buffer_size) {
        ssize_t rhs = -1;
        bool fin = false;
        try {
            rhs = co_await data->async_handler_send_body->operator() (data->async_buffer, fin);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG( "set_async_body(...) failed: {}", e.what());
        }

        if (rhs < 0) {
            /* error */
            if (finish) {
                co_return manapi::status_aborted("fetch:User async send callback failed");
            }


            data->async_buffer_size = 0;
            auto err = manapi::async::current()->eventloop()->unwatch_curl(&data->curl);
            data->async_run.unlock();

            co_return std::move(err);
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
        co_return manapi::status_ok();


    data->async_run.unlock();
    co_return manapi::async::current()->eventloop()->unpause_watch_curl(&data->curl);
}

static void curl_send_query_to_send_data (std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) MANAPIHTTP_NOEXCEPT {
    manapi::future<manapi::status> task{nullptr};
    MANAPIHTTP_MUST_ALLOC_START
    task = curl_send_async_body(data, finish);
    MANAPIHTTP_MUST_ALLOC_END
    manapi::async::run(std::move(task));
}

static std::size_t curl_send_async_continue (const std::shared_ptr<manapi::net::fetch::data_t> &data, char *buffer, ssize_t size) MANAPIHTTP_NOEXCEPT {
    try {
        size = std::min<ssize_t>(data->async_buffer_size - data->async_buffer_index, size);
        if (size > 0) {
            auto err = data->async_buffer.copy_to(buffer, data->async_buffer_index, size);
            if (!err) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s", "fetch", "curl_send_async_continue()",
                    err.msg().size(), err.msg().data());
                return CURL_READFUNC_ABORT;
            }

            data->async_buffer_index += size;

            if (data->async_buffer_size == data->async_buffer_index) {
                data->async_buffer_size = 0;
                data->async_buffer_index = 0;
            }

            return size;
        }

        if (data->flags & FLAG_DATA_EOF) {
            if (!(data->flags & FLAG_CONTENT_LENGTH)) {

            }

            /* end of stream */
            return 0;
        }

        /* in the loop event */
        if (!data->async_run.try_to_lock()) {
            /** something get wrong **/
            return CURL_READFUNC_ABORT;
        }

        manapi::async::current()->etaskpool()->append_task([data] () mutable
            -> void { curl_send_query_to_send_data(std::move(data), false); });

        return CURL_READFUNC_PAUSE;
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "fetch", "curl_send_async_continue()", e.what());
        return CURL_READFUNC_ABORT;
    }
}

size_t manapi::net::fetch::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    auto const f = static_cast <fetch::data_t *> (userdata);
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
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_header_handler", e.what());
    }

    return -1;
}

size_t manapi::net::fetch::curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p) {
    auto f = static_cast <data_t *> (user_p);

    try {
        // call user handler
        auto const len = static_cast<ssize_t>(size * nitems);
        if (!len)
            return 0;

        return (f->handler_recv_body (f->data, buffer, len));
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_write_handler", e.what());
        return -1;
    }
}

std::size_t manapi::net::fetch::curl_read_handler(char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto f = static_cast<data_t *> (user_p);
    try {
        if (f->flags & FLAG_CALLBACK_SYNC) {
            auto res = f->handler_send_body->operator()(buffer, static_cast<ssize_t> (size * nitems));

            if (res < 0)
                return CURL_READFUNC_ABORT;

            return static_cast<std::size_t>(res);
        }

        return static_cast<std::size_t>(curl_send_async_continue (f->data, buffer, static_cast<ssize_t> (size * nitems)));
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_read_handler", e.what());
        return CURL_READFUNC_ABORT;
    }
}

static manapi::status process_accepted_data_cb (const std::shared_ptr<manapi::net::fetch::data_t> &data) MANAPIHTTP_NOEXCEPT {
    try {
        manapi::async::current()->etaskpool()->append_static_task([data] () mutable -> void {
            MANAPIHTTP_MUST_ALLOC_START
            manapi::async::run(data->async_handler_recv_body(data, false));
            MANAPIHTTP_MUST_ALLOC_END
        });

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        return manapi::status_resource_exhausted();
    }
}

static manapi::status response_headers_received_(const std::shared_ptr<manapi::net::fetch::data_t> &data) MANAPIHTTP_NOEXCEPT {
    auto res = curl_easy_getinfo(data->curl.get(), CURLINFO_HTTP_CODE, &data->status_code_);
    if (res != CURLE_OK)
        return manapi::status_internal("fetch:CURLINFO_HTTP_CODE failed");

    data->parallel_task = process_accepted_data_cb;

    data->parallel_task (data);

    return manapi::status_ok();
}

static std::size_t curl_recv_data_and_wait (const std::shared_ptr<manapi::net::fetch::data_t> &data, char *buffer, ssize_t size) {
    if (data->async_buffer_size < data->async_buffer.size()
        && data->async_buffer.size() - data->async_buffer_size >= size) {
        const auto copy = size;
        auto res = data->async_buffer.copy_from(buffer, data->async_buffer_size, copy);
        if (!res.ok())
            goto err;

        data->async_buffer_size += copy;

        return copy;
    }
    else {
        /* in the loop event */
        if (!data->async_run.try_to_lock()) {
            /** something gets wrong **/
            goto err;
        }

        auto res = data->parallel_task (data);
        if (!res) {
            data->async_run.unlock();
            goto err;
        }

        return CURL_WRITEFUNC_PAUSE;
    }
    err:
#if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
    return CURL_WRITEFUNC_ERROR;
#else
    return 0;
#endif
}

static std::size_t curl_recv_async_headers_and_continiue (const std::shared_ptr<manapi::net::fetch::data_t> &data, char *buffer, ssize_t buffer_size)  {
    if (data->async_run.try_to_lock()) {
        auto res = data->parallel_task (data);
        if (!res) {
            data->async_run.unlock();
            goto err;
        }

        return CURL_WRITEFUNC_PAUSE;
    }
err:
#if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
    return CURL_WRITEFUNC_ERROR;
#else
    return 0;
#endif

}

static std::size_t curl_recv_sync_continiue (const std::shared_ptr<manapi::net::fetch::data_t> &data, char *buffer, ssize_t buffer_size) {
    try {
        auto const rhs = data->sync_user_body_cb->operator() (buffer, buffer_size);
        if (rhs >= 0)
            return static_cast<std::size_t>(rhs);
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s: %s failed due to %s", "fetch", "recv user callback", e.what());
    }

#if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
    return CURL_WRITEFUNC_ERROR;
#else
    return 0;
#endif
}

static manapi::future<manapi::status> curl_recv_async_callback (const std::shared_ptr<manapi::net::fetch::data_t> &data, bool finish) {
    ssize_t rhs = -1;

    if (data->async_buffer_size || finish) {
        try {
            rhs = co_await data->async_user_body_cb->operator() (data->async_buffer.subslice(0, data->async_buffer_size).unwrap(), finish);
        }
        catch (std::exception const &e) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
                "fetch", "async recv user callback", e.what());
        }

        if (data->async_buffer_size) {
            if (data->flags & FLAG_DATA_CLOSED) {
                data->async_run.unlock();
                co_return manapi::status_ok();
            }

            if (rhs != data->async_buffer_size) {
                if (finish)
                    co_return manapi::status_internal("curl_recv_async_callback:User callback failed");

                data->async_buffer_size = 0;
                auto err = manapi::async::current()->eventloop()->unwatch_curl(&data->curl);
                data->async_run.unlock();

                if (err.ok())
                    co_return manapi::status_ok();

                co_return manapi::status_internal("curl_recv_async_callback:User callback failed");
            }

            data->async_buffer_size = 0;
        }
    }

    if (finish)
        co_return manapi::status_ok();

    data->async_run.unlock();
    manapi::async::current()->eventloop()->unpause_watch_curl(&data->curl);

    co_return manapi::status_ok();
}

static manapi::future<manapi::status> curl_recv_async_continiue (std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) {
    try {
        auto err = co_await curl_recv_async_callback(std::move(data), finish);
        if (!err.ok()) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s", "fetch",
                "curl_recv_async_continue", err.msg().size(), err.msg().data());
            co_return manapi::status_aborted("fetch:User callback failed");
        }
        co_return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "fetch",
            "curl_recv_async_continiue", e.what());
    }
    co_return manapi::status_internal("fetch:User callback failed");
}

static manapi::future<bool> handle_body_verify (std::shared_ptr<manapi::net::fetch::data_t> data) {
    bool flg = false;

    auto res = curl_easy_getinfo(data->curl.get(), CURLINFO_HTTP_CODE, &data->status_code_);
    if (res) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s", "fetch",
            "curl_easy_getinfo with CURLINFO_HTTP_CODE", curl_easy_strerror(res));
    }

    if (!data->status_code_)
        data->status_code_ = 500;

    try {
        if (data->async_handler_headers) {
            auto const cb = std::move(data->async_handler_headers);
            flg = co_await cb->operator()(std::move(*data->headers));
        }

        else if (data->handler_headers) {
            auto const cb = std::move(data->handler_headers);
            flg = cb->operator()(std::move(*data->headers));
        }
        else {
            flg = true;
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "handle_body_verify", e.what());
    }

    data->headers = nullptr;

    if (!flg) {
        data->async_buffer_size = 0;
        manapi::async::current()->eventloop()->unwatch_curl(&data->curl);
        data->async_run.unlock();
        co_return false;
    }

    co_return true;
}

static manapi::future<manapi::status> handle_sync_body_finish(std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) {
    manapi::status status;
    std::size_t constexpr error_value =
    #if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
        CURL_WRITEFUNC_ERROR;
    #else
        0;
    #endif

    try {
        data->handler_recv_body = curl_recv_sync_continiue;

        if (finish) {
            co_return manapi::status_ok();
        }

        if (data->async_buffer_size > 0) {
            auto res = data->async_buffer.subslice(0, data->async_buffer_size);
            data->async_buffer_size = 0;
            if (!res.ok())
                goto err;
            auto buffs = res.unwrap();
            for (auto it = buffs.begin(); it != buffs.end(); ++it) {
                /* cached */
                ssize_t current = 0;
                std::size_t rhs = 0;
                auto const size = static_cast<ssize_t>(it.size());
                auto const buff = static_cast<char*>(it.buffer());

                while (current < size) {
                    if ((rhs = data->handler_recv_body (data, buff + current, size - current)) == error_value) {
                        goto err;
                    }

                    current += rhs;
                }
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "handle_sync_body_finish", e.what());
    }

    data->async_handler_recv_body = nullptr;
    data->async_run.unlock();
    status = manapi::async::current()->eventloop()->unpause_watch_curl(&data->curl);

    co_return std::move(status);

    err:
    data->async_buffer_size = 0;
    status = manapi::async::current()->eventloop()->unwatch_curl(&data->curl);
    data->async_run.unlock();
    co_return std::move(status);
}

static manapi::future<manapi::status> handle_async_body_finish(std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) {
    data->async_handler_recv_body = curl_recv_async_continiue;
    return data->async_handler_recv_body  (data, finish);
}

static manapi::future<manapi::status> curl_recv_sync_or_async (std::shared_ptr<manapi::net::fetch::data_t> data, bool finish) {
    try {
        if (!co_await handle_body_verify(data)) {
            co_return manapi::status_ok();
        }

        if (!data->sync_user_body_cb) {
            /* handler is async for now */
            co_return co_await handle_async_body_finish(data, finish);
        }

        co_return co_await handle_sync_body_finish(data, finish);
    }
    catch (std::bad_alloc const &) {
        co_return manapi::status_resource_exhausted();
    }
}

manapi::status manapi::net::fetch::setup_parallel_task() MANAPIHTTP_NOEXCEPT {
    this->data->parallel_task = response_headers_received_;
    return status_ok();
}

void manapi::net::fetch::default_setup_curl_() {
    this->timeout(5);
}

size_t curl_send_formdata_cb_read (char *buffer, size_t size, size_t nitems, void *userp) {
    auto &func = *static_cast<decltype(manapi::net::fetch_formdata::multipart_param_value_file::callback) *> (userp);
    return func (buffer, size * nitems);
}

int curl_send_formdata_cb_seek (void *userp, curl_off_t offset, int origin) {
    return CURL_SEEKFUNC_OK;
}

void curl_send_formdata_cb_free (void *userp) {
    // pass
}


manapi::net::fetch_formdata::fetch_formdata() = default;

manapi::net::fetch_formdata::~fetch_formdata() = default;

manapi::net::fetch_formdata::fetch_formdata(fetch_formdata &&fd)  MANAPIHTTP_NOEXCEPT : mdata(std::move(fd.mdata)) {}

manapi::net::fetch_formdata & manapi::net::fetch_formdata::operator=(fetch_formdata &&fd) MANAPIHTTP_NOEXCEPT {
    this->mdata = std::move(fd.mdata);
    return *this;
}

manapi::status manapi::net::fetch_formdata::set_text(std::string name, std::string value) MANAPIHTTP_NOEXCEPT {
    try {
        multipart_param_value res{};
        res.strdata = std::move(value);
        res.type = PARAM_DEFAULT;
        auto const it = this->mdata.insert({std::move(name), std::move(res)});
        if (!it.second)
            return status_already_exists("formdata:param exists");
        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::fetch_formdata::set_file(std::string name, std::string filepath) MANAPIHTTP_NOEXCEPT {
    try {
        multipart_param_value res{};
        res.strdata = std::move(filepath);
        res.type = PARAM_FILE;
        auto const it =this->mdata.insert({std::move(name), std::move(res)});

        if (!it.second)
            return status_already_exists("formdata:param exists");
        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::fetch_formdata::set_callback(std::string name, ssize_t size, std::move_only_function<size_t(void *buff, size_t buff_size)> cb) MANAPIHTTP_NOEXCEPT {
    try {
        multipart_param_value res{};
        res.filedata = multipart_param_value_file({std::move(cb), size});
        res.type = PARAM_CALLBACK;
        auto const it = this->mdata.insert({std::move(name), std::move(res)});
        if (!it.second)
            return status_already_exists("formdata:param exists");
        return status_ok();
    }
    catch (...) {
        return status_resource_exhausted();
    }
}

void manapi::net::fetch_formdata::clear() MANAPIHTTP_NOEXCEPT {
    this->mdata.clear();
}

manapi::net::fetch_formdata::tdata::iterator manapi::net::fetch_formdata::begin() MANAPIHTTP_NOEXCEPT {
    return this->mdata.begin();
}

manapi::net::fetch_formdata::tdata::iterator manapi::net::fetch_formdata::end() MANAPIHTTP_NOEXCEPT {
    return this->mdata.end();
}


manapi::net::fetch::fetch() {
    this->data = {};
}

manapi::net::fetch::fetch(const fetch &n) {
    this->data = n.data;
}

manapi::net::fetch::~fetch() {

}

manapi::status_or<manapi::net::fetch> manapi::net::fetch::create(std::string url,manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    fetch response;

    auto res = response.init(std::move(url), std::move(cancellation));
    if (!res)
        return std::move(res);

    return std::move(response);
}

manapi::status manapi::net::fetch::init(std::string url, manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->data)
            this->data = std::make_shared<fetch::data_t>(fetch::data_t{});

        this->data->url_ = std::move(url);
        this->data->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_easy_cleanup);
        this->data->cancellation = std::move(cancellation);
        this->data->content_length_ = -1;

        this->default_setup_curl_();
        
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::net::fetch & manapi::net::fetch::operator=(const fetch &n) = default;

manapi::future<manapi::status> manapi::net::fetch::async_doit() {
    CURLcode status;
    try {
        this->data->data = this->data;

        std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

        if (this->data->flags & FLAG_WAS_USED)
            co_return status_already_exists("fetch was already used. create another fetch object or reinit current");

        this->data->flags |= FLAG_WAS_USED;

        if (!this->data->curl)
            co_return status_internal("curl can not be init");

        int resp;

        if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_TCP_NODELAY, 1L)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_NOSIGNAL, 1L)) != CURLE_OK)
            goto errjmp;
        // curl_easy_setopt(this->data->curl.get(), CURLOPT_MAXLIFETIME_CONN, 1L);
        // curl_easy_setopt(this->data->curl.get(), CURLOPT_MAXAGE_CONN, 0);
        // curl_easy_setopt(this->data->curl.get(), CURLOPT_TCP_KEEPALIVE, 0L);
        if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_URL, this->data->url_.data())) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_HEADERDATA, this->data.get())) != CURLE_OK)
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
                if((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_CUSTOMREQUEST, method))!=CURLE_OK)
                    goto errjmp;
            }
            else if (optex) {
                if ((status = curl_easy_setopt(this->data->curl.get(), opt, 1))!=CURLE_OK)
                    goto errjmp;
            }
        }

        // if handler has been set
        if (this->data->handler_recv_body)
        {
            if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler))!=CURLE_OK)
                goto errjmp;
            if ((status = curl_easy_setopt(this->data->curl.get(), CURLOPT_WRITEDATA, this->data.get()))!=CURLE_OK)
                goto errjmp;
        }


        if (this->data->curl_headers)
        {
            if((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTPHEADER, this->data->curl_headers.get()))!=CURLE_OK)
                goto errjmp;
        }

        // body of the request
        switch (this->data->body_) {
            case BODY_PLAIN: {
                if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDS, this->data->body_default_->data()))!=CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->body_default_->size()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            case BODY_CALLBACK: {
                if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_READFUNCTION, curl_read_handler)) != CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_READDATA, this->data.get()))!=CURLE_OK)
                    goto errjmp;

                if (this->data->flags & FLAG_CONTENT_LENGTH) {
                    if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->data->content_length_)) != CURLE_OK)
                        goto errjmp;
                }

                break;
            }
            case BODY_MULTIPART: {
                form.reset(curl_mime_init(this->data->curl.get()));
                curl_mimepart *field = nullptr;
                for (auto &param : *this->data->body_formdata_) {
                    field = curl_mime_addpart(form.get());
                    if ((status=curl_mime_name(field, param.first.data()))!=CURLE_OK)
                        goto errjmp;
                    switch (param.second.type) {
                        case fetch_formdata::PARAM_DEFAULT: {
                            auto &str = param.second.strdata;
                            if ((status = curl_mime_data(field, str.data(), str.size())) != CURLE_OK)
                                goto errjmp;
                            break;
                        }
                        case fetch_formdata::PARAM_FILE: {
                            auto &str = param.second.strdata;
                            if ((status=curl_mime_filedata(field, str.data()))!=CURLE_OK)
                                goto errjmp;
                            break;
                        }
                        case fetch_formdata::PARAM_CALLBACK: {
                            auto &cbdata = param.second.filedata;
                            if ((status=curl_mime_data_cb(field, cbdata.filesize, curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback))!=CURLE_OK)
                                goto errjmp;
                            break;
                        }
                    }
                }
                if ((status=curl_easy_setopt(this->data->curl.get(), CURLOPT_MIMEPOST, form.get()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            default:
                break;
        }

        this->data->headers = std::make_unique<decltype(this->data->headers)::element_type>();

        try {
            if (this->data->cancellation.contains_cancel_callback()) {
                this->data->cancellation.cancel_callback([data = this->data->curl] () mutable -> void {
                    if (data) {
                        auto err = manapi::async::current()->eventloop()->unwatch_curl(&data);
                        data.reset();
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s",
                            "fetch", "cancellation", err.msg().size(), err.msg().data());
                    }
                });
            }

            resp = co_await async_curl_perform();
        }
        catch (std::exception const &e) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "fetch", e.what());
            resp = CURLE_AGAIN;
        }

        this->data->flags |= FLAG_DATA_CLOSED;

        /* wait all jobs */
#if !MANAPIHTTP_DISABLE_TRACE
        if (this->data->async_run.try_to_lock()) {
            manapi_log_trace(debug::LOG_TRACE_LOW, "fetch(%p):wait unfinished jobs", this->data.get());
            this->data->async_run.unlock();
        }
#endif
        auto lk = co_await this->data->async_run.lock_guard();

        manapi_log_trace(debug::LOG_TRACE_LOW, "fetch(%p):jobs have been finished", this->data.get());

        this->data->cancellation.disable();
        this->data->cancellation = nullptr;

        if (resp != CURLE_OK) {
            lk.call();
            this->clear_();
            co_return status_internal("Connection failed");
        }

        if (this->data->headers && !this->data->headers->empty()) {
            if (!this->data->status_code_) {
                auto res = curl_easy_getinfo(this->data->curl.get(), CURLINFO_HTTP_CODE, &this->data->status_code_);
                if (res) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s", "fetch",
                        "curl_easy_getinfo with CURLINFO_HTTP_CODE", curl_easy_strerror(res));
                }

                if (!this->data->status_code_)
                    this->data->status_code_ = 500;
            }

            if (this->data->async_handler_headers) {
                co_await this->data->async_handler_headers->operator()(std::move(*this->data->headers));
            }
            else if (this->data->handler_headers) {
                this->data->handler_headers->operator()(std::move(*this->data->headers));
            }
            this->data->headers = nullptr;
        }

        if ((status=curl_easy_getinfo(this->data->curl.get(), CURLINFO_HTTP_CODE, &this->data->status_code_))!=CURLE_OK)
            goto errjmp;
    }
    catch (std::bad_alloc const &e) {
        this->clear_();
        co_return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fetch:Failed", e.what());
        this->clear_();
        co_return status_internal("fetch:Failed");
    }

    this->clear_();

    co_return status_ok();
errjmp:
    this->clear_();
    co_return status_invalid_argument(curl_easy_strerror(status));
}

std::map <std::string, std::string, std::less<>> manapi::net::fetch::headers() {
    if (!this->data)
        return {};
    return std::move(*std::exchange(this->data->headers, nullptr));
}

void manapi::net::fetch::clear() {
    this->clear_();

    this->data->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_easy_cleanup);
    this->data->flags = 0;
}

void manapi::net::fetch::clear_() {
    this->data->data = nullptr;
    this->data->url_.clear();
    this->data->async_buffer.clear();
    this->data->async_buffer_size = 0;
    this->data->async_buffer_index = 0;
    this->data->async_handler_headers = nullptr;
    this->data->async_handler_recv_body = nullptr;
    this->data->async_handler_send_body = nullptr;
    this->data->async_user_body_cb = nullptr;
    this->data->body_ = BODY_NONE;
    this->data->body_default_ = nullptr;
    this->data->body_formdata_ = nullptr;
    this->data->cancellation = nullptr;
    this->data->content_length_ = -1;
    this->data->curl_headers = nullptr;
    this->data->flags = 0;
    this->data->handler_headers = nullptr;
    this->data->handler_recv_body = nullptr;
    this->data->handler_send_body = nullptr;
    this->data->headers = nullptr;
    this->data->method_ = nullptr;
    this->data->parallel_task = nullptr;
    this->data->status_code_ = 0;
    this->data->sync_user_body_cb = nullptr;

    this->default_setup_curl_();
}

manapi::future<int> manapi::net::fetch::async_curl_perform() {
    int res;

    try {
        using promise = async::promise_sync<int>;
        res = co_await promise ([this] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            auto err = manapi::async::current()->eventloop()->watch_curl(&this->data->curl, std::move(resolve));
            if (!err) {
                reject(std::make_exception_ptr(manapi::exception(err.data())));
            }
        });

        if (this->data->async_handler_recv_body && this->data->async_buffer_size) {
            auto err = co_await this->data->async_handler_recv_body (this->data, true);
            if (!err) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s",
                    "fetch", "async_curl_perform", err.msg().size(), err.msg().data());
                res = CURLE_ABORTED_BY_CALLBACK;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
            "fetch", "async_curl_perform", e.what());
        res = CURLE_AGAIN;
    }

    co_return res;
}

manapi::status manapi::net::fetch::handle_body(std::move_only_function<ssize_t(char *, ssize_t)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");

    std::unique_ptr<decltype(handler)> cb;

    try {
        cb = std::make_unique<decltype(handler)>(std::move(handler));
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }

    auto status = this->setup_parallel_task();
    if (!status)
        return std::move(status);

    if (this->data->async_handler_recv_body) {
        this->data->async_handler_recv_body = nullptr;
        this->data->async_user_body_cb.reset();
    }

    this->data->sync_user_body_cb = std::move(cb);

    this->data->async_handler_recv_body = curl_recv_sync_or_async;

    this->data->handler_recv_body = curl_recv_async_headers_and_continiue;

    return manapi::status_ok();
}

manapi::future<manapi::status_or<std::string>> manapi::net::fetch::text() {
    try {
        std::string content;

        auto res = handle_body ([&content](char *buffer, size_t size) MANAPIHTTP_NOEXCEPT -> ssize_t {
            try {
                content.append(buffer, size);
                return size;
            }
            catch (std::exception const &) {
                return -1;
            }
        });

        if (!res)
            co_return std::move(res);

        res = co_await async_doit();
        if (!res)
            co_return std::move(res);

        co_return content;
    }
    catch (std::bad_alloc const &) {
        co_return manapi::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fetch:Text failed", e.what());
        co_return manapi::status_internal("fetch:Text failed");
    }
}

manapi::future<manapi::status_or<manapi::json>> manapi::net::fetch::json() {
    auto res = co_await text();
    if (!res.ok())
        co_return std::move(res.err());
    co_return manapi::json::parse(res.unwrap());
}


manapi::status manapi::net::fetch::handle_async_body(std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool fin)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");

    if (this->data->async_buffer.empty())
        this->data->async_buffer = manapi::async::current()->memory_fabric().slice(65536).unwrap();

    this->data->sync_user_body_cb = nullptr;
    this->data->async_buffer_size = 0;

    auto status = this->setup_parallel_task();
    if (!status)
        return std::move(status);

    this->data->async_user_body_cb = std::make_unique<decltype(handler)>(std::move(handler));

    this->data->async_handler_recv_body = curl_recv_sync_or_async;

    this->data->handler_recv_body = curl_recv_data_and_wait;

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::handle_headers(std::move_only_function<bool(std::map <std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");
    try {
        this->data->handler_headers = std::make_unique<decltype(handler)>(std::move(handler));
        if (this->data->async_handler_headers) { this->data->async_handler_headers = {nullptr}; }
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}

manapi::status manapi::net::fetch::handle_async_headers(std::move_only_function<manapi::future<bool>(std::map<std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    try {
        this->data->async_handler_headers
        = std::make_unique<decltype(handler)>(std::move(handler));
        if (this->data->handler_headers) { this->data->handler_headers = {nullptr}; }
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}

manapi::status manapi::net::fetch::enable_alpn(bool status) {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http3() {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http2() {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http1_1() {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::body(fetch_formdata params) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");

    try {
        if (!this->data->body_formdata_)
            this->data->body_formdata_ = std::make_unique<fetch_formdata>(std::move(params));
        else
            *this->data->body_formdata_ = std::move(params);
        this->data->body_ = BODY_MULTIPART;
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}


manapi::status manapi::net::fetch::method(std::string_view method) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    int flag = 0;

    this->data->flags = this->data->flags & status_flags_methods;

    if (manapi::string::equals("get", method, 0b01)) {
        return manapi::status_ok();
    }

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
        try {
            if (this->data->method_)
                *this->data->method_ = method;
            else
                this->data->method_ = std::make_unique<std::string>((method));
        }
        catch (std::exception const &) {
            return manapi::status_resource_exhausted();
        }
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::body(std::string data) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    try {
        this->data->body_ = BODY_PLAIN;
        if (!this->data->body_default_)
            this->data->body_default_ = std::make_unique<std::string>(std::move(data));
        else
            *this->data->body_default_ = std::move(data);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::future<manapi::status> manapi::net::fetch::body(http::file_transfer_info file_info) {
    if (!this->data)
        co_return status_internal("null");

    auto fileres = manapi::filesystem::fstream::create (file_info.filelocal());
    if (!fileres)
        co_return fileres.err();
    auto file = fileres.unwrap();
    auto res = co_await file.open(ev::FS_O_RDONLY);
    if (!res.ok())
        co_return std::move(res);

    auto code = curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDS, nullptr);
    if (code != CURLE_OK)
        goto err;

    code = curl_easy_setopt(this->data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, co_await file.size());
    if (code != CURLE_OK)
        goto err;

    this->async_body([file = std::move(file), file_info = std::move(file_info)] (slice_view buffs, bool &fin) mutable
        -> manapi::future<ssize_t> {
        auto rhs = co_await file.fread(buffs);

        if (file.eof())
            fin = true;

        if (rhs < 0) {
            /* error */
            co_return -1;
        }

        co_return rhs;
    });
    co_return status_ok();
    err: co_return status_invalid_argument(curl_easy_strerror(code));
}

manapi::status manapi::net::fetch::async_body(std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    try {

        if (this->data->flags & FLAG_CALLBACK_SYNC) {
            this->data->flags ^= FLAG_CALLBACK_SYNC;
            this->data->handler_send_body = nullptr;
        }

        auto cb = std::make_unique<decltype(handler)>(std::move(handler));

        this->data->body_ = BODY_CALLBACK;

        if (this->data->async_buffer.empty())
            this->data->async_buffer = manapi::async::current()->memory_fabric().slice(65536).unwrap();

        this->data->async_buffer_size = 0;

        this->data->async_handler_send_body = std::move(cb);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::body(std::move_only_function<ssize_t(char *, ssize_t)> handler) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    try {
        auto cb = std::make_unique<decltype(handler)>(std::move(handler));

        if (this->data->async_handler_send_body)
            this->data->async_handler_send_body = nullptr;

        this->data->flags |= FLAG_CALLBACK_SYNC;

        this->data->body_ = BODY_CALLBACK;
        this->data->async_handler_send_body = nullptr;
        this->data->handler_send_body = std::move(cb);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::headers(std::map<std::string, std::string, std::less<>> headers) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    {
        auto content_length = headers.find(http::H_CONTENT_LENGTH);
        if (content_length != headers.end()) {
            this->data->flags |= FLAG_CONTENT_LENGTH;
            char *end;
            this->data->content_length_ = std::strtoll(content_length->second.data(), &end, 10);
            if (errno == ERANGE)
                return manapi::status_invalid_argument("content-length:erange");
            headers.erase(content_length);
        }
    }

    if (headers.contains(http::H_TRANSFER_ENCODING)) {
        this->data->flags |= FLAG_TRANSFER_ENCODING;
    }

    // headers
    for (auto &header: headers)
    {
        try {
            auto val = std::make_pair<std::string_view, std::string_view>(header.first, header.second);
            auto const ss = http::stringify_header_size(val);
#ifdef _MSC_VER 
            char *data = static_cast<char*>(alloca(ss + 1));
#else
            char data[ss + 1];
#endif
            auto hv = manapi::net::http::stringify_header(data, val);
            assert(ss == hv);
            data[ss]='\0';
            this->data->curl_headers.reset(curl_slist_append(this->data->curl_headers.release(), data));
            if (!this->data->curl_headers)
                return manapi::status_resource_exhausted();
        }
        catch (std::exception const &) {
            return manapi::status_resource_exhausted();
        }
    }
    return manapi::status_ok();
}

manapi::status manapi::net::fetch::header_(std::string_view key, std::string_view value) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return manapi::status_internal("null");

    try {
        auto val = std::make_pair(key, value);
        auto const ss = http::stringify_header_size(val);
#ifdef _MSC_VER
        char *data = static_cast<char*>(alloca(ss+1));
#else
        char data[ss + 1];
#endif
        auto hv = manapi::net::http::stringify_header(data, val);
        assert(ss==hv);
        data[hv] = '\0';
        this->data->curl_headers.reset(curl_slist_append(this->data->curl_headers.release(), data));
        if (!this->data->curl_headers)
            return manapi::status_resource_exhausted();
        return manapi::status_ok();
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
}

manapi::status manapi::net::fetch::json_headers(manapi::json headers) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");

    try {
        auto &m = headers.entries();
        auto content_length = m.find(http::H_CONTENT_LENGTH);
        if (content_length != m.end()) {
            this->data->flags |= FLAG_CONTENT_LENGTH;
            this->data->content_length_ = content_length->second.as_integer_cast();
            headers.erase(content_length);
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "json_headers", e.what());
        return manapi::status_invalid_argument("fetch:as_integer_cast failed");
    }

    auto it = headers.find(http::H_TRANSFER_ENCODING);
    if (it != headers.end<json::OBJECT>()) {
        this->data->flags |= FLAG_TRANSFER_ENCODING;
    }

    // headers
    for (auto &header: headers.entries())
    {
        try {
            std::string val_str;
            std::string_view val_view;
            if (header.second.is_string())
                val_view = header.second.as_string();
            else {
                val_str = header.second.as_string_cast();
                val_view = val_str;
            }
            auto val = std::make_pair<std::string_view, std::string_view>(header.first, {});
            val.second = val_view;
            auto const ss = http::stringify_header_size(val);
#ifdef _MSC_VER
            char *data = static_cast<char*>(alloca(ss+1));
#else
            char data[ss + 1];
#endif
            auto hv = manapi::net::http::stringify_header(data, val);
            assert(ss==hv);
            data[ss] = '\0';
            this->data->curl_headers.reset(curl_slist_append(this->data->curl_headers.release(), data));
        }
        catch (std::exception const &e) {
            return manapi::status_resource_exhausted();
        }
    }

    return manapi::status_ok();
}

void *manapi::net::fetch::custom() MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return nullptr;

    return this->data->curl.get();
}

manapi::status manapi::net::fetch::enable_verify_peer(bool status) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    if (res != CURLE_OK)
        goto err;
    // res = curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
    // if (res != CURLE_OK)
    //     goto err;
    return status_ok();
    err:
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_verify_host(bool status) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->data->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    if (res != CURLE_OK)
        goto err;
    return status_ok();
err:
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::verbose(bool status) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
    if (res == CURLE_OK)
        return status_ok();
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::timeout(std::size_t seconds) MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_setopt(this->data->curl.get(), CURLOPT_CONNECTTIMEOUT_MS, seconds * 1000);
    if (res == CURLE_OK)
        return status_ok();
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::break_write_loop()  MANAPIHTTP_NOEXCEPT{
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_pause(this->data->curl.get(), CURLPAUSE_RECV);
    if (res == CURLE_OK)
        return status_ok();

    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::continue_write_loop() MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return status_internal("null");
    auto const res = curl_easy_pause(this->data->curl.get(), CURLPAUSE_CONT);
    if (res == CURLE_OK)
        return status_ok();

    return status_invalid_argument(curl_easy_strerror(res));
}

uint16_t manapi::net::fetch::status_code() const MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return 0;
    return this->data->status_code_;
}

#endif