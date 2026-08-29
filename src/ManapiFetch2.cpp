#include "ManapiFetch2.hpp"

#include "http/ManapiHttpTypes.hpp"
#include "std/ManapiParallelRun.hpp"
#include "std/ManapiPromise.hpp"
#include "std/ManapiScopePtr.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

struct manapi::net::fetch2::fetch_data {
    std::shared_ptr<manapi::net::fetch> data;
    manapi::async::promise_sync<manapi::status>::resolve_t resolve;
};

static manapi::status manapi__fetch2_setup_fetch(manapi::net::fetch2::fetch_data *fetchdata, manapi::json params) MANAPIHTTP_NOEXCEPT {
    manapi::status res;
    try {
        auto &params_obj = params.as_object();
        
        auto it = params_obj.find("method");
        if (it != params_obj.end()) {
            res = fetchdata->data->method(std::move(it->second.as_string()));
            if (!res)
                goto err;
        }

        it = params_obj.find("verify_peer");
        if (it != params_obj.end()) {
            res = fetchdata->data->option( manapi::net::fetch::OPTION_VERIFY_PEER, it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }

        it = params_obj.find("verify_host");
        if (it != params_obj.end()) {
            res = fetchdata->data->option( manapi::net::fetch::OPTION_VERIFY_HOST,it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }

        it = params_obj.find("verbose");
        if (it != params_obj.end()) {
            res = fetchdata->data->option( manapi::net::fetch::OPTION_VERBOSE,it->second.cast_bool().as_bool());
            if (!res)
                goto err;
        }
        it = params_obj.find("alpn");
        if (it != params_obj.end()) {
            res = fetchdata->data->option( manapi::net::fetch::OPTION_ALPN,params["alpn"].as_bool());
            if (!res)
                goto err;
        }

        it = params_obj.find("timeout");
        if (it != params_obj.end()) {
            res = fetchdata->data->option( manapi::net::fetch::OPTION_TIMEOUT, static_cast<int32_t>(it->second.cast_integer().as_integer()));
            if (!res)
                goto err;
        }

        it = params_obj.find("http");
        if (it != params_obj.end()) {
            std::string_view version;
            std::string storage;
            if (it->second.is_string())
                version = it->second.as_string();
            else {
                storage = it->second.cast_string().as_string();
                version = storage;
            }
            if (version == "0.9" || version == "1.0" || version == "1" || version == "1.1") {
                res = fetchdata->data->option( manapi::net::fetch::OPTION_HTTP1_1, 1);
            }
            else if (version == "2.0" || version == "2") {
                res = fetchdata->data->option( manapi::net::fetch::OPTION_HTTP2, 1);
            }
            else if (version == "3.0" || version == "3") {
                res = fetchdata->data->option( manapi::net::fetch::OPTION_HTTP3, 1);
            }
            if (!res)
                goto err;

        }

        it = params_obj.find("headers");
        if (it != params_obj.end()
            && it->second.is_object()) {

            for (const auto & b : it->second.entries()) {
                res = fetchdata->data->send_header( b.first, b.second.as_string() );
                if (!res)
                    goto err;
            }
        }


        res = fetchdata->data->recv_body(+[] (char *buffer, std::size_t size)
            -> ssize_t { return static_cast<ssize_t>(size); });

        if (!res)
            goto err;

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %s",
            "setup_fetch:Failed", e.what());
        return manapi::status_internal("setup_fetch:Failed");
    }

    err: return std::move(res);
}

manapi::future<manapi::status> manapi::net::fetch2_response(std::shared_ptr<manapi::net::fetch2> fetch, manapi::ctoken token) {

    try {
        using promise = manapi::async::promise_sync<manapi::status>;

        co_await promise ([ &fetch, &token ] ( promise::resolve_t resolve ) -> void {

            fetch->fetchdata->resolve = std::move(resolve);

            fetch->fetchdata->data->recv_async_headers ([ data = fetch->fetchdata.get()]
                    (const std::shared_ptr<manapi::net::fetch> &) mutable
                -> manapi::future<bool> {

                auto st = co_await promise ([ data ] ( promise::resolve_t resolve )
                    -> void {
                    if (data->resolve) std::exchange(data->resolve, std::move(resolve)) ( manapi::status_ok() );
                    else resolve ( manapi::status_unavailable() );
                });

                co_return st.ok();
            }).unwrap();

            manapi::async::run <manapi::status> ( fetch->fetchdata->data->perform ( std::move(token) ),
                    [fetch] ( std::exception_ptr err, manapi::status *st ) -> void {
                if (!fetch->fetchdata->resolve) {
                    return;
                }

                if (err) std::exchange(fetch->fetchdata->resolve, {}) ( manapi::status_unknown("fetch2:failed") );
                else std::exchange(fetch->fetchdata->resolve, {}) ( std::move (*st) );

            });
        });

        co_return manapi::status_ok();
    }
    catch (std::exception const &) {
        if (fetch->fetchdata->resolve) {
            std::exchange(fetch->fetchdata->resolve, {})(
                    manapi::status_unknown("fetch2:failed"));
        }

        co_return manapi::status_internal("fetch2:response failed");
    }
}

template<typename T>
manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2_init(std::string url, manapi::json params, T body, manapi::ctoken cancellation) {
    auto response = std::shared_ptr<manapi::net::fetch2>(new manapi::net::fetch2(std::move(url)));

    manapi::status res;

    if (!( res = response->fetchdata->data->send_body(std::move(body)) )) {
        co_return std::move(res);
    }

    if (!( res = manapi__fetch2_setup_fetch(response->fetchdata.get(), std::move(params)) ))
        co_return std::move(res);

    if (!( res = co_await manapi::net::fetch2_response(response, std::move(cancellation)) ))
        co_return std::move(res);

    co_return std::move(response);
}

manapi::net::fetch2::~fetch2() {

}

manapi::net::fetch2::fetch2(std::string url) {
    this->fetchdata = std::make_unique<fetch_data>(manapi::net::fetch::create (std::move(url)).unwrap());
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, ctoken cancellation) {
    auto response = std::shared_ptr<manapi::net::fetch2>(new manapi::net::fetch2(std::move(url)));

    manapi::status res;
    if (! ( res = manapi__fetch2_setup_fetch(response->fetchdata.get(), std::move(params)) ))
        co_return std::move(res);

    if (! ( res = co_await manapi::net::fetch2_response(response, std::move(cancellation)) ))
        co_return std::move(res);

    co_return std::move(response);
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, fetch_formdata body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::string body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, manapi::slice_view body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params,std::move_only_function<ssize_t(char *, ssize_t)> body, ctoken cancellation) {
    return fetch2_init(std::move(url), std::move(params), std::move(body), std::move(cancellation));
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params, std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool &fin)> body, ctoken cancellation) {
    auto response = std::shared_ptr<fetch2>(new fetch2(std::move(url)));

    manapi::status res;
    if (! ( res = response->fetchdata->data->send_async_body(std::move(body)) ))
        co_return std::move(res);

    if (! ( res = manapi__fetch2_setup_fetch(response->fetchdata.get(), std::move(params)) ))
        co_return std::move(res);

    if (! ( res = co_await manapi::net::fetch2_response(response, std::move(cancellation)) ))
        co_return std::move(res);

    co_return std::move(response);
}

