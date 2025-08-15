#include <format>
#include <fstream>
#include <memory.h>

#include "http/ManapiHttpMime.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiURLParams.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "std/ManapiAsyncParallelRun.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "../include/ManapiHttpStructs.hpp"
#include "../include/ManapiSiteInternal.hpp"


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

int manapi::net::http::request::http() const {
    return this->request_data->http;
}

[[nodiscard]] const std::map<std::string, std::string, std::less<>> &manapi::net::http::request::ref_headers () const {
    return this->request_data->headers;
}

std::map<std::string, std::string, std::less<>> manapi::net::http::request::headers() const {
    return std::move(this->request_data->headers);
}

manapi::error::status_or<std::string_view> manapi::net::http::request::param(std::string_view param) const MANAPIHTTP_NOEXCEPT {
    auto it = this->request_data->params.find(param);
    if (it == this->request_data->params.end())
        return manapi::error::status_not_found("params:Not found");

    return std::string_view{it->second};
}

manapi::error::status_or<std::pair<std::string, std::string>> manapi::net::http::request::param_extract( std::string_view param) MANAPIHTTP_NOEXCEPT {
    auto it = this->request_data->params.find(param);
    if (it == this->request_data->params.end())
        return manapi::error::status_not_found("params:Not found");

    auto data = this->request_data->params.extract(it);
    return std::make_pair(std::move(data.key()), std::move(data.mapped()));
}

manapi::future<manapi::error::status_or<std::string>> manapi::net::http::request::text() {
    try {
        if (!(this->request_data->flags & internal::REQ_DATA_FLAG_HAS_BODY))
            co_return manapi::error::status_invalid_argument("req:Body is denied");

        std::string body;

        if (this->request_data->body_size > this->max_plain_body_size_)
            co_return manapi::error::status_invalid_argument("req:Body is too large");

        if (this->request_data->body_size >= 0) {
            body.resize(this->request_data->body_size);

            size_t j = 0;

            auto rhs = co_await manapi::net::http::request::read_body_(this->worker_.get(), this->conn_, this->request_data,[&body, &j] (const char *data, ssize_t size, bool fin) -> ssize_t {
                memcpy (body.data() + j, data, size);
                j += size;
                return size;
            });

            if (!rhs.ok())
                co_return std::move(rhs);
        }
        else {
            auto rhs = co_await manapi::net::http::request::read_body_(this->worker_.get(), this->conn_, this->request_data,[&body] (const char *data, ssize_t size, bool fin)
                -> ssize_t { body.append(data, size); return size; });

            if (!rhs.ok())
                co_return std::move(rhs);
        }

        co_return std::move(body);
    }
    catch (std::bad_alloc const &) {
        co_return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "req:Failed", e.what());
        co_return error::status_internal("req:Failed");
    }
}

manapi::future<manapi::json_error::status_or<manapi::json>> manapi::net::http::request::json(const manapi::json_mask *mask)
{
    try {
        struct builder_callback_data_t {
            manapi::json_builder *builder_;
            json_error::status *status_;
        }
        data_callback{};

        if (mask) {
            json_builder builder = json_builder (*mask);
            auto status = json_error::status_ok();

            data_callback.builder_ = &builder;
            data_callback.status_ = &status;

            auto res = co_await read_body_(this->worker_.get(), this->conn_, this->request_data,
                [&data_callback] (const char *data, ssize_t size, bool fin)
                -> ssize_t {
                *data_callback.status_ = data_callback.builder_->parse(std::string_view (data, size));
                if (!*data_callback.status_)
                    return -1;

                return size;
            });

            if (!status)
                co_return std::move(status);

            if (!res.ok())
                co_return json_error::status(res);
            auto json_res = builder.get();
            if (!json_res)
                co_return std::move(json_res);
            co_return json_res.unwrap();
        }
        else {
            auto status = json_error::status_ok();
            json_builder builder = json_builder ();

            data_callback.builder_ = &builder;
            data_callback.status_ = &status;

            auto res = co_await read_body_(this->worker_.get(), this->conn_, this->request_data,
                [&data_callback] (const char *data, ssize_t size, bool fin)
            -> ssize_t {
                *data_callback.status_ = data_callback.builder_->parse(std::string_view (data, size));
                if (!*data_callback.status_)
                    return -1;

                return size;
            });
            if (!status)
                co_return std::move(status);

            if (!res.ok())
                co_return json_error::status(res);

            auto json_res = builder.get();
            if (!json_res)
                co_return std::move(json_res);

            co_return json_res.unwrap();
        }
    }
    catch (std::bad_alloc const &) {
        co_return json_error::status(error::status_resource_exhausted());
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "req:Json failed", e.what());
    }
    co_return json_error::status(error::status_internal("req:Json failed"));
}

