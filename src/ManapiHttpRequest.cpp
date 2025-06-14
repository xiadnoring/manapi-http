#include <format>
#include <fstream>
#include <memory.h>

#include "ManapiHttpRequest.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiHttpMime.hpp"
#include "async/ManapiAsyncParallelRun.hpp"
#include "http/base_http.hpp"
#include "http/ManapiURLParams.hpp"
#include "include/ManapiDefaultErrors.hpp"
#include "include/ManapiHttpStructs.hpp"


manapi::net::http::request::request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::shared_conn *conn, worker::shared_worker worker, const http_handler_function *handler)  {
    this->conn_ = (conn);
    this->ip_data_ = std::move(ip_data);
    this->request_data = request_data;
    this->worker_ = std::move(worker);
    this->flags = 0;
    this->max_plain_body_size_ = 1000000;
    this->handler_ = handler;
}

manapi::net::http::request::~request () = default;

manapi::async::cancellation_action manapi::net::http::request::cancellation() {
    return this->conn_->get()->cancellation;
}

const manapi::net::http::manapi_socket_information &manapi::net::http::request::ip_data() const {
    return *this->ip_data_;
}

const std::string &manapi::net::http::request::method() const {
    return this->request_data->method;
}

int manapi::net::http::request::http_version() const {
    return this->request_data->http;
}

[[nodiscard]] const std::map<std::string, std::string> &manapi::net::http::request::ref_headers () const {
    return this->request_data->headers;
}

std::map<std::string, std::string> manapi::net::http::request::headers() const {
    return std::move(this->request_data->headers);
}

const std::string &manapi::net::http::request::param(const std::string &param) const {
    if (this->request_data->params.contains(param))
        return this->request_data->params.at(param);

    THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "cannot find param '{}'", param);
}

std::string manapi::net::http::request::dump() const {
    std::string result;

    result += "HTTP: " + this->http_version() + '\n';
    result += "Method: " + this->method() + '\n';
    result += "URL: " + this->request_data->uri + '\n';

    result += "Headers: \n";

    for (const auto &header: ref_headers()) {
        result += http::stringify_header(header) + '\n';
    }

    result += '\n';

    result += "body\n";

    return result;
}

manapi::future<std::string> manapi::net::http::request::text() {
    if (!(this->request_data->flags & internal::REQ_DATA_FLAG_HAS_BODY))
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "{}", "this method cannot have a body");
    }

    std::string body;

    if (this->request_data->body_size > this->max_plain_body_size_)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "plain body can have only {} length", max_plain_body_size_);
    }

    if (this->request_data->body_size >= 0) {
        body.resize(this->request_data->body_size);

        size_t j = 0;
        //size_t socket_block_size    = http_server->get_socket_block_size();

        co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,[&body, &j] (const char *data, ssize_t size, bool fin) -> ssize_t {
            memcpy (body.data() + j, data, size);
            j += size;
            return size;
        });
    }
    else {
        co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,[&body] (const char *data, ssize_t size, bool fin)
            -> ssize_t { body.append(data, size); return size; });
    }

    co_return body;
}

manapi::future<manapi::json> manapi::net::http::request::json()
{
    // TODO: check with json_mask during processing read_mask()
    const auto &post_mask = this->post_mask();

    if (post_mask) {
        json_builder builder = json_builder (*post_mask);
        co_await read_body_(this->worker_.get(), this->conn_, this->request_data,[&builder] (const char *data, ssize_t size, bool fin)
            -> ssize_t { builder << std::string_view (data, size); return size; });

        co_return std::move(builder.get());
    }
    else {
        json_builder builder = json_builder ();
        co_await read_body_(this->worker_.get(), this->conn_, this->request_data,[&builder] (const char *data, ssize_t size, bool fin)
            -> ssize_t { builder << std::string_view (data, size); return size; });

        co_return std::move(builder.get());
    }
}

