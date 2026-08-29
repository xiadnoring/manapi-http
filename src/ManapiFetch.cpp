#include "ManapiFetch.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY

#   include <exception>
#   include <cstring>

#   include <curl/curl.h>

#   include "ManapiHttp.hpp"
#   include "ManapiString.hpp"
#   include "std/ManapiPromise.hpp"
#   include "./include/ManapiUtils.hpp"

#   define MANAPIHTTP_CURL_VERSION_REQUIRE(major, minor, patch) MANAPIHTTP_SINCE_AT_CUSTOM(LIBCURL_VERSION_MAJOR,LIBCURL_VERSION_MINOR,LIBCURL_VERSION_PATCH, major, minor, patch)

// Utils

enum manapi__curl_status_flags {
    MANAPI__CURL_FLAG_STATUS_PASSED = 1<<0,
    MANAPI__CURL_FLAG_DATA_EOF = 1<<1,
    MANAPI__CURL_FLAG_IS_PROCESSING = 1<<2,
    MANAPI__CURL_FLAG_HEADERS_RECEIVED = 1<<3,
    MANAPI__CURL_FLAG_SENDING = 1<<4,
    MANAPI__CURL_FLAG_RECVING = 1<<5,
    MANAPI__CURL_FLAG_FAILED = 1<<6,
    MANAPI__CURL_FLAG_ASYNC_RECV = 1<<7,
    MANAPI__CURL_FLAG_ASYNC_HEADERS = 1<<8,
    MANAPI__CURL_FLAG_ASYNC_SEND = 1<<9,
    MANAPI__CURL_FLAG_BODY_PLAIN = 1<<10,
    MANAPI__CURL_FLAG_BODY_CALLBACK = 1<<11,
    MANAPI__CURL_FLAG_BODY_FORMDATA = 1<<12,
    MANAPI__CURL_FLAG_HEADER_RECVING = 1<<13
};

struct manapi__curl_deleter {
    void operator() (CURL *curl)
    { curl_easy_cleanup(curl); }
};
struct manapi__curl_slist_deleter {
    void operator() (curl_slist *list)
    { curl_slist_free_all(list); }
};

struct manapi__curl_mime_deleter {
    void operator() (curl_mime *mime)
    { curl_mime_free(mime); }
};

union manapi__fetch_recv_body_t {
    std::move_only_function<ssize_t(char *buffer, std::size_t size)> sync_cb{};
    std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool finish)> async_cb;

    ~manapi__fetch_recv_body_t() {}

    void reset (uint32_t &flags, bool is_async) MANAPIHTTP_NOEXCEPT {
        if (flags & MANAPI__CURL_FLAG_ASYNC_RECV) {
            if (is_async) return;
            this->async_cb.~move_only_function();
            new (this) decltype (this->sync_cb) ();
            flags ^= MANAPI__CURL_FLAG_ASYNC_RECV;
        }
        else {
            if (!is_async) return;
            this->sync_cb.~move_only_function();
            new (this) decltype (this->async_cb) ();
            flags |= MANAPI__CURL_FLAG_ASYNC_RECV;
        }
    }
};

union manapi__fetch_handler_headers_t {
    std::move_only_function<bool(const std::shared_ptr<manapi::net::fetch> &)> sync_cb{};
    std::move_only_function<manapi::future<bool>(const std::shared_ptr<manapi::net::fetch> &)> async_cb;

    ~manapi__fetch_handler_headers_t() {}

    void reset (uint32_t &flags, bool is_async) MANAPIHTTP_NOEXCEPT {
        if (flags & MANAPI__CURL_FLAG_ASYNC_HEADERS) {
            if (is_async) return;
            this->async_cb.~move_only_function();
            new (this) decltype (this->sync_cb) ();
            flags ^= MANAPI__CURL_FLAG_ASYNC_HEADERS;
        }
        else {
            if (!is_async) return;
            this->sync_cb.~move_only_function();
            new (this) decltype (this->async_cb) ();
            flags |= MANAPI__CURL_FLAG_ASYNC_HEADERS;
        }
    }
};