manapi::future<manapi::status_or<std::shared_ptr<manapi::net::fetch2>>> manapi::net::fetch2::fetch(std::string url, manapi::json params,http::file_transfer_info body, ctoken cancellation) {
    auto response = std::shared_ptr<manapi::net::fetch2>(new manapi::net::fetch2(std::move(url)));

    manapi::status res;

    if (! ( res = co_await response->fetchdata->data->send_body(std::move(body)) )) {
        co_return std::move(res);
    }

    if (! ( res = manapi__fetch2_setup_fetch(response->fetchdata.get(), std::move(params)) ))
        co_return std::move(res);

    if (! ( res = co_await manapi::net::fetch2_response(response, std::move(cancellation)) ))
        co_return std::move(res);

    co_return std::move(response);
}

bool manapi::net::fetch2::ok() const MANAPIHTTP_NOEXCEPT {
    auto const s = this->status();
    return s >= 200 && s <= 299;
}

uint16_t manapi::net::fetch2::status() const MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data->status_code();
}

std::map<std::string, std::string, std::less<>> &manapi::net::fetch2::headers() MANAPIHTTP_NOEXCEPT {
    return this->fetchdata->data->headers();
}

manapi::future<manapi::status> manapi::net::fetch2::callback_async(std::move_only_function<manapi::future<ssize_t>(slice_view buffs, bool fin)> cb) {
    if (!this->is_processing()) co_return manapi::status_invalid_argument("fetch2:not active");
    auto st = this->fetchdata->data->recv_async_body (std::move(cb));
    if (!st) co_return std::move(st);
    using promise = manapi::async::promise_sync <manapi::status>;
    co_return co_await promise ([this] (promise::resolve_t resolve)
            -> void {
        if (this->fetchdata->resolve) std::exchange(this->fetchdata->resolve, std::move(resolve)) ( manapi::status_ok() );
        else resolve ( manapi::status_invalid_argument("fetch2:not active") );
    });
}

manapi::future<manapi::status> manapi::net::fetch2::callback_sync(std::move_only_function<ssize_t(char *buffer, std::size_t size)> cb) {
    if (!this->is_processing()) co_return manapi::status_invalid_argument("fetch2:not active");
    auto st = this->fetchdata->data->recv_body (std::move(cb));
    if (!st) co_return std::move(st);
    using promise = manapi::async::promise_sync <manapi::status>;
    co_return co_await promise ([this] (promise::resolve_t resolve)
            -> void {
        if (this->fetchdata->resolve) std::exchange(this->fetchdata->resolve, std::move(resolve)) ( manapi::status_ok() );
        else resolve ( manapi::status_invalid_argument("fetch2:not active") );
    });
}

manapi::future<manapi::status_or<std::string>> manapi::net::fetch2::text() {
    try {
        std::string data;

        auto res = co_await this->callback_sync ([&data] (char *buffer, std::size_t size) -> ssize_t {
            data.append(buffer, (size));
            return static_cast<ssize_t>(size);
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

        auto res = co_await this->callback_sync([&builder] (char *buffer, std::size_t size) -> ssize_t {
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

manapi::future<manapi::status_or<manapi::slice>> manapi::net::fetch2::slice() {
    try {
        manapi::slice data;

        auto res = co_await this->callback_sync ([&data] (char *buffer, std::size_t size) -> ssize_t {
            try {
                data.push_back (buffer, (size)).unwrap();
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

bool manapi::net::fetch2::is_processing() const {
    return !!this->fetchdata->resolve;
}

#endif