manapi::future<> manapi::net::http::request::form (formdata_recv::onparam_cb_t cb) {
    formdata_recv fdata (request::read_async_body_, this->worker_.get(), this->conn_, this->request_data);
    co_await fdata.get(std::move(cb));
}

manapi::future<> manapi::net::http::request::callback_sync(onrecv_sync_cb callback) {
    co_return co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<> manapi::net::http::request::callback_async(onrecv_async_cb callback) {
    co_return co_await this->read_async_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<> manapi::net::http::request::file(std::string filepath) {
    manapi::filesystem::fstream f (filepath);
    auto res = co_await f.open(ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_TRUNC);

    if (!res.ok()) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FILESYSTEM_FAILED, "http request: Failed to open the file ({}) to write", filepath);
    }

    std::exception_ptr err{nullptr};

    try {
        co_await this->read_async_body_(this->worker_.get(), this->conn_, this->request_data, [&] (const char *data, ssize_t size, bool fin)
            -> manapi::future<ssize_t> { return f.write(data, size); });
    }
    catch (...) {
        err = std::current_exception();
    }

    co_await f.close();

    if (err) {
        std::rethrow_exception(std::move(err));
    }
}

ssize_t manapi::net::http::request::left() {
    return this->request_data->body_size;
}

const std::string & manapi::net::http::request::get(const std::string &key) {
    this->prepare_get_params_();

    auto it = this->get_params_->find(key);
    if (it == this->get_params_->end()) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_INVALID_ARGUMENT, "GET param {} is missing", key);
    }

    return it->second;
}

bool manapi::net::http::request::contains_get_param(const std::string &key) {
    this->prepare_get_params_();
    return this->get_params_->contains(key);
}

void manapi::net::http::request::max_plain_body_size(const size_t &size) {
    this->max_plain_body_size_ = size;
}

bool manapi::net::http::request::contains_header(const std::string &name) {
    return this->request_data->headers.contains(name);
}

const std::string &manapi::net::http::request::header(const std::string &name) {
    return this->request_data->headers.at(name);
}