union manapi__fetch_send_body_t {
    std::move_only_function<ssize_t(char *buffer, std::size_t size)> sync_cb;
    std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool &finish)> async_cb;

    manapi__fetch_send_body_t () {}

    ~manapi__fetch_send_body_t() {}

    void reset (uint32_t &flags, bool is_async) MANAPIHTTP_NOEXCEPT {
        if (flags & MANAPI__CURL_FLAG_ASYNC_SEND) {
            if (is_async) return;
            this->async_cb.~move_only_function();
            new (this) decltype (this->sync_cb) ();
            flags ^= MANAPI__CURL_FLAG_ASYNC_SEND;
        }
        else {
            if (!is_async) return;
            this->sync_cb.~move_only_function();
            new (this) decltype (this->async_cb) ();
            flags |= MANAPI__CURL_FLAG_ASYNC_SEND;
        }
    }

    void init () {
        new (this) decltype (this->sync_cb) ();
    }

    void clear (uint32_t &flags) MANAPIHTTP_NOEXCEPT {
        if (flags & MANAPI__CURL_FLAG_ASYNC_SEND) {
            this->async_cb.~move_only_function();
            flags ^= MANAPI__CURL_FLAG_ASYNC_SEND;
        }
        else {
            this->sync_cb.~move_only_function();
        }
    }
};

struct manapi__fetch_cb_t {
    manapi__fetch_recv_body_t recv;
    manapi__fetch_handler_headers_t headers;

    ~manapi__fetch_cb_t() {}
};

union manapi__fetch_body_t {
    std::string str;
    manapi::net::fetch_formdata formdata;
    manapi__fetch_send_body_t send;

    manapi__fetch_body_t () {}

    ~manapi__fetch_body_t () {}

    void reset (uint32_t &flags, uint32_t set) {
        if (set & flags) {
            return;
        }

        if (flags & MANAPI__CURL_FLAG_BODY_PLAIN) {
            this->str.~basic_string();
            flags ^= MANAPI__CURL_FLAG_BODY_PLAIN;
        }

        if (flags & MANAPI__CURL_FLAG_BODY_FORMDATA) {
            this->formdata.~fetch_formdata();
            flags ^= MANAPI__CURL_FLAG_BODY_FORMDATA;
        }

        if (flags & MANAPI__CURL_FLAG_BODY_CALLBACK) {
            this->send.clear(flags);
            this->send.~manapi__fetch_send_body_t();
            flags ^= MANAPI__CURL_FLAG_BODY_CALLBACK;
        }

        if (set & MANAPI__CURL_FLAG_BODY_PLAIN) {
            new (this) std::string ();
            flags |= MANAPI__CURL_FLAG_BODY_PLAIN;
        }

        if (set & MANAPI__CURL_FLAG_BODY_CALLBACK) {
            new (this) manapi__fetch_send_body_t ();
            this->send.init();
            flags |= MANAPI__CURL_FLAG_BODY_CALLBACK;
        }

        if (set & MANAPI__CURL_FLAG_BODY_FORMDATA) {
            new (this) manapi::net::fetch_formdata ();
            flags |= MANAPI__CURL_FLAG_BODY_FORMDATA;
        }
    }
};

struct manapi__fetch_builder_t {
    manapi::slice recv, send;

    std::unique_ptr<curl_slist, manapi__curl_slist_deleter> send_headers;

    std::map<std::string, std::string, std::less<>> recv_headers;

    manapi::async::promise_sync<void>::resolve_t resolve;

    uint32_t deps;
};

struct manapi::net::fetch::data_t {
    uint32_t flags;

    std::shared_ptr<CURL> curl;

    manapi__fetch_body_t body;

    manapi__fetch_builder_t builder;

    manapi__fetch_cb_t cbs;

    manapi::net::fetch *parent;
};

static manapi::future<> manapi__curl_header_handler__async (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *m_data, bool finish) {
    try {

        manapi_log_trace2 ("manapihttp::fetch", "fetch:calling async_cb");
        auto rhs = co_await m_data->cbs.headers.async_cb ( parent );
        manapi_log_trace2 ("manapihttp::fetch", "fetch:finish calling async_cb result = %d", (int)rhs);


        if (rhs) {

            assert(!(m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED));
            m_data->flags |= MANAPI__CURL_FLAG_HEADERS_RECEIVED;

        } else {

            m_data->flags |= MANAPI__CURL_FLAG_FAILED;

        }

    }
    catch (std::exception const &e) {
        manapi_log_trace (e.what());
        m_data->flags |= MANAPI__CURL_FLAG_FAILED;
    }

    if (finish) {

        co_return;

    }

    if (! (m_data->flags & MANAPI__CURL_FLAG_HEADER_RECVING) ) {

        assert(m_data->builder.deps);

        manapi_log_trace2 ("manapihttp::fetch", "fetch:deps=%u", m_data->builder.deps);

        if (!--m_data->builder.deps) {
            m_data->builder.resolve();
        }
        else {
            auto st = manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);

            if (!st.ok()) {
                m_data->flags |= MANAPI__CURL_FLAG_FAILED;
            }
        }
    }
}

