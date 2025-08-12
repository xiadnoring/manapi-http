#include "services/ManapiFetch2.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

enum fetch2_data_flags {
    FETCH2_DATA_FLAG_RECEIVED = 1,
    FETCH2_DATA_FLAG_RESULT = 2,
    FETCH2_DATA_FLAG_SETUP = 4
};

struct manapi::net::fetch2::fetch_data {
    manapi::net::fetch data;
    manapi::async::parallel_run<std::string_view> async_run;
    async::mutex mx{};
    int flags {0};
};

template<typename T>
manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch_(std::string url, manapi::json params, T body, async::cancellation_action cancellation) {
    fetch2 response;
    auto status = manapi::async::parallel_run<std::string_view>::create();
    if (!status)
        co_return status.err();

    response.fetchdata->async_run = status.unwrap();
    auto err = response.fetchdata->data.init(std::move(url), std::move(cancellation));
    if (!err)
        co_return std::move(err);

    if (body.has_value())
        response.fetchdata->data.body(std::move(body.value()));

    auto res = response.setup_fetch(std::move(params));
    if (!res)
        co_return std::move(res);
    co_await response.response();
    co_return std::move(response);
}

manapi::net::fetch2::~fetch2() {
    if (this->fetchdata
        && this->fetchdata.use_count() == 1
        && !(this->fetchdata->flags & FETCH2_DATA_FLAG_RESULT)) {
        /* was skipped, need to be cancelled */
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "fetch2:active fetch (%p) was destroyed, so it will be cancelled", this->fetchdata.get());

        MANAPIHTTP_MUST_ALLOC_START
        manapi::async::run<manapi::error::status>(
        fetch2::continue_receiving((this->fetchdata)),
        +[] (std::exception_ptr err, manapi::error::status *res) -> void {
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

manapi::net::fetch2::fetch2() {
    this->fetchdata = std::make_shared<fetch_data>(manapi::net::fetch{});
}

manapi::net::fetch2::fetch2(const fetch2 &n) = default;

manapi::net::fetch2 & manapi::net::fetch2::operator=(const fetch2 &n) = default;

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params, async::cancellation_action cancellation) {
    return fetch_(std::move(url), std::move(params), std::optional<std::string> {}, std::move(cancellation));
}

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<fetch_formdata> body, async::cancellation_action cancellation) {
    return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<std::string> body, async::cancellation_action cancellation) {
    return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params,std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, async::cancellation_action cancellation) {
    return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)>> body, async::cancellation_action cancellation) {
    fetch2 response;
    auto status = manapi::async::parallel_run<std::string_view>::create();
    if (!status)
        co_return status.err();

    response.fetchdata->async_run = status.unwrap();
    auto err = response.fetchdata->data.init(std::move(url), std::move(cancellation));
    if (!err)
        co_return std::move(err);

    if (body)
        response.fetchdata->data.async_body(std::move(body.value()));

    auto res = response.setup_fetch(std::move(params));
    if (!res)
        co_return std::move(res);
    co_await response.response();
    co_return std::move(response);
}

manapi::future<manapi::error::status_or<manapi::net::fetch2>> manapi::net::fetch2::fetch(std::string url, manapi::json params,std::optional<file_transfer_info> body, async::cancellation_action cancellation) {
    fetch2 response;

    auto status = manapi::async::parallel_run<std::string_view>::create();
    if (!status)
        co_return status.err();

    response.fetchdata->async_run = status.unwrap();
    auto err = response.fetchdata->data.init(std::move(url), std::move(cancellation));
    if (!err)
        co_return std::move(err);

    if (body.has_value()) {
        co_await response.fetchdata->data.body(std::move(body.value()));
    }
    auto res = response.setup_fetch(std::move(params));
    if (!res)
        co_return std::move(res);
    co_await response.response();
    co_return std::move(response);
}

bool manapi::net::fetch2::ok() const MANAPIHTTP_NOEXCEPT {
    auto s = this->status();
    return s >= 200 && s <= 299;
}

size_t manapi::net::fetch2::status() const MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data.status_code();
}

std::map<std::string, std::string, std::less<>> manapi::net::fetch2::headers() MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data.headers();
}

manapi::future<manapi::error::status> manapi::net::fetch2::callback_async(std::function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb) {
    if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP))
        co_return error::status_resource_exhausted("null");

    auto res = this->fetchdata->data.handle_async_body(std::move(cb));
    if (!res)
        co_return std::move(res);
    this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
    co_return co_await continue_receiving(this->fetchdata);
}

manapi::future<manapi::error::status> manapi::net::fetch2::callback_sync(std::function<ssize_t(char *buffer, ssize_t size)> cb) {
    if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP))
        co_return error::status_resource_exhausted("null");
    auto res = this->fetchdata->data.handle_body(std::move(cb));
    if (!res)
        co_return std::move(res);
    this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
    co_return co_await continue_receiving(this->fetchdata);
}

