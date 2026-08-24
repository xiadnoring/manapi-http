#include "ManapiFetch2.hpp"

#include "http/ManapiHttpTypes.hpp"
#include "std/ManapiParallelRun.hpp"
#include "std/ManapiPromise.hpp"
#include "std/ManapiScopePtr.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

enum fetch2_data_flags {
    FETCH2_DATA_FLAG_RECEIVED = 1,
    FETCH2_DATA_FLAG_RESULT = 2,
    FETCH2_DATA_FLAG_SETUP = 4
};

struct manapi::net::fetch2::fetch_data {
    std::shared_ptr<manapi::net::fetch> data;
    manapi::async::parallel_run<messages> async_run;
    async::mutex mx{};
    int flags {0};
};


static manapi::status fetch2_setup_send_body(manapi::net::fetch2::fetch_data *fetchdata, std::string &&data) MANAPIHTTP_NOEXCEPT {
    return fetchdata->data->body(std::forward<decltype(data)>(data));
}

static manapi::future<manapi::status> fetch2_continue_receiving(std::shared_ptr<manapi::net::fetch2> parent, manapi::scope_ptr<manapi::net::fetch2::fetch_data> fetchdata) {
    // |parent| can be null

    fetchdata->mx.unlock();
    auto res = co_await fetchdata->async_run.get_or({});
    if (res.errnum() == manapi::ERR_OK)
        co_return manapi::status_ok();

    manapi::status b;
    b.data(std::move(res));
    co_return std::move(b);
}

static manapi::status fetch2_setup_fetch(manapi::net::fetch2::fetch_data *fetchdata, manapi::json params) MANAPIHTTP_NOEXCEPT {
    manapi::status res;
    try {
        auto it = params.as_object().find("method");
        if (it != params.as_object().end()) {
            res = fetchdata->data->method(std::move(it->second.as_string()));
            if (!res)
                goto err;
        }

        it = params.as_object().find("verify_peer");
        if (it != params.as_object().end()) {
            res = fetchdata->data->enable_verify_peer(it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }

        it = params.as_object().find("verify_host");
        if (it != params.as_object().end()) {
            res = fetchdata->data->enable_verify_host(it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }

        it = params.as_object().find("verbose");
        if (it != params.as_object().end()) {
            res = fetchdata->data->verbose(it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }
        it = params.as_object().find("alpn");
        if (it != params.as_object().end()) {
            res = fetchdata->data->enable_alpn(params["alpn"].as_bool());
            if (!res)
                goto err;
        }

        it = params.as_object().find("timeout");
        if (it != params.as_object().end()) {
            res = fetchdata->data->timeout(static_cast<std::size_t>(it->second.cast_integer().as_integer()));
            if (!res)
                goto err;
        }

        it = params.as_object().find("recv_nodelay");
        if (it != params.as_object().end()) {
            fetchdata->data->async_recv_nodelay(it->second.cast_bool().as_bool());
        }

        it = params.as_object().find("http");
        if (it != params.as_object().end()) {
            std::string_view version;
            std::string storage;
            if (it->second.is_string())
                version = it->second.as_string();
            else {
                storage = it->second.cast_string().as_string();
                version = storage;
            }
            if (version == "0.9" || version == "1.0" || version == "1" || version == "1.1") {
                res = fetchdata->data->enable_http1_1();
            }
            else if (version == "2.0" || version == "2") {
                res = fetchdata->data->enable_http2();
            }
            else if (version == "3.0" || version == "3") {
                res = fetchdata->data->enable_http3();
            }
            if (!res)
                goto err;

        }

        it = params.as_object().find("headers");
        if (it != params.as_object().end()
            && it->second.is_object()) {
            res = fetchdata->data->json_headers(std::move(it->second));
            if (!res)
                goto err;
        }


        res = fetchdata->data->handle_body(+[] (char *buffer, std::size_t size)
            -> ssize_t { return static_cast<ssize_t>(size); });

        if (!res)
            goto err;


        fetchdata->flags |= FETCH2_DATA_FLAG_SETUP;

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s",
            "setup_fetch:Failed", e.what());
        return manapi::status_internal("setup_fetch:Failed");
    }

    err: return std::move(res);
}

static manapi::future<manapi::status> fetch2_response(std::shared_ptr<manapi::net::fetch2> fetch, manapi::net::fetch2::fetch_data *data) {
    MANAPIHTTP_MUST_ALLOC_START
    co_await data->mx.lock();
    MANAPIHTTP_MUST_ALLOC_END

    manapi::status status;

    try {
        using promise = manapi::async::promise_sync<manapi::status>;
        status = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            try {
                data->data->handle_async_headers([fetchdata = data, resolve] (const std::shared_ptr<manapi::net::fetch> &) mutable
                    -> manapi::future<bool> {
                    auto resolve_ = std::move(resolve);

                    fetchdata->flags |= FETCH2_DATA_FLAG_RECEIVED;
                    resolve_ (manapi::status_ok());
                    MANAPIHTTP_MUST_ALLOC_START
                    auto lk = co_await fetchdata->mx.lock_guard();
                    MANAPIHTTP_MUST_ALLOC_END
                    co_return fetchdata->flags & FETCH2_DATA_FLAG_RESULT;
                });


                auto p = manapi::async::invoke(
                    [] (manapi::net::fetch2::fetch_data *data, promise::resolve_t resolve)
                    -> manapi::future<manapi::messages> {
                    manapi::messages msg;
                    try {
                        auto err = (co_await data->data->async_doit());
                        if (!err) {
                            msg = err.copy_data();

                            if (!(data->flags & FETCH2_DATA_FLAG_RECEIVED)) {
                                resolve (std::move(err));
                            }
                        }
                    }
                    catch (std::exception const &e) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "fetch2:response failed", e.what());
                        msg.errnum(manapi::ERR_INTERNAL);
                        msg.msg_view("fetch2:response failed");
                    }
                    co_return std::move(msg);
                }, data, std::move(resolve));

                data->async_run.run(
                    std::move(p));
            }
            catch (std::exception const &e) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "fetch2:response failed", e.what());
                resolve(manapi::status_internal("fetch2:response failed"));
            }
        });
    }
    catch (std::exception const &) {
        status = manapi::status_internal("fetch2:response failed");
    }

    if (!status) {
        MANAPIHTTP_MUST_ALLOC_START
        manapi::messages msg;
        auto res = co_await data->async_run.get();
        if (res.ok()) msg = res.unwrap();
        else msg = res.err().data();
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %.*s", "fetch2:response failed", msg.msg_view().size(), msg.msg_view().data());
        msg.errnum(manapi::ERR_ABORTED);
        co_return manapi::status (std::move(msg));
        MANAPIHTTP_MUST_ALLOC_END
    }

    co_return manapi::status_ok();
}