static manapi::future<> manapi__curl_write_handler__async (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t *m_data, bool finish) {
    try {
        while (m_data->builder.send.empty()) {
            ssize_t rhs = -1;
            bool fin = false;

            m_data->builder.send.resize ( manapi::object_pool::area_size() * 16 );
            rhs = co_await m_data->body.send.async_cb (m_data->builder.send, fin /* reference */);

            if (rhs < 0) {
                m_data->builder.send.clear();
                m_data->flags |= MANAPI__CURL_FLAG_FAILED;

                co_return;
            }

            m_data->builder.send.resize (static_cast<std::size_t>(rhs)).unwrap ();

            if (fin || !rhs) {
                /* eof */
                m_data->flags |= MANAPI__CURL_FLAG_DATA_EOF;
                break;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace ( "fetch:set_async_body failed due to %s", e.what());
        m_data->flags |= MANAPI__CURL_FLAG_FAILED;
    }

    if (finish) {

        co_return;

    }

    if (! (m_data->flags & MANAPI__CURL_FLAG_SENDING) ) {
        assert(m_data->builder.deps);
        manapi_log_trace2 ("manapihttp::fetch", "fetch:deps=%u", m_data->builder.deps);
        if (!--m_data->builder.deps) {
            m_data->builder.resolve();
        }
        else {

            auto st = manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);

            if (!st.ok()) {
                m_data->flags |= MANAPI__CURL_FLAG_FAILED;
            }
        }
    }
}

static manapi::future<> manapi__curl_recv_handler__async (std::shared_ptr<manapi::net::fetch> parent, manapi::net::fetch::data_t * m_data, bool finish) {
    try {
        ssize_t rhs ;

        if (!m_data->builder.recv.empty() || finish) {
            while (true) {
                rhs = co_await m_data->cbs.recv.async_cb (
                        m_data->builder.recv, finish);

                if (rhs < 0) {
                    m_data->flags |= MANAPI__CURL_FLAG_FAILED;
                } else {
                    m_data->builder.recv.shift_add(static_cast<std::size_t>(rhs)).unwrap();

                    if (!m_data->builder.recv.empty()) {
                        continue;
                    }
                }

                break;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace (e.what());
        m_data->flags |= MANAPI__CURL_FLAG_FAILED;
    }

    if (finish) {

        co_return;

    }

    if (! (m_data->flags & MANAPI__CURL_FLAG_RECVING) ) {
        assert(m_data->builder.deps);
        manapi_log_trace2 ("manapihttp::fetch", "fetch:deps=%u", m_data->builder.deps);
        if (!--m_data->builder.deps) {
            m_data->builder.resolve();
        }
        else {
            auto st = manapi::async::current()->eventloop()->unpause_watch_curl(&m_data->curl);

            if (!st.ok()) {
                m_data->flags |= MANAPI__CURL_FLAG_FAILED;
            }
        }
    }
}

std::size_t manapi::net::curl_header_handler (char *buffer, std::size_t size, std::size_t n_items, void *userdata) {
    auto const data = static_cast <fetch::data_t *> (userdata);
    try {
        if (data->flags & MANAPI__CURL_FLAG_STATUS_PASSED) {
            std::string_view const str (buffer, size * n_items - 2);
            if (!str.empty()) {
                auto res = manapi::net::http::parse_header(str);
                if (res.ok()) {
                    auto header = res.unwrap();
                    auto key = std::string{header.first};
                    manapi::string::lower_ascii(key);
                    data->builder.recv_headers.insert({std::move(key), std::string{header.second}});
                }
            }
        }
        else {
            data->flags |= MANAPI__CURL_FLAG_STATUS_PASSED;
        }

        return n_items * size;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_header_handler", e.what());
    }

    return 0;
}

std::size_t manapi::net::curl_read_handler(char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto m_data = static_cast<fetch::data_t *> (user_p);
    size = static_cast<std::size_t> (size * nitems);

    try {

        if (m_data->flags & MANAPI__CURL_FLAG_ASYNC_SEND) {

            while (!(m_data->flags & MANAPI__CURL_FLAG_FAILED)) {

                std::size_t copy = std::min<std::size_t>(m_data->builder.send.size(), size);

                if (copy > 0) {
                    m_data->builder.send.copy_to(buffer, 0, copy).unwrap();
                    m_data->builder.send.shift_add(copy).unwrap();

                    return copy;
                }

                if (m_data->flags & MANAPI__CURL_FLAG_DATA_EOF) {
                    m_data->builder.send.clear();

                    /* end of stream */
                    return 0;
                }

                assert (m_data->builder.send.empty());

                m_data->flags |= MANAPI__CURL_FLAG_SENDING;

                manapi::async::run(manapi__curl_write_handler__async(
                        m_data->parent->shared_from_this(),
                        m_data,
                        false));

                m_data->flags ^= MANAPI__CURL_FLAG_SENDING;

                if (!m_data->builder.send.empty()) {
                    continue;
                }

                m_data->builder.deps++;

                return CURL_READFUNC_PAUSE;
            }
        }
        else {

            if (m_data->flags & MANAPI__CURL_FLAG_FAILED) {
                return CURL_READFUNC_ABORT;
            }

            auto rhs = m_data->body.send.sync_cb(buffer, size);

            if (rhs < 0)
                return CURL_READFUNC_ABORT;

            return static_cast<std::size_t>(rhs);
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_read_handler", e.what());
    }

    return CURL_READFUNC_ABORT;
}

std::size_t manapi::net::curl_write_handler (char *buffer, std::size_t size, std::size_t nitems, void *user_p) {
    auto m_data = static_cast <fetch::data_t *> (user_p);

    try {
        // call user handler
        size = (size * nitems);

        while (!(m_data->flags & MANAPI__CURL_FLAG_FAILED)) {

            if (m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED) [[likely]] {

                // handle data

                if (m_data->flags & MANAPI__CURL_FLAG_ASYNC_RECV) {

                    auto constexpr sz = manapi::object_pool::area_size() * 16;

                    if (m_data->builder.recv.size() < sz) {
                        m_data->builder.recv.push_back( buffer, size ).unwrap();
                        return size;

                    }

                    m_data->flags |= MANAPI__CURL_FLAG_RECVING;

                    manapi::async::run ( manapi__curl_recv_handler__async ( m_data->parent->shared_from_this(), m_data, false ) );

                    m_data->flags ^= MANAPI__CURL_FLAG_RECVING;

                    if (m_data->builder.recv.size() < sz) {
                        continue;
                    }

                    m_data->builder.deps++;

                    return CURL_WRITEFUNC_PAUSE;

                }
                else if (m_data->cbs.recv.sync_cb) {
                    std::size_t total = 0;
                    while (total < size) {
                        auto rhs = m_data->cbs.recv.sync_cb(buffer + total, size - total);

                        if (rhs < 0) {
                            break;
                        }

                        total += static_cast<std::size_t> (rhs);
                        assert(total <= size);
                    }

                    return total;
                }
                else {
                    break;
                }

            } else {
                // handle headers

                if (m_data->flags & MANAPI__CURL_FLAG_ASYNC_HEADERS) {

                    m_data->flags |= MANAPI__CURL_FLAG_HEADER_RECVING;

                    manapi::async::run ( manapi__curl_header_handler__async ( m_data->parent->shared_from_this(), m_data, false ) );

                    m_data->flags ^= MANAPI__CURL_FLAG_HEADER_RECVING;

                    if (m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED) {
                        continue;
                    }

                    m_data->builder.deps++;

                    return CURL_WRITEFUNC_PAUSE;
                } else if (m_data->cbs.headers.sync_cb) {
                    m_data->flags |= MANAPI__CURL_FLAG_HEADERS_RECEIVED;

                    if (m_data->cbs.headers.sync_cb ( m_data->parent->shared_from_this() )) {
                        continue;
                    }
                }
            }

            break;
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "fetch", "curl_write_handler", e.what());

        if (m_data->flags & MANAPI__CURL_FLAG_RECVING)
            m_data->flags ^= MANAPI__CURL_FLAG_RECVING;
    }
#if MANAPIHTTP_CURL_VERSION_REQUIRE(7,87,0)
    return CURL_WRITEFUNC_ERROR;
#else
    return 0;
#endif
}


// formdata

static size_t manapi__curl_send_formdata_cb_read (char *buffer, size_t size, size_t nitems, void *userp) {
    auto &func = *static_cast<decltype(manapi::net::fetch_formdata::multipart_param_value_file::callback) *> (userp);
    return func (buffer, size * nitems);
}

static int manapi__curl_send_formdata_cb_seek (void *userp, curl_off_t offset, int origin) {
    return CURL_SEEKFUNC_OK;
}

static void manapi__curl_send_formdata_cb_free (void *userp) {
    // pass
}

// main functions

static void manapi__fetch_cleanup(manapi::net::fetch *p, manapi::net::fetch::data_t *data) {

    data->cbs.headers.reset(data->flags, false);
    data->cbs.recv.reset(data->flags, false);

    data->builder.send.clear();
    data->builder.recv.clear();

    data->builder.send_headers.reset();
    data->builder.recv_headers.clear();

    data->builder.resolve = {};

    data->body.reset( data->flags, 0 );

    data->flags = 0;
}

manapi::net::fetch::fetch(std::string url) {

    this->m_data = std::make_unique <fetch::data_t>();
    this->m_data->parent = this;

    this->init(std::move(url)).unwrap();
}

manapi::net::fetch::~fetch() {

    this->clear();

}

manapi::status manapi::net::fetch::init(std::string url) {
    try {
        if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

        CURLcode status;

        this->m_data->curl = std::shared_ptr<CURL> (curl_easy_init(), curl_easy_cleanup);

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_URL, url.data())) != CURLE_OK) {
            return status_unknown(::curl_easy_strerror(status));
        }

        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status_or<std::shared_ptr<manapi::net::fetch>> manapi::net::fetch::create(std::string url) MANAPIHTTP_NOEXCEPT {
    return std::shared_ptr<fetch>(new fetch(std::move(url)));
}

manapi::future<manapi::status> manapi::net::fetch::perform(manapi::ctoken token) {
    auto st = manapi::status_ok();
    CURLcode status;

    auto _ = this->shared_from_this();

    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) co_return manapi::status_unavailable();

    try {
        this->m_data->flags |= MANAPI__CURL_FLAG_IS_PROCESSING;

        std::unique_ptr<curl_mime, manapi__curl_mime_deleter> form{nullptr};

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_NOSIGNAL, 1L)) != CURLE_OK)
            goto errcurl;

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HEADERFUNCTION, curl_header_handler)) !=
            CURLE_OK)
            goto errcurl;

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HEADERDATA, this->m_data.get())) != CURLE_OK)
            goto errcurl;

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_HTTPHEADER,
                                       this->m_data->builder.send_headers.get())) != CURLE_OK)
            goto errcurl;

        if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_WRITEFUNCTION, curl_write_handler))!=CURLE_OK
            || (status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_WRITEDATA, this->m_data.get()))!=CURLE_OK)
            goto errcurl;

        if (this->m_data->flags & MANAPI__CURL_FLAG_BODY_PLAIN) {
            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDS,
                                           this->m_data->body.str.data())) != CURLE_OK)
                goto errcurl;

            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                                           this->m_data->body.str.size())) != CURLE_OK)
                goto errcurl;

        } else if (this->m_data->flags & MANAPI__CURL_FLAG_BODY_CALLBACK) {

            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_READFUNCTION, curl_read_handler)) !=
                CURLE_OK)
                goto errcurl;

            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_READDATA, this->m_data.get())) != CURLE_OK)
                goto errcurl;

        } else if (this->m_data->flags & MANAPI__CURL_FLAG_BODY_FORMDATA) {
            form.reset(curl_mime_init(this->m_data->curl.get()));
            curl_mimepart *field = nullptr;
            for (auto &param: this->m_data->body.formdata) {
                field = curl_mime_addpart(form.get());

                if ((status = curl_mime_name(field, param.first.data())) != CURLE_OK)
                    goto errcurl;

                switch (param.second.type) {
                    case fetch_formdata::PARAM_DEFAULT: {
                        auto &str = param.second.strdata;
                        if ((status = curl_mime_data(field, str.data(), str.size())) != CURLE_OK)
                            goto errcurl;
                        break;
                    }
                    case fetch_formdata::PARAM_FILE: {
                        auto &str = param.second.strdata;
                        if ((status = curl_mime_filedata(field, str.data())) != CURLE_OK)
                            goto errcurl;
                        break;
                    }
                    case fetch_formdata::PARAM_CALLBACK: {
                        auto &cbdata = param.second.filedata;
                        if ((status = curl_mime_data_cb(field, static_cast<ssize_t>(cbdata.filesize),
                                                        manapi__curl_send_formdata_cb_read,
                                                        manapi__curl_send_formdata_cb_seek,
                                                        manapi__curl_send_formdata_cb_free, &cbdata.callback)) !=
                            CURLE_OK)
                            goto errcurl;
                        break;
                    }
                }
            }

            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_MIMEPOST, form.get())) != CURLE_OK)
                goto errcurl;
        }


        try {
            if (token.contains_cancel_callback()) {
                token.cancel_callback([z = this->m_data->curl] () mutable -> void {
                    auto st = manapi::async::current()->eventloop()->unwatch_curl(&z);
                    if (!st.ok()) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s",
                                         "fetch", "cancellation", st.msg().size(), st.msg().data());
                    }
                });
            }

            using promisez = manapi::async::promise_sync<void>;

            {
                struct perform_data_t {
                    manapi::net::fetch *p;
                    CURLcode status;
                } perform_data(this);

                co_await promisez ([&perform_data](promisez::resolve_t resolve) -> void {
                    perform_data.p->m_data->builder.resolve = std::move(resolve);
                    perform_data.p->m_data->builder.deps++;

                    try {
                        manapi::async::current()->eventloop()->watch_curl(&perform_data.p->m_data->curl,
                            [&perform_data](int status)
                                  -> void {
                            manapi_log_trace2 ( "manapihttp::fetch", "watch_curl:Unbind perform_data=%p deps=%u", &perform_data, perform_data.p->m_data->builder.deps);
                            perform_data.status = static_cast<CURLcode> (status);
                            if (!--perform_data.p->m_data->builder.deps) {
                                perform_data.p->m_data->builder.resolve();
                            }
                            manapi_log_trace2 ( "manapihttp::fetch", "watch_curl:Finish unbind perform_data=%p", &perform_data);
                        }).unwrap();
                    }
                    catch (...) {
                        perform_data.p->m_data->builder.deps--;
                        std::rethrow_exception(std::current_exception());
                    }
                });

                status = perform_data.status;
            }

            token.disable();

            if (status == CURLE_OK) {

                if ( this->m_data->flags & MANAPI__CURL_FLAG_FAILED ) {
                    st = manapi::status_unknown("fetch:failed");
                    goto errst;
                }

                if (!(this->m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED)) {

                    if (this->m_data->flags & MANAPI__CURL_FLAG_ASYNC_HEADERS) {
                        co_await manapi__curl_header_handler__async (this->shared_from_this(), this->m_data.get(), true);
                    }
                    else if (this->m_data->cbs.headers.sync_cb) {
                        if (this->m_data->cbs.headers.sync_cb ( this->shared_from_this() )) {
                            st = manapi::status_unknown("fetch:failed");
                            goto errst;
                        }
                    }

                    this->m_data->flags |= MANAPI__CURL_FLAG_HEADERS_RECEIVED;
                }

                if (this->m_data->flags & MANAPI__CURL_FLAG_ASYNC_RECV) {

                    co_await manapi__curl_recv_handler__async (this->shared_from_this(), this->m_data.get(), true);

                }

                else if (!this->m_data->builder.recv.empty() && this->m_data->cbs.recv.sync_cb) {

                    for (auto sv : this->m_data->builder.recv) {
                        std::size_t total = 0;

                        while (total < sv.size()) {
                            auto rhs = this->m_data->cbs.recv.sync_cb ( const_cast<char *>(sv.data()), sv.size() );
                            if (rhs < 0) co_return manapi::status_unknown("fetch:failed");
                            total += static_cast<std::size_t> (rhs);
                        }
                    }
                }

                manapi_log_trace(debug::LOG_TRACE_LOW, "fetch(%p):jobs have been finished", this->m_data.get());

                st = manapi::status_ok();
                goto errst;
            }
            else {
                goto errcurl;
            }
        }
        catch (std::exception const &e) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "fetch", e.what());
            status = CURLE_AGAIN;
        }
    }
    catch (std::bad_alloc const &) {
        st = status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fetch:Failed", e.what());
        st = status_internal("fetch:Failed");
    }

    goto errst;

