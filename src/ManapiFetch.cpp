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

enum m_curl_status_flags {
    M_CURL_FLAG_POST = 1<<0,
    M_CURL_FLAG_HEAD = 1<<1,
    M_CURL_FLAG_PUT = 1<<2,
    M_CURL_FLAG_DELETE = 1<<3,
    M_CURL_FLAG_TRACE = 1<<4,
    M_CURL_FLAG_TRANSFER_ENCODING = 1<<5,
    M_CURL_FLAG_CONTENT_LENGTH = 1<<6,
    M_CURL_FLAG_WAS_USED = 1<<7,
    M_CURL_FLAG_STATUS_PASSED = 1<<8,
    M_CURL_FLAG_DATA_EOF = 1<<9,
    M_CURL_FLAG_DATA_CLOSED = 1<<10,
    M_CURL_FLAG_CALLBACK_SYNC = 1<<11,
    M_CURL_FLAG_ASYNC_RECV_NODELAY = 1<<12
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
    uint32_t flags;
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

    std::shared_ptr<CURL> curl;
    std::unique_ptr<std::map<std::string, std::string, std::less<>>> headers;
    std::unique_ptr<struct curl_slist, curl_slist_deleter> curl_headers;

    manapi::net::fetch *parent;

    std::size_t(*handler_recv_body)(data_t *data, char *, std::size_t);
    manapi::status (*parallel_task)(data_t *data);
    manapi::future<manapi::status> (*async_handler_recv_body) (std::shared_ptr<fetch> parent, data_t *data, bool finish);

    std::unique_ptr<std::move_only_function<ssize_t(char *buffer, std::size_t size)>> sync_user_body_cb;
    std::unique_ptr<std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool finish)>> async_user_body_cb;
    std::unique_ptr<std::move_only_function <manapi::future<bool>(std::map <std::string, std::string, std::less<>>)>> async_handler_headers;
    std::unique_ptr<std::move_only_function <bool(std::map <std::string, std::string, std::less<>>)>> handler_headers;
    std::unique_ptr<std::move_only_function <ssize_t(char *, std::size_t)>> handler_send_body;
    std::unique_ptr<std::move_only_function <manapi::future<ssize_t>(slice_view buffs, bool &fin)>> async_handler_send_body;
};


manapi::future<manapi::status> manapi::net::curl_send_async_body (std::shared_ptr<manapi::net::fetch> parent, bool finish) {
    auto &m_data = parent->m_data;
    while (!m_data->async_buffer_size) {
        ssize_t rhs = -1;
        bool fin = false;
        try {
            rhs = co_await m_data->async_handler_send_body->operator() (m_data->async_buffer, fin);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG( "set_async_body(...) failed: {}", e.what());
        }

        if (rhs < 0) {
            /* error */
            if (finish) {
                co_return manapi::status_aborted("fetch:User async send callback failed");
            }


            m_data->async_buffer_size = 0;
            auto err = manapi::async::current()->eventloop()->unwatch_curl(&m_data->curl);
            m_data->async_run.unlock();

            co_return std::move(err);
        }

        if (fin || !rhs) {
            /* eof */
            m_data->flags |= M_CURL_FLAG_DATA_EOF;

            if (!rhs)
                break;
        }

        m_data->async_buffer_size += static_cast<std::size_t>(rhs);
    }

    if (finish)
        co_return manapi::status_ok();


    m_data->async_run.unlock();
    co_return manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);
}

static void curl_send_query_to_send_data (std::shared_ptr<manapi::net::fetch> parent, bool finish) MANAPIHTTP_NOEXCEPT {
    manapi::future<manapi::status> task{nullptr};
    MANAPIHTTP_MUST_ALLOC_START
    task = curl_send_async_body(parent, finish);
    MANAPIHTTP_MUST_ALLOC_END
    manapi::async::run(std::move(task));
}

static void default_setup_curl_(manapi::net::fetch *p) {
    p->timeout(5);
}

