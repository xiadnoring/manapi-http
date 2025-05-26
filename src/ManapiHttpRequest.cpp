#include <format>
#include <fstream>
#include <memory.h>

#include "ManapiHttpRequest.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiHttpMime.hpp"
#include "http/base_http.hpp"
#include "http/ManapiURLParams.hpp"
#include "include/ManapiDefaultErrors.hpp"
#include "include/ManapiHttpStructs.hpp"


manapi::net::http::request::request(std::unique_ptr<manapi::net::http::manapi_socket_information> ip_data, manapi::net::http::request_data_t *request_data, manapi::net::worker::shared_conn *conn, worker::shared_worker worker, const http_handler_functions *handler)  {
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

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PARAM_MISSING, "cannot find param '{}'", param);
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
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    std::string body;

    if (this->request_data->body_size > this->max_plain_body_size_)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_TOO_LONG, "plain body can have only {} length", max_plain_body_size_);
    }

    if (this->request_data->body_size >= 0) {
        body.resize(this->request_data->body_size);

        size_t j = 0;
        //size_t socket_block_size    = http_server->get_socket_block_size();

        co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,[&body, &j] (const char *data, ssize_t size) -> ssize_t {
            memcpy (body.data() + j, data, size);
            j += size;
            return size;
        });
    }
    else {
        co_await this->read_body_(this->worker_.get(), this->conn_, this->request_data,[&body] (const char *data, ssize_t size)
            -> ssize_t { body.append(data, size); return size; });
    }

    co_return body;
}

manapi::future<manapi::json> manapi::net::http::request::json()
{
    // TODO: check with json_mask during processing read_mask()
    const auto &post_mask = this->post_mask();

    json_builder builder = post_mask ? json_builder (*post_mask) : json_builder ();
    co_await read_body_(this->worker_.get(), this->conn_, this->request_data,[&builder] (const char *data, ssize_t size)
        -> ssize_t { builder << std::string_view (data, size); return size; });

    co_return std::move(builder.get());
}

manapi::future<> manapi::net::http::request::form (formdata_recv::onparam_cb_t cb) {
    formdata_recv fdata (request::read_async_body_, this->worker_.get(), this->conn_, this->request_data);
    co_await fdata.get(std::move(cb));
}

manapi::future<> manapi::net::http::request::callback_sync( std::move_only_function<ssize_t(const char *buffer, ssize_t size)> callback) {
    return this->read_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<> manapi::net::http::request::callback_async(std::move_only_function<manapi::future<ssize_t>(const char *buffer, ssize_t size)> callback) {
    return this->read_async_body_(this->worker_.get(), this->conn_, this->request_data,std::move(callback));
}

manapi::future<> manapi::net::http::request::file(std::string filepath) {
    manapi::filesystem::fstream f (filepath);
    co_await f.open(ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_TRUNC);

    if (!f.is_open()) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FILE_IO, "http request: Failed to open the file ({}) to write", filepath);
    }

    std::exception_ptr err{nullptr};

    try {
        co_await this->read_async_body_(this->worker_.get(), this->conn_, this->request_data, [&] (const char *data, ssize_t size)
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
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PARAM_MISSING, "GET param {} is missing", key);
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
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_GET_PARAMS_MASK_FAILED, "GET params verify failed");
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

manapi::future<void> manapi::net::http::request::read_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, std::move_only_function<ssize_t(const char *, ssize_t)> handler) {
    using promise = manapi::async::promise<void, std::false_type>;
    std::unique_ptr<worker::worker_watcher_cb> prev{nullptr};
    int pflags;
    std::exception_ptr err;

    try {
        co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            auto cb = std::make_unique<worker::worker_watcher_cb>(
                [worker, req, resolve = std::move(resolve), reject = std::move(reject), handler = std::move(handler)] (
                const worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(
                            manapi::ERR_HTTP_CONNECTION_WAS_CLOSED, manapi::error::default_msgs[manapi::error::ERRMSG_CONNECTION_WAS_CLOSED])));
                        goto finish;
                    }
                    if (flags & ev::READ) {
                        try {
                            ssize_t size;
                            if (req->body_size >= 0)
                                size = std::min(req->body_size, static_cast<ssize_t> (nsize));
                            else
                                size = static_cast<ssize_t> (nsize);

                            ssize_t rhs = 0;
                            while (rhs < size) {
                                auto const copy = size - rhs;

                                auto const res = handler (buffer + rhs, copy);
                                if (res >= 0) {
                                    if (copy > res) {
                                        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(
                                            manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                        goto finish;
                                    }

                                    rhs += res;

                                    req->body_size -= res;

                                    continue;
                                }
                                reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                    manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                goto finish;
                            }

                            if (!req->body_size) {
                                auto const copy = static_cast<int>(size - rhs);
                                if (copy) {
                                    assert(req->buffer == nullptr);
                                    auto object = worker->bufferpool()->get();
                                    object->resize(nsize - copy);
                                    memcpy(object->data(), buffer + copy, nsize - copy);
                                    req->buffer = std::move(object);
                                }
                                resolve();
                                goto finish;
                            }
                        }
                        catch (std::exception const &e) {
                            reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], e.what())));
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

            pflags = worker->event_flags(*conn, ev::READ);

            if (req->buffer) {
                cb->operator()(*conn, ev::READ, req->buffer->data(), req->buffer->size());
                req->buffer = {};
            }


            prev = worker->event_on(*conn, std::move(cb));
        });
    }
    catch (...) {
        err = std::current_exception();
    }

    if (prev) {
        worker->event_on(*conn, std::move(prev));
        worker->event_flags(*conn, pflags);
    }

    if (err) {
        std::rethrow_exception(std::move(err));
    }
}