errcurl:
    st = manapi::status_unknown(::curl_easy_strerror(status));

errst:
    assert (this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING);
    this->m_data->flags ^= MANAPI__CURL_FLAG_IS_PROCESSING;

    co_return std::move(st);
}

std::map <std::string, std::string, std::less<>> &manapi::net::fetch::headers() {
    return this->m_data->builder.recv_headers;
}

void manapi::net::fetch::clear() {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) throw std::runtime_error ("unavailable");

    manapi__fetch_cleanup (this, this->m_data.get());
}

manapi::status manapi::net::fetch::recv_body(std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT {
    if (this->m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED) return manapi::status_unavailable();

    this->m_data->cbs.recv.reset( this->m_data->flags, false );
    this->m_data->cbs.recv.sync_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::future<manapi::status_or<std::string>> manapi::net::fetch::text() {
    try {
        std::string content;

        auto res = this->recv_body ([&content](char *buffer, size_t size) MANAPIHTTP_NOEXCEPT -> ssize_t {
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

        res = co_await this->perform();
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


manapi::status manapi::net::fetch::recv_async_body(std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool fin)> handler) MANAPIHTTP_NOEXCEPT {

    if (this->m_data->flags & MANAPI__CURL_FLAG_HEADERS_RECEIVED) return manapi::status_unavailable();

    this->m_data->cbs.recv.reset( this->m_data->flags, true );
    this->m_data->cbs.recv.async_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::recv_headers(std::move_only_function<bool(const std::shared_ptr<manapi::net::fetch> &)> handler) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->cbs.headers.reset( this->m_data->flags, false );
    this->m_data->cbs.headers.sync_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::recv_async_headers(std::move_only_function<manapi::future<bool>(const std::shared_ptr<manapi::net::fetch> &)> handler) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->cbs.headers.reset( this->m_data->flags, true );
    this->m_data->cbs.headers.async_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::send_body(fetch_formdata params) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->body.reset( this->m_data->flags, MANAPI__CURL_FLAG_BODY_FORMDATA );
    this->m_data->body.formdata = std::move(params);

    return manapi::status_ok();
}


manapi::status manapi::net::fetch::method(std::string_view method) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    if (!manapi::string::equals("get", method, 0b01)) {

        CURLcode status;
        CURLoption opt;

        if (string::equals("post", method, 0b01))
            opt = CURLOPT_POST;
        else if (string::equals("put", method, 0b01))
            opt = CURLOPT_UPLOAD;
        else if (string::equals("head", method, 0b01))
            opt = CURLOPT_NOBODY;
        else {
            if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_CUSTOMREQUEST, method.data())) != CURLE_OK)
                return manapi::status_unknown(::curl_easy_strerror(status));

            return manapi::status_ok();
        }

        if ((status = curl_easy_setopt(this->m_data->curl.get(), opt, 1)) != CURLE_OK)
            return manapi::status_unknown(::curl_easy_strerror(status));
    }

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::send_body(std::string str) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->body.reset( this->m_data->flags, MANAPI__CURL_FLAG_BODY_PLAIN );
    this->m_data->body.str = std::move(str);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::send_body(manapi::slice_view str) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();


    auto st =  this->send_body ( [str = std::move(str)] ( char *buffer, std::size_t size ) mutable -> ssize_t {
        auto const copy = std::min <std::size_t> (str.size(), size);

        str.copy_to ( buffer, 0, copy ).unwrap();
        str = str.subslice ( 0, copy ).unwrap();

        return static_cast<ssize_t>(size);
    });

    if (!st) return std::move(st);

    return this->send_content_length ( static_cast<ssize_t>(str.size()) );
}

manapi::future<manapi::status> manapi::net::fetch::send_body(http::file_transfer_info file_info) {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) co_return manapi::status_unavailable();

    auto file = manapi::fs::fstream::create (file_info.filelocal()).unwrap();
    auto res = co_await file->open(ev::FS_O_RDONLY);
    if (!res.ok())
        co_return std::move(res);

    auto code = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDS, nullptr);

    if (code != CURLE_OK)
        co_return status_unknown(curl_easy_strerror(code));

    code = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, (co_await file->size()).unwrap());
    if (code != CURLE_OK)
        co_return status_unknown(curl_easy_strerror(code));

    co_return this->send_async_body([file = std::move(file), file_info = std::move(file_info)] (slice_view buffs, bool &fin) mutable
        -> manapi::future<ssize_t> {
        auto rhs = co_await file->fread(buffs);
        fin = file->eof();
        co_return rhs;
    });
}