manapi::future<manapi::error::status> manapi::net::http::request::form (formdata_recv::onparam_cb_t cb) {
    try {
        formdata_recv fdata (request::read_async_body_, this->worker_.get(), this->conn_, this->request_data);
        co_return co_await fdata.get(std::move(cb));
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "req:Form failed", e.what());
        co_return error::status_internal("req:Form failed");
    }
}

manapi::future<manapi::error::status> manapi::net::http::request::callback_sync(onrecv_sync_cb callback) {
    co_return co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<manapi::error::status> manapi::net::http::request::callback_async(onrecv_async_cb callback) {
    co_return co_await this->read_async_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<manapi::error::status> manapi::net::http::request::file(std::string filepath) {
    auto status = manapi::filesystem::fstream::create (std::move(filepath), this->cancellation().sub());
    if (!status)
        co_return status.err();

    auto f = status.unwrap();

    auto res = co_await f.open(ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_TRUNC);

    if (!res.ok())
        co_return error::status_invalid_argument("formdata:Failed to open file");

    res = co_await manapi::net::http::request::read_async_body_(this->worker_.get(), this->conn_, this->request_data,
        [f] (slice_view buffs, bool fin) mutable
            -> manapi::future<ssize_t> { return f.write(buffs); });

    co_await f.close();

    co_return std::move(res);
}

ssize_t manapi::net::http::request::left() {
    return this->request_data->body_size;
}

manapi::json_error::status manapi::net::http::request::verify_get(const manapi::json_mask *mask) MANAPIHTTP_NOEXCEPT {
    return this->prepare_get_params_(mask);
}

manapi::json_error::status_or<std::string_view> manapi::net::http::request::get(std::string_view key) {
    auto err = this->prepare_get_params_(nullptr);
    if (!err)
        return std::move(err);

    auto it = this->get_params_->find(key);
    if (it == this->get_params_->end())
        return json_error::status{error::status_not_found("get params:Not found")};

    return std::string_view{it->second};
}

manapi::json_error::status_or<std::pair<std::string, std::string>> manapi::net::http::request::get_extract(std::string_view key) {
    auto err = this->prepare_get_params_(nullptr);
    if (!err)
        return std::move(err);

    auto it = this->get_params_->find(key);
    if (it == this->get_params_->end())
        return json_error::status{error::status_not_found("get params:Not found")};

    auto data = this->get_params_->extract(it);
    return std::make_pair(std::move(data.key()), std::move(data.mapped()));
}

manapi::json_error::status manapi::net::http::request::contains_get_param(std::string_view key) {
    auto err = this->prepare_get_params_(nullptr);
    if (!err)
        return std::move(err);

    if (this->get_params_->find(key) == this->get_params_->end())
        return error::status_not_found("get params:Not found");

    return error::status_ok();
}

void manapi::net::http::request::max_plain_body_size(size_t size) {
    this->max_plain_body_size_ = size;
}

bool manapi::net::http::request::contains_header(std::string_view name) {
    return this->request_data->headers.find(name) != this->request_data->headers.end();
}

manapi::error::status_or<std::string_view> manapi::net::http::request::header(std::string_view name) {
    auto it = this->request_data->headers.find(name);
    if (it==this->request_data->headers.end())
        return error::status_not_found("headers:Not found");
    return std::string_view{it->second};
}

manapi::error::status_or<std::pair<std::string, std::string>> manapi::net::http::request::header_extract( std::string_view name) {
    auto it = this->request_data->headers.find(name);
    if (it==this->request_data->headers.end())
        return error::status_not_found("headers:Not found");
    auto data = this->request_data->headers.extract(it);
    return std::make_pair(std::move(data.key()), std::move(data.mapped()));
}

manapi::json_error::status manapi::net::http::request::prepare_get_params_(const manapi::json_mask *mask) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->get_params_) {
            this->get_params_ = std::make_unique<decltype(this->get_params_)::element_type>();

            if (this->request_data->divided != -1) {
                *this->get_params_ = http::parse_get_params (this->request_data->path[this->request_data->divided]);
            }

            // verify params
            if (mask) {
                auto res = mask->valid(*this->get_params_);
                if (!res.ok())
                    return std::move(res);
            }
        }
        return json_error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "get params:Failed" ,e.what());
        return error::status_internal("get params:Failed");
    }
}

void manapi::net::http::request::stop_propagation() {
    this->propagation(false);
}

void manapi::net::http::request::propagation(bool state) {
    if (!state) {
        this->flags |= internal::REQUEST_FLAG_IS_NO_PROPAGATION;
    }
    else if (this->flags & internal::REQUEST_FLAG_IS_NO_PROPAGATION) {
        this->flags ^= internal::REQUEST_FLAG_IS_NO_PROPAGATION;
    }
}