template<typename T>
manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2_init(std::string url, manapi::json params, T body, manapi::ctoken cancellation) {
    auto response = std::shared_ptr<manapi::net::fetch2>(new manapi::net::fetch2(std::move(url), std::move(cancellation)));
    auto status = manapi::async::parallel_run<manapi::messages>::create();
    if (!status)
        co_return status.err();

    response->fetchdata->async_run = status.unwrap();

    if (body.has_value())
        response->fetchdata->data->body(std::move(body.value()));

    auto res = fetch2_setup_fetch(response->fetchdata.get(), std::move(params));
    if (!res)
        co_return std::move(res);
    res = co_await fetch2_response(response, response->fetchdata.get());
    if (!res)
        co_return std::move(res);
    co_return std::move(response);
}

manapi::net::fetch2::~fetch2() {
    if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_RESULT)) {
        /* was skipped, need to be cancelled */
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "fetch2:active fetch (%p) was destroyed, so it will be cancelled", this->fetchdata.get());

        MANAPIHTTP_MUST_ALLOC_START
        manapi::async::run<manapi::status>(
        fetch2_continue_receiving(nullptr, scope_ptr (this->fetchdata.release(), true)),
        +[] (std::exception_ptr err, manapi::status *res) -> void {
            if (err) {
                char msg[256];
                std::size_t size = sizeof (msg);
                manapi::extract_exception_ptr(std::move(err), nullptr, msg, &size);
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s", "fetch2", "continue_receiving", size, msg);
            }

            if (res && !res->ok()) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %.*s", "fetch2", "continue_receiving",
                    res->msg().size(), res->msg().data());
            }
        });
        MANAPIHTTP_MUST_ALLOC_END
    }
}

manapi::net::fetch2::fetch2(std::string url, ctoken cancellation) {
    this->fetchdata = std::make_unique<fetch_data>(manapi::net::fetch::create (std::move(url), std::move(cancellation)).unwrap());
}