manapi::future<> manapi::net::http::request::read_async_body_(worker::base *worker, worker::shared_conn *conn, request_data_t *req, std::move_only_function<manapi::future<ssize_t>(const char *, ssize_t)> handler) {
    using promise = manapi::async::promise<void, std::false_type>;
    std::unique_ptr<worker::worker_watcher_cb> prev{nullptr};
    int pflags;
    std::exception_ptr err;

    try {
        co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            auto cb = std::make_unique<worker::worker_watcher_cb>(
                [worker, req, resolve = std::move(resolve), reject = std::move(reject), handler = std::move(handler)] (
                const worker::shared_conn & conn, int flags, const char * buffer, ssize_t nsize) mutable -> void {
                    if (flags & ev::DISCONNECT) {
                        reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(
                            manapi::ERR_HTTP_CONNECTION_WAS_CLOSED, manapi::error::default_msgs[manapi::error::ERRMSG_CONNECTION_WAS_CLOSED])));
                        goto finish;
                    }
                    if (flags & ev::READ) {
                        worker->event_flags(conn, 0);
                        manapi::async::run ([&] () -> manapi::future<> {
                            ssize_t size;

                            if (req->body_size >= 0)
                                size = std::min(req->body_size, static_cast<ssize_t> (nsize));
                            else
                                size = static_cast<ssize_t> (nsize);

                            ssize_t rhs = 0;
                            while (rhs < size) {
                                auto const copy = size - rhs;

                                auto const res = co_await handler (buffer + rhs, copy);
                                if (res >= 0) {
                                    if (copy > res) {
                                        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(
                                            manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                        goto finish;
                                    }

                                    rhs += res;

                                    req->body_size -= res;

                                    continue;
                                }
                                reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                    manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], "invalid result")));
                                goto finish;
                            }

                            if (!req->body_size) {
                                auto const copy = static_cast<int>(size - rhs);
                                if (copy) {
                                    assert(req->buffer == nullptr);
                                    auto object = worker->bufferpool()->get();
                                    object->resize(size - copy);
                                    memcpy (object->data(), buffer + copy, size - copy);
                                    req->buffer = std::move(object);
                                }
                                resolve();
                                goto finish;
                            }

                            worker->event_flags(conn, ev::READ);

                            co_return;
                            finish: {
                                worker->event_flags(conn, 0);
                            }
                        }, [conn, worker, reject] (std::exception_ptr err) -> void {
                            if (err) {
                                std::string msg;
                                manapi::rethrow_exception_ptr(std::move(err), nullptr, &msg, nullptr);
                                reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION (
                                    manapi::ERR_HTTP_PROTOCOL_ERROR, manapi::error::default_msgs[manapi::error::ERRMSG_CUSTOM_CALLBACK_ERR1], msg)));
                                worker->event_flags(conn, 0);
                            }
                        });
                    }
                    else if (flags & worker::base::CONN_RECV_END) {
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

            pflags = worker->event_flags(*conn, ev::READ);

            if (req->buffer) {
                cb->operator()(*conn, ev::READ, req->buffer->data(), req->buffer->size());
                req->buffer = {};
            }

            prev = worker->event_on(*conn, std::move(cb));


        });
    }
    catch (...) {
        err = std::current_exception();
    }

    if (prev) {
        worker->event_on(*conn, std::move(prev));
        worker->event_flags(*conn, pflags);
    }

    if (err) {
        std::rethrow_exception(std::move(err));
    }
}