manapi::future<manapi::error::status_or<std::string>> manapi::net::fetch2::text() {
    try {
        std::string data;

        auto res = co_await this->callback_sync ([&data] (char *buffer, ssize_t size) MANAPIHTTP_NOEXCEPT -> ssize_t {
            try {
                data.append(buffer, size);
                return size;
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
        co_return manapi::error::status_resource_exhausted();
    }
}

manapi::future<manapi::json_error::status_or<manapi::json>> manapi::net::fetch2::json() {
    try {
        manapi::json_builder builder;

        auto res =co_await this->callback_sync([&builder] (char *buffer, ssize_t size) -> ssize_t {
            auto res = builder.parse(std::string_view(buffer, size));
            if (!res)
                return -1;
            return size;
        });

        if (!res)
            co_return json_error::status{std::move(res)};

        co_return builder.get();
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "fetch2:json failed", e.what());
        co_return json_error::status{error::status_internal("fetch2:json failed")};
    }
}

manapi::error::status manapi::net::fetch2::setup_send_body(std::string &&data) MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data.body(std::forward<decltype(data)>(data));
}

manapi::future<manapi::error::status> manapi::net::fetch2::continue_receiving(std::shared_ptr<fetch2::fetch_data> fetchdata) {
    fetchdata->mx.unlock();
    auto res = co_await fetchdata->async_run.get_or({});
    if (res.empty())
        co_return error::status_ok();

    co_return manapi::error::status_internal(res);
}

manapi::error::status manapi::net::fetch2::setup_fetch(manapi::json params) MANAPIHTTP_NOEXCEPT {
    manapi::error::status res;
    try {
        auto it = params.as_object().find("method");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.method(std::move(it->second.as_string()));
            if (!res)
                goto err;
        }

        it = params.as_object().find("verify_peer");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.enable_verify_peer(it->second.as_bool_cast());
            if (!res)
                goto err;
        }

        it = params.as_object().find("verify_host");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.enable_verify_host(it->second.as_bool_cast());
            if (!res)
                goto err;
        }

        it = params.as_object().find("verbose");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.verbose(it->second.as_bool_cast());
            if (!res)
                goto err;
        }
        it = params.as_object().find("alpn");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.enable_alpn(params["alpn"].as_bool());
            if (!res)
                goto err;
        }

        it = params.as_object().find("timeout");
        if (it != params.as_object().end()) {
            res = this->fetchdata->data.timeout(it->second.as_integer_cast());
            if (!res)
                goto err;
        }
        it = params.as_object().find("http");
        if (it != params.as_object().end()) {
            std::string_view version;
            std::string storage;
            if (it->second.is_string())
                version = it->second.as_string();
            else {
                storage = it->second.as_string_cast();
                version = storage;
            }
            if (version == "0.9" || version == "1.0" || version == "1" || version == "1.1") {
                res = this->fetchdata->data.enable_http1_1();
            }
            else if (version == "2.0" || version == "2") {
                res = this->fetchdata->data.enable_http2();
            }
            else if (version == "3.0" || version == "3") {
                res = this->fetchdata->data.enable_http3();
            }
            if (!res)
                goto err;

        }

        it = params.as_object().find("headers");
        if (it != params.as_object().end()
            && it->second.is_object()) {
            res = this->fetchdata->data.json_headers(std::move(it->second));
            if (!res)
                goto err;
        }


        res = this->fetchdata->data.handle_body(+[] (char *buffer, ssize_t size)
            -> ssize_t { return size; });

        if (!res)
            goto err;


        this->fetchdata->flags |= FETCH2_DATA_FLAG_SETUP;

        return manapi::error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s",
            "setup_fetch:Failed", e.what());
        return manapi::error::status_internal("setup_fetch:Failed");
    }

    err: return std::move(res);
}

manapi::future<> manapi::net::fetch2::response() {
    MANAPIHTTP_MUST_ALLOC_START
    co_await this->fetchdata->mx.lock();
    MANAPIHTTP_MUST_ALLOC_END

    manapi::error::status status;

    try {
        using promise = async::promise_sync<manapi::error::status>;
        status = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            try {
                this->fetchdata->data.handle_async_headers([fetchdata = this->fetchdata.get(), resolve] (std::map<std::string, std::string, std::less<>> headers) mutable
                    -> manapi::future<bool> {
                    auto resolve_ = std::move(resolve);

                    fetchdata->flags |= FETCH2_DATA_FLAG_RECEIVED;
                    resolve_ (manapi::error::status_ok());
                    MANAPIHTTP_MUST_ALLOC_START
                    auto lk = co_await fetchdata->mx.lock_guard();
                    MANAPIHTTP_MUST_ALLOC_END
                    co_return fetchdata->flags & FETCH2_DATA_FLAG_RESULT;
                });


                auto p = manapi::async::invoke(
                    [] (std::shared_ptr<fetch_data> fetchdata, promise::resolve_t resolve)
                    -> manapi::future<std::string_view> {
                    try {
                        std::string_view msg;
                        auto err = (co_await fetchdata->data.async_doit());
                        if (!err) {
                            msg = err.msg();
                            if (!(fetchdata->flags & FETCH2_DATA_FLAG_RECEIVED)) {
                                resolve (std::move(err));
                            }
                        }
                        co_return msg;
                    }
                    catch (std::exception const &e) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "fetch2:response failed", e.what());
                        co_return {"fetch2:response failed"};
                    }
                }, this->fetchdata, std::move(resolve));

                this->fetchdata->async_run.run(
                    std::move(p));
            }
            catch (std::exception const &e) {
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s", "fetch2:response failed", e.what());
                resolve(manapi::error::status_internal("fetch2:response failed"));
            }
        });
    }
    catch (std::exception const &) {
        status = manapi::error::status_internal("fetch2:response failed");
    }

    if (!status) {
        MANAPIHTTP_MUST_ALLOC_START
        std::string_view msg;
        auto res = co_await this->fetchdata->async_run.get();
        if (!res.ok()) {
            msg = res.message();
        }
        else {
            msg = res.unwrap();
        }
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %.*s", "fetch2:response failed", msg.size(), msg.data());
        MANAPIHTTP_MUST_ALLOC_END
    }
}


#endif