std::move_only_function<void(std::string_view name, std::string_view value)> & manapi::net::http::request::trailer_recv() MANAPIHTTP_NOEXCEPT {
    return this->trailer_recv_cb_;
}

manapi::error::status manapi::net::http::request::trailer_recv(std::move_only_function<void(std::string_view name, std::string_view value)> cb) MANAPIHTTP_NOEXCEPT {
    this->trailer_recv_cb_ = std::move(cb);
    return error::status_ok();
}

bool manapi::net::http::request::propagation() const {
    return !(this->flags & internal::REQUEST_FLAG_IS_NO_PROPAGATION);
}

manapi::future<manapi::error::status> manapi::net::http::request::read_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_sync_cb handler) {
    using promise = manapi::async::promise_sync<manapi::error::status>;

    manapi::error::status status;
    worker->waiting(*conn, true);

    struct ctx_cb_t_ {
        net::worker::base *worker;
        request_data_t *req;
        promise::resolve_t resolve;
        onrecv_sync_cb handler;
        worker::shared_conn conn;
        int pflags;
        worker::worker_watcher_cb prev;
    } ctx_cb {};

    ctx_cb.worker = worker;
    ctx_cb.req = req;
    ctx_cb.handler = std::move(handler);
    ctx_cb.conn = *conn;

    try {
        status = co_await promise ([&ctx_cb] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            ctx_cb.resolve = std::move(resolve);

            worker::worker_watcher_cb cb =
                [&ctx_cb] (
                const worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, worker::ibuffpool_t *p) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        ctx_cb.resolve (manapi::error::status_aborted("read_body:Connection was closed"));
                        goto finish;
                    }
                    if (flags & ev::READ) {
                        try {
                            ssize_t size;
                            bool flg = false;

                            if (ctx_cb.req->body_size >= 0) {
                                size = std::min(ctx_cb.req->body_size, static_cast<ssize_t> (nsize));
                                if (ctx_cb.req->body_size == size)
                                    flg = true;
                            }
                            else
                                size = static_cast<ssize_t> (nsize);

                            ssize_t rhs = 0;
                            while (rhs < size) {
                                auto const copy = size - rhs;

                                auto const res = ctx_cb.handler (buffer + rhs, copy, flg);
                                if (res >= 0) {
                                    if (copy > res) {
                                        ctx_cb.resolve(manapi::error::status_internal("read_body:Something gets wrong"));
                                        goto finish;
                                    }

                                    rhs += res;

                                    ctx_cb.req->body_size -= res;

                                    continue;
                                }
                                ctx_cb.resolve (manapi::error::status_internal("read_body:Something gets wrong"));
                                goto finish;
                            }

                            if (!ctx_cb.req->body_size) {
                                auto const copy = static_cast<int>(size - rhs);
                                if (copy) {
                                    ctx_cb.worker->feed_event(conn, worker::base::CONN_TOP_READ,
                                           static_cast<const char *>(buffer + rhs), copy, nullptr);
                                }
                                ctx_cb.resolve(manapi::error::status_ok());
                                goto finish;
                            }
                        }
                        catch (std::exception const &e) {
                            manapi_log_error("%s failed due to %s", "read_body:Failed", e.what());
                            ctx_cb.resolve (manapi::error::status_internal("read_body:Something gets wrong"));
                            goto finish;
                        }
                    }
                    if (flags & worker::base::CONN_RECV_END) {
                        ctx_cb.resolve(manapi::error::status_ok());
                        goto finish;
                    }

                    return;

                    finish: {
                        auto const wrk_ = ctx_cb.worker;
                        wrk_->event_on(conn, nullptr);
                        wrk_->event_flags(conn, 0);
                    }
            };

            ctx_cb.prev = ctx_cb.worker->event_on(ctx_cb.conn, std::move(cb));
            ctx_cb.pflags = ctx_cb.worker->event_flags(ctx_cb.conn, ev::READ);
        });
    }
    catch (std::bad_alloc const &e) {
        status = error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "read_body:Failed", e.what());
        status = error::status_internal("read_body:Failed");
    }

    worker->waiting(*conn, false);

    if (ctx_cb.prev) {
        worker->event_on(*conn, std::move(ctx_cb.prev));
        worker->event_flags(*conn, ctx_cb.pflags);
    }

    co_return std::move(status);
}