static std::size_t curl_send_async_continue (manapi::net::fetch::data_t *m_data, char *buffer, std::size_t size) MANAPIHTTP_NOEXCEPT {
    try {
        size = std::min<std::size_t>(m_data->async_buffer_size - m_data->async_buffer_index, size);
        if (size > 0) {
            auto err = m_data->async_buffer.copy_to(buffer, m_data->async_buffer_index, size);
            if (!err) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s", "fetch", "curl_send_async_continue()",
                    err.msg().size(), err.msg().data());
                return CURL_READFUNC_ABORT;
            }

            m_data->async_buffer_index += size;

            if (m_data->async_buffer_size == m_data->async_buffer_index) {
                m_data->async_buffer_size = 0;
                m_data->async_buffer_index = 0;
            }

            return size;
        }

        if (m_data->flags & M_CURL_FLAG_DATA_EOF) {
            if (!(m_data->flags & M_CURL_FLAG_CONTENT_LENGTH)) {

            }

            /* end of stream */
            return 0;
        }

        /* in the loop event */
        if (!m_data->async_run.try_to_lock()) {
            /** something get wrong **/
            return CURL_READFUNC_ABORT;
        }

        manapi::async::current()->etaskpool()->append_task([parent = m_data->parent->shared_from_this()] () mutable
            -> void { curl_send_query_to_send_data(std::move(parent), false); });

        return CURL_READFUNC_PAUSE;
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "fetch", "curl_send_async_continue()", e.what());
        return CURL_READFUNC_ABORT;
    }
}

size_t manapi::net::curl_header_handler (char *buffer, size_t size, size_t n_items, void *userdata)
{
    auto const f = static_cast <fetch::data_t *> (userdata);
    try {
        /**
         * clear body send callback
         */
        f->async_buffer_size = 0;

        if (f->flags & M_CURL_FLAG_STATUS_PASSED) {
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
            f->flags |= M_CURL_FLAG_STATUS_PASSED;
        }

        return n_items * size;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_header_handler", e.what());
    }

    return 0;
}

size_t manapi::net::curl_write_handler (char *buffer, size_t size, size_t nitems, void *user_p) {
    auto f = static_cast <fetch::data_t *> (user_p);

    try {
        // call user handler
        auto const len = (size * nitems);
        if (!len)
            return 0;

        return (f->handler_recv_body (f, buffer, len));
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_write_handler", e.what());
        return 0;
    }
}

std::size_t manapi::net::curl_read_handler(char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto f = static_cast<fetch::data_t *> (user_p);
    try {
        if (f->flags & M_CURL_FLAG_CALLBACK_SYNC) {
            auto res = f->handler_send_body->operator()(buffer, (size * nitems));

            if (res < 0)
                return CURL_READFUNC_ABORT;

            return static_cast<std::size_t>(res);
        }

        return static_cast<std::size_t>(curl_send_async_continue (f, buffer, static_cast<std::size_t> (size * nitems)));
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_read_handler", e.what());
        return CURL_READFUNC_ABORT;
    }
}

static manapi::status process_accepted_data_cb (manapi::net::fetch::data_t *m_data) MANAPIHTTP_NOEXCEPT {
    try {
        manapi::async::current()->etaskpool()->append_static_task([parent = m_data->parent->shared_from_this(), m_data] () mutable -> void {
            MANAPIHTTP_MUST_ALLOC_START
            manapi::async::run(m_data->async_handler_recv_body(parent, m_data, false));
            MANAPIHTTP_MUST_ALLOC_END
        });

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        return manapi::status_resource_exhausted();
    }
}

static manapi::status response_headers_received_(manapi::net::fetch::data_t *m_data) MANAPIHTTP_NOEXCEPT {
    auto res = curl_easy_getinfo(m_data->curl.get(), CURLINFO_HTTP_CODE, &m_data->status_code_);
    if (res != CURLE_OK)
        return manapi::status_internal("fetch:CURLINFO_HTTP_CODE failed");

    m_data->parallel_task = process_accepted_data_cb;

    m_data->parallel_task (m_data);

    return manapi::status_ok();
}