manapi::status manapi::net::fetch::send_async_body(std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> handler) MANAPIHTTP_NOEXCEPT {

    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->body.reset( this->m_data->flags, MANAPI__CURL_FLAG_BODY_CALLBACK );

    this->m_data->body.send.reset( this->m_data->flags, true );
    this->m_data->body.send.async_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::send_body(std::move_only_function<ssize_t(char *, std::size_t)> handler) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    this->m_data->body.reset( this->m_data->flags, MANAPI__CURL_FLAG_BODY_CALLBACK );

    this->m_data->body.send.reset( this->m_data->flags, false );
    this->m_data->body.send.sync_cb = std::move(handler);

    return manapi::status_ok();
}

manapi::status manapi::net::fetch::send_header(std::string_view key, std::string_view value) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    try {
        if (key == http::H_CONTENT_LENGTH) {
            return this->send_content_length(manapi::string::strtoll(value));
        } else {
            auto const val = std::make_pair(key, value);
            auto const ss = http::stringify_header_size(val);

            char *buff;
            bool buff_own = false;

            if (ss < 4096) {
                buff = static_cast<char*>(alloca(ss + 1));
            }
            else {
                buff = static_cast<char*>(malloc ( ss + 1 ));
                if (!buff)
                    return manapi::status_resource_exhausted();
                buff_own = true;
            }
            auto hv = manapi::net::http::stringify_header(buff, ss, val);
            assert(ss == hv);
            buff[ss] = '\0';

            auto header = (
                    curl_slist_append(this->m_data->builder.send_headers.get(), buff));

            if (buff_own)
                free ( buff );

            if (!header)
                return manapi::status_resource_exhausted();

            this->m_data->builder.send_headers.release ();
            this->m_data->builder.send_headers.reset( header );
        }

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace(e.what());
        return manapi::status_unknown("send_header:failed");
    }
}