manapi::future<manapi::error::status> manapi::net::http::request::read_async_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, onrecv_async_cb handler) {
    using promise = manapi::async::promise_sync<manapi::error::status>;
    using handler_t = decltype(handler);

    struct ctx_cb_t_ {
        net::worker::base *worker;
        request_data_t *req;
        promise::resolve_t resolve;
        worker::shared_conn conn;
        handler_t handler;
        worker::worker_watcher_cb prev{nullptr};
        int pflags;
        int cnt;
        async::mutex mx;
    } ctx_cb {};

    ctx_cb.handler = std::move(handler);
    ctx_cb.worker = worker;
    ctx_cb.req = req;
    ctx_cb.conn = *conn;

    manapi::error::status status;

    worker->waiting(*conn, true);

    try {
        status = co_await promise ([&ctx_cb] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            ctx_cb.resolve = std::move(resolve);

            worker::worker_watcher_cb cb =
                [&ctx_cb] (
                const worker::shared_conn & conn, int flags, const char * buffer, ssize_t nsize, worker::ibuffpool_t *p) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        ctx_cb.resolve(manapi::error::status_aborted("read_async_body:Connection was closed"));
                        goto finish;
                    }

                    if (flags & ev::READ) {
                        slice buffs = ctx_cb.worker->bufferpool().slice(nsize).unwrap();
                        buffs.copy_from(buffer, 0, nsize).unwrap();

                        while (ctx_cb.worker->recv_count(ctx_cb.conn))
                            buffs.push_back(ctx_cb.worker->recv_first_buffer(ctx_cb.conn)).unwrap();

                        if (ctx_cb.worker->event_flags(conn) & worker::base::CONN_RECV_END)
                            flags |= worker::base::CONN_RECV_END;

                        ctx_cb.worker->waiting(conn, false);
                        ctx_cb.worker->event_toggle(conn, false, ev::READ);
                        ctx_cb.cnt++;
                        manapi::async::run (manapi::async::invoke(
                            [] (const worker::shared_conn & conn, manapi::slice buffs, ctx_cb_t_ *ctx_cb, int flags) -> manapi::future<> {
                                try {

                                    ssize_t size;
                                    bool flg = false;
                                    if (ctx_cb->req->body_size >= 0) {
                                        size = std::min(ctx_cb->req->body_size, static_cast<ssize_t> (buffs.size()));

                                        if (ctx_cb->req->body_size == size)
                                            flg = true;
                                    }
                                    else {
                                        size = static_cast<ssize_t> (buffs.size());
                                    }

                                    auto buffsview = buffs.subslice(0, size).unwrap();

                                    ssize_t rhs = 0;
                                    while (rhs < size) {
                                        auto const copy = size - rhs;

                                        auto const res = co_await ctx_cb->handler (buffsview, flg);
                                        if (res >= 0) {
                                            if (copy > res) {
                                                ctx_cb->resolve(manapi::error::status_internal("read_async_body:Something gets wrong"));
                                                goto finish;
                                            }

                                            rhs += res;

                                            ctx_cb->req->body_size -= res;

                                            buffsview = buffs.subslice(res).unwrap();

                                            continue;
                                        }

                                        ctx_cb->resolve (manapi::error::status_internal("read_async_body:Something gets wrong"));
                                        goto finish;
                                    }

                                    if (ctx_cb->req->body_size <= 0) {
                                        auto const copy = static_cast<int>(size - rhs);
                                        if (copy) {
                                            for (auto it = buffsview.begin(); it != buffsview.end(); ++it) {
                                                ctx_cb->worker->feed_event(ctx_cb->conn, worker::base::CONN_TOP_READ,
                                                    static_cast<const char *>(it.buffer()), it.size(), nullptr);
                                            }
                                        }
                                        ctx_cb->resolve(manapi::error::status_ok());
                                        goto finish;
                                    }

                                }
                                catch (std::exception const &e) {
                                    manapi_log_error("%s due to %s", "read_async_body:Failed", e.what());
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
                        }, ctx_cb.conn, std::move(buffs), &ctx_cb, flags));
                    }
                    else if (flags & worker::base::CONN_RECV_END) {
                        ctx_cb.resolve(manapi::error::status_ok());
                        goto finish;
                    }

                    return;
                    finish: {
                        auto const wrk_ = ctx_cb.worker;

                        wrk_->event_on(conn, nullptr);
                        wrk_->event_flags(conn, 0);
                    }
            };

            ctx_cb.prev = ctx_cb.worker->event_on(ctx_cb.conn, std::move(cb));
            ctx_cb.pflags = ctx_cb.worker->event_flags(ctx_cb.conn, ev::READ);


        });
    }
    catch (std::bad_alloc const &e) {
        status = error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "read_async_body:Failed", e.what());
        status = manapi::error::status_internal("read_async_body:Failed");
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

    co_return std::move(status);
}