void manapi::net::http::request::prepare_get_params_() {
    if (!this->get_params_) {
        this->get_params_ = std::make_unique<decltype(this->get_params_)::element_type>();

        if (this->request_data->divided != -1) {
            *this->get_params_ = http::parse_get_params (this->request_data->path[this->request_data->divided]);
        }

        // verify params
        auto &mask = this->get_mask();
        if (mask && !mask->valid(*this->get_params_)) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INVALID_ARGUMENT, "GET params verify failed");
        }
    }
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http::request::post_mask() const {
    return this->handler_->post_mask;
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http::request::get_mask() const {
    return this->handler_->get_mask;
}

void manapi::net::http::request::stop_propagation() {
    this->propagation(false);
}

void manapi::net::http::request::propagation(bool state) {
    if (state) {
        this->flags |= internal::REQUEST_FLAG_IS_PROPAGATION;
    }
    else if (this->flags & internal::REQUEST_FLAG_IS_PROPAGATION) {
        this->flags ^= internal::REQUEST_FLAG_IS_PROPAGATION;
    }
}

bool manapi::net::http::request::propagation() const {
    return this->flags & internal::REQUEST_FLAG_IS_PROPAGATION;
}

manapi::future<void> manapi::net::http::request::read_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_sync_cb handler) {
    using promise = manapi::async::promise<void, std::false_type>;
    std::unique_ptr<worker::worker_watcher_cb> prev{nullptr};
    int pflags;
    std::exception_ptr err;

    worker->waiting(*conn, true);

    try {
        co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            auto cb = std::make_unique<worker::worker_watcher_cb>(
                [worker, req, resolve = std::move(resolve), reject = std::move(reject), handler = std::move(handler)] (
                const worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, worker::ibuffpool_t *p) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(
                            manapi::ERR_ABORTED, manapi::error::default_msgs[manapi::error::ERRMSG_CONNECTION_WAS_CLOSED])));
                        goto finish;
                    }
                    if (flags & ev::READ) {
                        try {
                            ssize_t size;
                            bool flg = false;

                            if (req->body_size >= 0) {
                                size = std::min(req->body_size, static_cast<ssize_t> (nsize));
                                if (req->body_size == size)
                                    flg = true;
                            }
                            else
                                size = static_cast<ssize_t> (nsize);

                            ssize_t rhs = 0;
                            while (rhs < size) {
                                auto const copy = size - rhs;

                                auto const res = handler (buffer + rhs, copy, flg);
                                if (res >= 0) {
                                    if (copy > res) {
                                        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(
                                            manapi::ERR_INVALID_ARGUMENT, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                        goto finish;
                                    }

                                    rhs += res;

                                    req->body_size -= res;

                                    continue;
                                }
                                reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                    manapi::ERR_INVALID_ARGUMENT, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                goto finish;
                            }

                            if (!req->body_size) {
                                auto const copy = static_cast<int>(size - rhs);
                                if (copy) {
                                    assert(req->buffer == nullptr);
                                    auto object = worker->bufferpool().buffer(nsize - copy);
                                    memcpy(object.data(), buffer + copy, nsize - copy);
                                    req->buffer = std::move(object);
                                }
                                resolve();
                                goto finish;
                            }
                        }
                        catch (std::exception const &e) {
                            reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                manapi::ERR_INVALID_ARGUMENT, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], e.what())));
                            goto finish;
                        }
                    }
                    if (flags & worker::base::CONN_RECV_END) {
                        resolve();
                        goto finish;
                    }

                    return;
                    finish: {
                        auto const wrk_ = worker;
                        wrk_->event_on(conn, nullptr);
                        wrk_->event_flags(conn, 0);
                    }
            });

            prev = worker->event_on(*conn, std::move(cb));
            pflags = worker->event_flags(*conn, ev::READ);


        });
    }
    catch (...) {
        err = std::current_exception();
    }

    worker->waiting(*conn, false);

    if (prev) {
        worker->event_on(*conn, std::move(prev));
        worker->event_flags(*conn, pflags);
    }

    if (err) {
        std::rethrow_exception(std::move(err));
    }
}