static std::size_t curl_recv_data_and_wait (manapi::net::fetch::data_t *m_data, char *buffer, std::size_t size) {
    bool decision;
    if (m_data->flags & M_CURL_FLAG_ASYNC_RECV_NODELAY)
        decision = m_data->async_buffer_size == m_data->async_buffer_index;
    else
        decision = m_data->async_buffer_size < m_data->async_buffer.size()
            && m_data->async_buffer.size() - m_data->async_buffer_size >= size;

    if (decision) {
        const auto copy = size;
        auto res = m_data->async_buffer.copy_from(buffer, m_data->async_buffer_size, copy);
        if (!res.ok())
            goto err;

        m_data->async_buffer_size += copy;

        return copy;
    }
    else {
        /* in the loop event */
        if (!m_data->async_run.try_to_lock()) {
            /** something gets wrong **/
            goto err;
        }

        auto res = m_data->parallel_task (m_data);
        if (!res) {
            m_data->async_run.unlock();
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

static std::size_t curl_recv_async_headers_and_continiue (manapi::net::fetch::data_t *m_data, char *buffer, std::size_t buffer_size)  {
    if (m_data->async_run.try_to_lock()) {
        auto res = m_data->parallel_task (m_data);
        if (!res) {
            m_data->async_run.unlock();
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

static std::size_t curl_recv_sync_continiue (manapi::net::fetch::data_t *m_data, char *buffer, std::size_t buffer_size) {
    try {
        auto const rhs = m_data->sync_user_body_cb->operator() (buffer, buffer_size);
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

static manapi::future<manapi::status> curl_recv_async_callback (manapi::net::fetch::data_t *m_data, bool finish) {
    ssize_t rhs = -1;

    if (m_data->async_buffer_size || finish) {
        try {
            rhs = co_await m_data->async_user_body_cb->operator() (m_data->async_buffer.subslice(0, m_data->async_buffer_size).unwrap(), finish);
        }
        catch (std::exception const &e) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
                "fetch", "async recv user callback", e.what());
        }

        if (m_data->async_buffer_size) {
            if (m_data->flags & M_CURL_FLAG_DATA_CLOSED) {
                m_data->async_run.unlock();
                co_return manapi::status_ok();
            }

            if (rhs != m_data->async_buffer_size) {
                if (finish)
                    co_return manapi::status_internal("curl_recv_async_callback:User callback failed");

                m_data->async_buffer_size = 0;
                auto err = manapi::async::current()->eventloop()->unwatch_curl(&m_data->curl);
                m_data->async_run.unlock();

                if (err.ok())
                    co_return manapi::status_ok();

                co_return manapi::status_internal("curl_recv_async_callback:User callback failed");
            }

            m_data->async_buffer_size = 0;
        }
    }

    if (finish)
        co_return manapi::status_ok();

    m_data->async_run.unlock();
    manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);

    co_return manapi::status_ok();
}

static manapi::future<manapi::status> curl_recv_async_continiue (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t * m_data, bool finish) {
    try {
        auto err = co_await curl_recv_async_callback((m_data), finish);
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

static manapi::future<bool> handle_body_verify (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *data) {
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

static manapi::future<manapi::status> handle_sync_body_finish(std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *m_data, bool finish) {
    manapi::status status;
    std::size_t constexpr error_value =
    #if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
        CURL_WRITEFUNC_ERROR;
    #else
        0;
    #endif

    try {
        m_data->handler_recv_body = curl_recv_sync_continiue;

        if (finish) {
            co_return manapi::status_ok();
        }

        if (m_data->async_buffer_size > 0) {
            auto res = m_data->async_buffer.subslice(0, m_data->async_buffer_size);
            m_data->async_buffer_size = 0;
            if (!res.ok())
                goto err;
            auto buffs = res.unwrap();
            for (auto it = buffs.begin(); it != buffs.end(); ++it) {
                /* cached */
                std::size_t current = 0;
                std::size_t rhs = 0;
                auto const size = (it.size());
                auto const buff = static_cast<char*>(it.buffer());

                while (current < size) {
                    if ((rhs = m_data->handler_recv_body (m_data, buff + current, size - current)) == error_value) {
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

    m_data->async_handler_recv_body = nullptr;
    m_data->async_run.unlock();
    status = manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);

    co_return std::move(status);

    err:
    m_data->async_buffer_size = 0;
    status = manapi::async::current()->eventloop()->unwatch_curl(&m_data->curl);
    m_data->async_run.unlock();
    co_return std::move(status);
}

static manapi::future<manapi::status> handle_async_body_finish(std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t * m_data, bool finish) {
    m_data->async_handler_recv_body = curl_recv_async_continiue;
    return m_data->async_handler_recv_body (parent, m_data, finish);
}

static manapi::future<manapi::status> curl_recv_sync_or_async (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *data, bool finish) {
    try {
        if (!co_await handle_body_verify(parent, data)) {
            co_return manapi::status_ok();
        }

        if (!data->sync_user_body_cb) {
            /* handler is async for now */
            co_return co_await handle_async_body_finish(parent, data, finish);
        }

        co_return co_await handle_sync_body_finish(parent, data, finish);
    }
    catch (std::bad_alloc const &) {
        co_return manapi::status_resource_exhausted();
    }
}

static void fetch_setup_parallel_task(manapi::net::fetch::data_t *m_data) MANAPIHTTP_NOEXCEPT {
    m_data->parallel_task = response_headers_received_;
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

static void fetch_cleanup(manapi::net::fetch *p, manapi::net::fetch::data_t *data) {

    data->url_.clear();
    data->async_buffer.clear();
    data->async_buffer_size = 0;
    data->async_buffer_index = 0;
    data->async_handler_headers = nullptr;
    data->async_handler_recv_body = nullptr;
    data->async_handler_send_body = nullptr;
    data->async_user_body_cb = nullptr;
    data->body_ = BODY_NONE;
    data->body_default_ = nullptr;
    data->body_formdata_ = nullptr;
    data->cancellation = nullptr;
    data->content_length_ = -1;
    data->curl_headers = nullptr;
    data->flags = 0;
    data->handler_headers = nullptr;
    data->handler_recv_body = nullptr;
    data->handler_send_body = nullptr;
    data->headers = nullptr;
    data->method_ = nullptr;
    data->parallel_task = nullptr;
    data->status_code_ = 0;
    data->sync_user_body_cb = nullptr;

    ::default_setup_curl_(p);
}

manapi::future<int> fetch_async_curl_perform(std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *data) {
    int res;

    try {
        using promise = manapi::async::promise_sync<int>;
        res = co_await promise ([data] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            auto err = manapi::async::current()->eventloop()->watch_curl(&data->curl, std::move(resolve));
            if (!err) {
                reject(std::make_exception_ptr(manapi::exception(err.data())));
            }
        });

        if (data->async_handler_recv_body && data->async_buffer_size) {
            auto err = co_await data->async_handler_recv_body (parent, data, true);
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

manapi::status manapi::net::fetch_formdata::set_callback(std::string name, std::size_t size, std::move_only_function<size_t(void *buff, size_t buff_size)> cb) MANAPIHTTP_NOEXCEPT {
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


manapi::net::fetch::fetch(std::string url, manapi::ctoken cancellation) {
    this->m_data = std::make_unique <fetch::data_t>(fetch::data_t{});
    this->m_data->parent = this;
    this->init(std::move(url), std::move(cancellation)).unwrap();
}

manapi::net::fetch::~fetch() = default;

manapi::status manapi::net::fetch::init(std::string url, manapi::ctoken cancellation) {
    try {
        this->m_data->url_ = std::move(url);
        this->m_data->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_easy_cleanup);
        this->m_data->cancellation = std::move(cancellation);
        this->m_data->content_length_ = -1;

        default_setup_curl_(this);

        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status_or<std::shared_ptr<manapi::net::fetch>> manapi::net::fetch::create(std::string url,manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    return std::shared_ptr<fetch>(new fetch(std::move(url), std::move(cancellation)));
}

manapi::future<manapi::status> manapi::net::fetch::async_doit() {
    CURLcode status;
    auto rstatus = manapi::status_ok();

    auto grab = this->shared_from_this();

    try {
        std::unique_ptr<curl_mime, curl_mime_deleter> form {nullptr};

        if (this->m_data->flags & M_CURL_FLAG_WAS_USED)
            co_return status_already_exists("fetch was already used. create another fetch object or reinit current");

        this->m_data->flags |= M_CURL_FLAG_WAS_USED;

        if (!this->m_data->curl)
            co_return status_internal("curl can not be init");

        int resp;

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_TCP_NODELAY, 1L)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_NOSIGNAL, 1L)) != CURLE_OK)
            goto errjmp;
        // curl_easy_setopt(this->m_data->curl.get(), CURLOPT_MAXLIFETIME_CONN, 1L);
        // curl_easy_setopt(this->m_data->curl.get(), CURLOPT_MAXAGE_CONN, 0);
        // curl_easy_setopt(this->m_data->curl.get(), CURLOPT_TCP_KEEPALIVE, 0L);
        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_URL, this->m_data->url_.data())) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler)) != CURLE_OK)
            goto errjmp;
        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HEADERDATA, this->m_data.get())) != CURLE_OK)
            goto errjmp;

        {
            bool optex = true;
            CURLoption opt;
            char const *method{nullptr};

            if (this->m_data->method_)
                method = this->m_data->method_->data();
            else {
                if (this->m_data->flags & M_CURL_FLAG_POST)
                    opt = CURLOPT_POST;
                else if (this->m_data->flags & M_CURL_FLAG_PUT)
                    opt = CURLOPT_UPLOAD;
                else if (this->m_data->flags & M_CURL_FLAG_HEAD)
                    opt = CURLOPT_NOBODY;
                else if (this->m_data->flags & M_CURL_FLAG_DELETE)
                    method = "DELETE";
                else if (this->m_data->flags & M_CURL_FLAG_TRACE)
                    method = "TRACE";
                else
                    optex = false;
            }

            if (method) {
                if((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_CUSTOMREQUEST, method))!=CURLE_OK)
                    goto errjmp;
            }
            else if (optex) {
                if ((status = curl_easy_setopt(this->m_data->curl.get(), opt, 1))!=CURLE_OK)
                    goto errjmp;
            }
        }

        // if handler has been set
        if (this->m_data->handler_recv_body)
        {
            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler))!=CURLE_OK)
                goto errjmp;
            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_WRITEDATA, this->m_data.get()))!=CURLE_OK)
                goto errjmp;
        }


        if (this->m_data->curl_headers)
        {
            if((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HTTPHEADER, this->m_data->curl_headers.get()))!=CURLE_OK)
                goto errjmp;
        }

        // body of the request
        switch (this->m_data->body_) {
            case BODY_PLAIN: {
                if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDS, this->m_data->body_default_->data()))!=CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->m_data->body_default_->size()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            case BODY_CALLBACK: {
                if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_READFUNCTION, curl_read_handler)) != CURLE_OK)
                    goto errjmp;
                if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_READDATA, this->m_data.get()))!=CURLE_OK)
                    goto errjmp;

                if (this->m_data->flags & M_CURL_FLAG_CONTENT_LENGTH) {
                    if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, this->m_data->content_length_)) != CURLE_OK)
                        goto errjmp;
                }

                break;
            }
            case BODY_MULTIPART: {
                form.reset(curl_mime_init(this->m_data->curl.get()));
                curl_mimepart *field = nullptr;
                for (auto &param : *this->m_data->body_formdata_) {
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
                            if ((status=curl_mime_data_cb(field, static_cast<ssize_t>(cbdata.filesize), curl_send_formdata_cb_read, curl_send_formdata_cb_seek, curl_send_formdata_cb_free, &cbdata.callback))!=CURLE_OK)
                                goto errjmp;
                            break;
                        }
                    }
                }
                if ((status=curl_easy_setopt(this->m_data->curl.get(), CURLOPT_MIMEPOST, form.get()))!=CURLE_OK)
                    goto errjmp;
                break;
            }
            default:
                break;
        }

        this->m_data->headers = std::make_unique<decltype(this->m_data->headers)::element_type>();

        try {
            if (this->m_data->cancellation.contains_cancel_callback()) {
                this->m_data->cancellation.cancel_callback([m_data = this->m_data->curl] () mutable -> void {
                    if (m_data) {
                        auto err = manapi::async::current()->eventloop()->unwatch_curl(&m_data);
                        m_data.reset();
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s",
                            "fetch", "cancellation", err.msg().size(), err.msg().data());
                    }
                });
            }

            resp = co_await fetch_async_curl_perform(grab, this->m_data.get());
        }
        catch (std::exception const &e) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "fetch", e.what());
            resp = CURLE_AGAIN;
        }

        this->m_data->flags |= M_CURL_FLAG_DATA_CLOSED;

        /* wait all jobs */