void *manapi::net::fetch::custom() MANAPIHTTP_NOEXCEPT {
    return this->m_data->curl.get();
}

uint16_t manapi::net::fetch::status_code() const MANAPIHTTP_NOEXCEPT {
    int code;

    CURLcode status;
    if ((status=curl_easy_getinfo(this->m_data->curl.get(), CURLINFO_HTTP_CODE, &code))!=CURLE_OK) {
        manapi_log_trace(::curl_easy_strerror(status));
        return 500;
    }

    return static_cast<uint16_t>(code);
}

manapi::status manapi::net::fetch::option(manapi::net::fetch::option_type type, int32_t value) MANAPIHTTP_NOEXCEPT {
    if ( this->m_data->flags & MANAPI__CURL_FLAG_IS_PROCESSING) return manapi::status_unavailable();

    CURLoption curlopt_type;

    switch (type) {
        case fetch::OPTION_HTTP1_1: curlopt_type = CURLOPT_HTTP_VERSION; value = CURL_HTTP_VERSION_1_1; break;
        case fetch::OPTION_HTTP2: curlopt_type = CURLOPT_HTTP_VERSION; value = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE; break;
        case fetch::OPTION_HTTP3: curlopt_type = CURLOPT_HTTP_VERSION; value = CURL_HTTP_VERSION_3; break;
        case fetch::OPTION_ALPN: curlopt_type = CURLOPT_SSL_ENABLE_ALPN; break;
        case fetch::OPTION_VERBOSE: curlopt_type = CURLOPT_VERBOSE; break;
        case fetch::OPTION_TCP_NO_DELAY: curlopt_type = CURLOPT_TCP_NODELAY; break;
        case fetch::OPTION_VERIFY_HOST: curlopt_type = CURLOPT_SSL_VERIFYHOST; break;
        case fetch::OPTION_VERIFY_PEER: curlopt_type = CURLOPT_SSL_VERIFYPEER; break;
        case fetch::OPTION_TIMEOUT: curlopt_type = CURLOPT_CONNECTTIMEOUT_MS; break;
        default: return manapi::status_invalid_argument("fetch:option invalid");
    }

    auto const res = curl_easy_setopt(this->m_data->curl.get(), curlopt_type, value);
    if (res == CURLE_OK)
        return status_ok();

    return status_invalid_argument(curl_easy_strerror(res));
}

manapi::status manapi::net::fetch::send_content_length(int64_t length) MANAPIHTTP_NOEXCEPT {
    CURLcode status;
    if ((status = curl_easy_setopt(this->m_data->curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, length)) != CURLE_OK)
        return manapi::status_unknown(::curl_easy_strerror(status));

    return manapi::status();
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
        auto const it = this->mdata.insert({std::move(name), std::move(res)});

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

#endif