manapi::future<> manapi::net::http::request::read_async_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_async_cb handler) {
    using promise = manapi::async::promise<void, std::false_type>;
    using handler_t = decltype(handler);

    struct ctx_cb_t_ {
        net::worker::base *worker;
        request_data_t *req;
        promise::resolve_t resolve;
        promise::reject_t reject;
        worker::shared_conn conn;
        handler_t handler;
        std::unique_ptr<worker::worker_watcher_cb> prev{nullptr};
        int pflags;
        int cnt;
        async::mutex mx;
    } ctx_cb {};

    ctx_cb.handler = std::move(handler);
    ctx_cb.worker = worker;
    ctx_cb.req = req;
    ctx_cb.conn = *conn;

    std::exception_ptr err;

    worker->waiting(*conn, true);

    try {
        co_await promise ([&ctx_cb] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            ctx_cb.resolve = std::move(resolve);
            ctx_cb.reject = std::move(reject);

            auto cb = std::make_unique<worker::worker_watcher_cb>(
                [&ctx_cb] (
                const worker::shared_conn & conn, int flags, const char * buffer, ssize_t nsize, worker::ibuffpool_t *p) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        ctx_cb.reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(
                            manapi::ERR_ABORTED, manapi::error::default_msgs[manapi::error::ERRMSG_CONNECTION_WAS_CLOSED])));
                        goto finish;
                    }
                    if (flags & ev::READ) {
                        worker::ibuffpool_t buff;

                        if (!p) {
                            buff = ctx_cb.worker->bufferpool().buffer(nsize);
                            memcpy (buff.data(), buffer, nsize);

                            p = &buff;

                            buffer = buff.data();
                        }

                        assert(!(p->empty()));

                        ctx_cb.worker->waiting(conn, false);
                        ctx_cb.worker->event_toggle(conn, false, ev::READ);
                        ctx_cb.cnt++;
                        manapi::async::run (manapi::async::invoke(
                            [] (const worker::shared_conn & conn, worker::ibuffpool_t p, const char * buffer, ssize_t nsize, ctx_cb_t_ *ctx_cb, int flags) -> manapi::future<> {
                                try {

                                    ssize_t size;
                                    bool flg = false;
                                    if (ctx_cb->req->body_size >= 0) {
                                        size = std::min(ctx_cb->req->body_size, static_cast<ssize_t> (nsize));

                                        if (ctx_cb->req->body_size == size)
                                            flg = true;
                                    }
                                    else {
                                        size = static_cast<ssize_t> (nsize);
                                    }

                                    ssize_t rhs = 0;
                                    while (rhs < size) {
                                        auto const copy = size - rhs;

                                        auto const res = co_await ctx_cb->handler (buffer + rhs, copy, flg);
                                        if (res >= 0) {
                                            if (copy > res) {
                                                ctx_cb->reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(
                                                    manapi::ERR_INVALID_ARGUMENT, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                                goto finish;
                                            }

                                            rhs += res;

                                            ctx_cb->req->body_size -= res;

                                            continue;
                                        }

                                        ctx_cb->reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                            manapi::ERR_INVALID_ARGUMENT, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                        goto finish;
                                    }

                                    if (!ctx_cb->req->body_size
                                        || (flags & worker::base::CONN_RECV_END)) {
                                        auto const copy = static_cast<int>(size - rhs);
                                        if (copy) {
                                            assert(ctx_cb->req->buffer == nullptr);
                                            auto object = ctx_cb->worker->bufferpool().buffer(size - copy);
                                            memcpy (object.data(), buffer + copy, size - copy);
                                            ctx_cb->req->buffer = std::move(object);
                                        }
                                        ctx_cb->resolve();
                                        goto finish;
                                    }

                                }
                                catch (...) {
                                    ctx_cb->reject (std::current_exception());
                                    goto finish;
                                }

                                ctx_cb->worker->waiting(conn, true);
                                ctx_cb->worker->event_toggle(conn, true, ev::READ);
                                ctx_cb->cnt--;
                                ctx_cb->mx.unlock();

                                co_return;

                                finish: {
                                    auto const ctx_cb_ = ctx_cb;
                                    ctx_cb_->worker->event_flags(conn, 0);
                                    ctx_cb_->worker->event_on(conn, nullptr);
                                    ctx_cb_->cnt--;
                                    ctx_cb_->mx.unlock();
                                }
                        }, ctx_cb.conn, std::move(*p), buffer, nsize, &ctx_cb, flags));
                    }
                    else if (flags & worker::base::CONN_RECV_END) {
                        ctx_cb.resolve();
                        goto finish;
                    }

                    return;
                    finish: {
                        auto const wrk_ = ctx_cb.worker;

                        wrk_->event_on(conn, nullptr);
                        wrk_->event_flags(conn, 0);
                    }
            });

            ctx_cb.prev = ctx_cb.worker->event_on(ctx_cb.conn, std::move(cb));
            ctx_cb.pflags = ctx_cb.worker->event_flags(ctx_cb.conn, ev::READ);


        });
    }
    catch (...) {
        err = std::current_exception();
    }

    while (true) {
        if (ctx_cb.cnt) {
            co_await ctx_cb.mx.lock();
            continue;
        }
        break;
    }

    worker->waiting(*conn, false);

    if (ctx_cb.prev) {
        worker->event_on(ctx_cb.conn, std::move(ctx_cb.prev));
        worker->event_flags(ctx_cb.conn, ctx_cb.pflags);
    }

    if (err) {
        std::rethrow_exception(err);
    }
}