#ifndef MANAPIHTTP_DISABLE_TRACE
        if (this->m_data->async_run.try_to_lock()) {
            manapi_log_trace(debug::LOG_TRACE_LOW, "fetch(%p):wait unfinished jobs", this->m_data.get());
            this->m_data->async_run.unlock();
        }
#endif
        auto lk = co_await this->m_data->async_run.lock_guard();

        manapi_log_trace(debug::LOG_TRACE_LOW, "fetch(%p):jobs have been finished", this->m_data.get());

        this->m_data->cancellation.disable();
        this->m_data->cancellation = nullptr;

        if (resp != CURLE_OK) {
            lk.call();
            rstatus = status_internal("Connection failed");
            goto fin;
        }

        if (this->m_data->headers && !this->m_data->headers->empty()) {
            if (!this->m_data->status_code_) {
                auto res = curl_easy_getinfo(this->m_data->curl.get(), CURLINFO_HTTP_CODE, &this->m_data->status_code_);
                if (res) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s:%s failed due to %s", "fetch",
                        "curl_easy_getinfo with CURLINFO_HTTP_CODE", curl_easy_strerror(res));
                }

                if (!this->m_data->status_code_)
                    this->m_data->status_code_ = 500;
            }

            if (this->m_data->async_handler_headers) {
                co_await this->m_data->async_handler_headers->operator()(std::move(*this->m_data->headers));
            }
            else if (this->m_data->handler_headers) {
                this->m_data->handler_headers->operator()(std::move(*this->m_data->headers));
            }
            this->m_data->headers = nullptr;
        }

        if ((status=curl_easy_getinfo(this->m_data->curl.get(), CURLINFO_HTTP_CODE, &this->m_data->status_code_))!=CURLE_OK)
            goto errjmp;
    }
    catch (std::bad_alloc const &) {
        rstatus = status_resource_exhausted();
        goto fin;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fetch:Failed", e.what());
        rstatus = status_internal("fetch:Failed");
        goto fin;
    }

    goto fin;