// manapi::net::fetch2::fetch2(const fetch2 &n) = default;
//
// manapi::net::fetch2 & manapi::net::fetch2::operator=(const fetch2 &n) = default;

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::optional<std::string> {}, std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<fetch_formdata> body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<std::string> body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params,std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)>> body, ctoken cancellation) {
    auto response = std::shared_ptr<fetch2>(new fetch2(std::move(url), std::move(cancellation)));
    auto status = manapi::async::parallel_run<messages>::create();
    if (!status)
        co_return status.err();

    response->fetchdata->async_run = status.unwrap();

    if (body)
        response->fetchdata->data->async_body(std::move(body.value()));

    auto res = fetch2_setup_fetch(response->fetchdata.get(), std::move(params));
    if (!res)
        co_return std::move(res);
    res = co_await fetch2_response(response, response->fetchdata.get());
    if (!res)
        co_return std::move(res);
    co_return std::move(response);
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params,std::optional<http::file_transfer_info> body, ctoken cancellation) {
    auto response = std::shared_ptr<fetch2>(new fetch2(std::move(url), std::move(cancellation)));

    auto status = manapi::async::parallel_run<messages>::create();
    if (!status)
        co_return status.err();

    response->fetchdata->async_run = status.unwrap();

    if (body.has_value()) {
        co_await response->fetchdata->data->body(std::move(body.value()));
    }
    auto res = fetch2_setup_fetch(response->fetchdata.get(),std::move(params));
    if (!res)
        co_return std::move(res);
    res = co_await fetch2_response(response, response->fetchdata.get());
    if (!res)
        co_return std::move(res);
    co_return std::move(response);
}

bool manapi::net::fetch2::ok() const MANAPIHTTP_NOEXCEPT {
    auto s = this->status();
    return s >= 200 && s <= 299;
}

uint16_t manapi::net::fetch2::status() const MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data->status_code();
}

std::map<std::string, std::string, std::less<>> &manapi::net::fetch2::headers() MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data->headers();
}

manapi::future<manapi::status> manapi::net::fetch2::callback_async(std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb) {
    if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP))
        co_return status_resource_exhausted("null");

    if (this->fetchdata->flags & FETCH2_DATA_FLAG_RESULT) {
        co_return status_unavailable("data was already received");
    }

    auto res = this->fetchdata->data->handle_async_body(std::move(cb));
    if (!res)
        co_return std::move(res);
    this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
    co_return co_await fetch2_continue_receiving(this->shared_from_this(), scope_ptr(this->fetchdata.get(), false));
}

manapi::future<manapi::status> manapi::net::fetch2::callback_sync(std::move_only_function<ssize_t(char *buffer, std::size_t size)> cb) {
    if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP))
        co_return status_resource_exhausted("null");

    if (this->fetchdata->flags & FETCH2_DATA_FLAG_RESULT) {
        co_return status_unavailable("data was already received");
    }

    auto res = this->fetchdata->data->handle_body(std::move(cb));
    if (!res)
        co_return std::move(res);
    this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
    co_return co_await fetch2_continue_receiving(this->shared_from_this(), scope_ptr(this->fetchdata.get(), false));
}

manapi::future<manapi::status_or<std::string>> manapi::net::fetch2::text() {
    try {
        std::string data;

        auto res = co_await this->callback_sync ([&data] (char *buffer, std::size_t size) MANAPIHTTP_NOEXCEPT -> ssize_t {
            try {
                data.append(buffer, (size));
                return static_cast<ssize_t>(size);
            }
            catch (std::exception const &) {
                return -1;
            }
        });

        if (!res)
            co_return std::move(res);

        co_return std::move(data);
    }
    catch (std::exception const &) {
        co_return manapi::status_resource_exhausted();
    }
}

manapi::future<manapi::json_error::status_or<manapi::json>> manapi::net::fetch2::json() {
    try {
        manapi::json_builder builder;

        auto res =co_await this->callback_sync([&builder] (char *buffer, std::size_t size) -> ssize_t {
            auto res = builder.parse(std::string_view(buffer, (size)));
            if (!res)
                return -1;
            return static_cast<ssize_t>(size);
        });

        if (!res)
            co_return json_error::status{std::move(res)};

        co_return builder.get();
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "fetch2:json failed", e.what());
        co_return json_error::status{status_internal("fetch2:json failed")};
    }
}

manapi::future<manapi::status> manapi::net::fetch2::form(formdata_recv::onparam_cb_t cb) {
    try {
        manapi::net::formdata_recv fd_recv ([this] (formdata_recv::req_data_cb_t cb)
            -> manapi::future<manapi::status> {
            return this->callback_async(std::move(cb));
        });

        auto hit = this->headers().find(http::H_CONTENT_TYPE);
        co_return co_await fd_recv.get( hit == this->headers().end() ? std::string_view{} : hit->second,
             std::move(cb) );
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "fetch2:json failed", e.what());
        co_return json_error::status{status_internal("fetch2:json failed")};
    }
}

#endif