errjmp:
    rstatus = status_invalid_argument(curl_easy_strerror(status));
fin:
    fetch_cleanup (this, this->m_data.get());
    co_return std::move(rstatus);
}

std::map <std::string, std::string, std::less<>> manapi::net::fetch::headers() {
    return std::move(*std::exchange(this->m_data->headers, nullptr));
}

void manapi::net::fetch::clear() {
    
    fetch_cleanup (this, this->m_data.get());

    this->m_data->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_easy_cleanup);
    this->m_data->flags = 0;
}

manapi::status manapi::net::fetch::handle_body(std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT {
    

    std::unique_ptr<decltype(handler)> cb;

    try {
        cb = std::make_unique<decltype(handler)>(std::move(handler));
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }

    fetch_setup_parallel_task (this->m_data.get());

    if (this->m_data->async_handler_recv_body) {
        this->m_data->async_handler_recv_body = nullptr;
        this->m_data->async_user_body_cb.reset();
    }

    this->m_data->sync_user_body_cb = std::move(cb);

    this->m_data->async_handler_recv_body = curl_recv_sync_or_async;

    this->m_data->handler_recv_body = curl_recv_async_headers_and_continiue;

    return manapi::status_ok();
}

manapi::future<manapi::status_or<std::string>> manapi::net::fetch::text() {
    try {
        std::string content;

        auto res = handle_body ([&content](char *buffer, size_t size) MANAPIHTTP_NOEXCEPT -> ssize_t {
            try {
                content.append(buffer, size);
                return static_cast<ssize_t>(size);
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
    

    if (this->m_data->async_buffer.empty())
        this->m_data->async_buffer = manapi::async::current()->memory_fabric().slice(65536).unwrap();

    this->m_data->sync_user_body_cb = nullptr;
    this->m_data->async_buffer_size = 0;

    fetch_setup_parallel_task (this->m_data.get());

    this->m_data->async_user_body_cb = std::make_unique<decltype(handler)>(std::move(handler));

    this->m_data->async_handler_recv_body = curl_recv_sync_or_async;

    this->m_data->handler_recv_body = curl_recv_data_and_wait;

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::handle_headers(std::move_only_function<bool(std::map <std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT {
    
    try {
        this->m_data->handler_headers = std::make_unique<decltype(handler)>(std::move(handler));
        if (this->m_data->async_handler_headers) { this->m_data->async_handler_headers = {nullptr}; }
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}

manapi::status manapi::net::fetch::handle_async_headers(std::move_only_function<manapi::future<bool>(std::map<std::string, std::string, std::less<>>)> handler) MANAPIHTTP_NOEXCEPT {
    
    try {
        this->m_data->async_handler_headers
        = std::make_unique<decltype(handler)>(std::move(handler));
        if (this->m_data->handler_headers) { this->m_data->handler_headers = {nullptr}; }
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}

manapi::status manapi::net::fetch::enable_alpn(bool status) {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http3() {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http2() {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_http1_1() {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    return res == CURLE_OK ? status_ok() : status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::body(fetch_formdata params) MANAPIHTTP_NOEXCEPT {
    

    try {
        if (!this->m_data->body_formdata_)
            this->m_data->body_formdata_ = std::make_unique<fetch_formdata>(std::move(params));
        else
            *this->m_data->body_formdata_ = std::move(params);
        this->m_data->body_ = BODY_MULTIPART;
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
    return manapi::status_ok();
}


manapi::status manapi::net::fetch::method(std::string_view method) MANAPIHTTP_NOEXCEPT {
    

    uint32_t flag = 0;

    this->m_data->flags = this->m_data->flags & status_flags_methods;

    if (manapi::string::equals("get", method, 0b01)) {
        return manapi::status_ok();
    }

    if (string::equals("post", method, 0b01))
        flag = M_CURL_FLAG_POST;
    else if (string::equals("delete", method, 0b01))
        flag = M_CURL_FLAG_DELETE;
    else if (string::equals("put", method, 0b01))
        flag = M_CURL_FLAG_PUT;
    else if (string::equals("head", method, 0b01))
        flag = M_CURL_FLAG_HEAD;
    else if (string::equals("trace", method, 0b01))
        flag = M_CURL_FLAG_TRACE;

    if (flag) {
        this->m_data->flags |= flag;
    }
    else {
        try {
            if (this->m_data->method_)
                *this->m_data->method_ = method;
            else
                this->m_data->method_ = std::make_unique<std::string>((method));
        }
        catch (std::exception const &) {
            return manapi::status_resource_exhausted();
        }
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::body(std::string m_data) MANAPIHTTP_NOEXCEPT {
    

    try {
        this->m_data->body_ = BODY_PLAIN;
        if (!this->m_data->body_default_)
            this->m_data->body_default_ = std::make_unique<std::string>(std::move(m_data));
        else
            *this->m_data->body_default_ = std::move(m_data);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::future<manapi::status> manapi::net::fetch::body(http::file_transfer_info file_info) {

    auto fileres = manapi::fs::fstream::create (file_info.filelocal());
    if (!fileres)
        co_return fileres.err();
    auto file = fileres.unwrap();
    auto res = co_await file.open(ev::FS_O_RDONLY);
    if (!res.ok())
        co_return std::move(res);

    auto code = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDS, nullptr);
    if (code != CURLE_OK)
        goto err;

    code = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, co_await file.size());
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
    

    try {

        if (this->m_data->flags & M_CURL_FLAG_CALLBACK_SYNC) {
            this->m_data->flags ^= M_CURL_FLAG_CALLBACK_SYNC;
            this->m_data->handler_send_body = nullptr;
        }

        auto cb = std::make_unique<decltype(handler)>(std::move(handler));

        this->m_data->body_ = BODY_CALLBACK;

        if (this->m_data->async_buffer.empty())
            this->m_data->async_buffer = manapi::async::current()->memory_fabric().slice(65536).unwrap();

        this->m_data->async_buffer_size = 0;

        this->m_data->async_handler_send_body = std::move(cb);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::body(std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT {
    

    try {
        auto cb = std::make_unique<decltype(handler)>(std::move(handler));

        if (this->m_data->async_handler_send_body)
            this->m_data->async_handler_send_body = nullptr;

        this->m_data->flags |= M_CURL_FLAG_CALLBACK_SYNC;

        this->m_data->body_ = BODY_CALLBACK;
        this->m_data->async_handler_send_body = nullptr;
        this->m_data->handler_send_body = std::move(cb);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::headers(std::map<std::string, std::string, std::less<>> headers) MANAPIHTTP_NOEXCEPT {
    

    {
        auto content_length = headers.find(http::H_CONTENT_LENGTH);
        if (content_length != headers.end()) {
            this->m_data->flags |= M_CURL_FLAG_CONTENT_LENGTH;
            char *end;
            this->m_data->content_length_ = std::strtoll(content_length->second.data(), &end, 10);
            if (errno == ERANGE)
                return manapi::status_invalid_argument("content-length:erange");
            headers.erase(content_length);
        }
    }

    if (headers.contains(http::H_TRANSFER_ENCODING)) {
        this->m_data->flags |= M_CURL_FLAG_TRANSFER_ENCODING;
    }

    // headers
    for (auto &header: headers)
    {
        try {
            auto val = std::make_pair<std::string_view, std::string_view>(header.first, header.second);
            auto const ss = http::stringify_header_size(val);
#ifdef _MSC_VER 
            char *m_data = static_cast<char*>(alloca(ss + 1));
#else
            char m_data[ss + 1];
#endif
            auto hv = manapi::net::http::stringify_header(m_data, val);
            assert(ss == hv);
            m_data[ss]='\0';
            this->m_data->curl_headers.reset(curl_slist_append(this->m_data->curl_headers.release(), m_data));
            if (!this->m_data->curl_headers)
                return manapi::status_resource_exhausted();
        }
        catch (std::exception const &) {
            return manapi::status_resource_exhausted();
        }
    }
    return manapi::status_ok();
}

// manapi::status manapi::net::fetch::mheader(std::string_view key, std::string_view value) MANAPIHTTP_NOEXCEPT {
//
//
//     try {
//         auto val = std::make_pair(key, value);
//         auto const ss = http::stringify_header_size(val);
// #ifdef _MSC_VER
//         char *m_data = static_cast<char*>(alloca(ss+1));
// #else
//         char m_data[ss + 1];
// #endif
//         auto hv = manapi::net::http::stringify_header(m_data, val);
//         assert(ss==hv);
//         m_data[hv] = '\0';
//         this->m_data->curl_headers.reset(curl_slist_append(this->m_data->curl_headers.release(), m_data));
//         if (!this->m_data->curl_headers)
//             return manapi::status_resource_exhausted();
//         return manapi::status_ok();
//     }
//     catch (std::exception const &) {
//         return manapi::status_resource_exhausted();
//     }
// }

manapi::status manapi::net::fetch::json_headers(manapi::json headers) MANAPIHTTP_NOEXCEPT {
    

    try {
        auto &m = headers.entries();
        auto content_length = m.find(http::H_CONTENT_LENGTH);
        if (content_length != m.end()) {
            this->m_data->flags |= M_CURL_FLAG_CONTENT_LENGTH;
            this->m_data->content_length_ = content_length->second.as_integer_cast();
            headers.erase(content_length);
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "json_headers", e.what());
        return manapi::status_invalid_argument("fetch:as_integer_cast failed");
    }

    auto it = headers.find(http::H_TRANSFER_ENCODING);
    if (it != headers.end<json::OBJECT>()) {
        this->m_data->flags |= M_CURL_FLAG_TRANSFER_ENCODING;
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
            char *m_data = static_cast<char*>(alloca(ss+1));
#else
            char m_data[ss + 1];
#endif
            auto hv = manapi::net::http::stringify_header(m_data, val);
            assert(ss==hv);
            m_data[ss] = '\0';
            this->m_data->curl_headers.reset(curl_slist_append(this->m_data->curl_headers.release(), m_data));
        }
        catch (std::exception const &e) {
            return manapi::status_resource_exhausted();
        }
    }

    return manapi::status_ok();
}

void *manapi::net::fetch::custom() MANAPIHTTP_NOEXCEPT {
    return this->m_data->curl.get();
}

manapi::status manapi::net::fetch::enable_verify_peer(bool status) MANAPIHTTP_NOEXCEPT {
    
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_SSL_VERIFYPEER, lstatus);
    if (res != CURLE_OK)
        goto err;
    // res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_SSL_VERIFYSTATUS, lstatus);
    // if (res != CURLE_OK)
    //     goto err;
    return status_ok();
    err:
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::enable_verify_host(bool status) MANAPIHTTP_NOEXCEPT {
    
    auto lstatus = static_cast<long> (status);
    CURLcode res;
    res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_SSL_VERIFYHOST, lstatus);
    if (res != CURLE_OK)
        goto err;
    return status_ok();
err:
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::verbose(bool status) MANAPIHTTP_NOEXCEPT {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_VERBOSE, static_cast<long>(status));
    if (res == CURLE_OK)
        return status_ok();
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::timeout(std::size_t seconds) MANAPIHTTP_NOEXCEPT {
    
    auto const res = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_CONNECTTIMEOUT_MS, seconds * 1000);
    if (res == CURLE_OK)
        return status_ok();
    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::break_write_loop()  MANAPIHTTP_NOEXCEPT{
    
    auto const res = curl_easy_pause(this->m_data->curl.get(), CURLPAUSE_RECV);
    if (res == CURLE_OK)
        return status_ok();

    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::continue_write_loop() MANAPIHTTP_NOEXCEPT {
    
    auto const res = curl_easy_pause(this->m_data->curl.get(), CURLPAUSE_CONT);
    if (res == CURLE_OK)
        return status_ok();

    return status_invalid_argument(curl_easy_strerror(res));
}

void manapi::net::fetch::async_recv_nodelay(bool enabled) MANAPIHTTP_NOEXCEPT {
    if (enabled)
        this->m_data->flags |= M_CURL_FLAG_ASYNC_RECV_NODELAY;
    else if (this->m_data->flags & M_CURL_FLAG_ASYNC_RECV_NODELAY)
        this->m_data->flags ^= M_CURL_FLAG_ASYNC_RECV_NODELAY;
}

uint16_t manapi::net::fetch::status_code() const MANAPIHTTP_NOEXCEPT {
    return this->m_data->status_code_;
}

#endif