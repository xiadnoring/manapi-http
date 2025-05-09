#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiFetch.hpp"
#include "../ManapiJsonBuilder.hpp"
#include "../components/ManapiFileTransferInfo.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

namespace manapi::net {
    class fetch2 {
        struct fetch_data {
            manapi::net::fetch data;
            async::shared_cthread ctx;
            manapi::async::parallel_run<std::exception_ptr> async_run;
            async::mutex mx;
            bool result {true};
            bool received {false};
            bool setup {false};
        };


    public:
        ~fetch2() {

        }

        fetch2 (async::shared_cthread ctx, std::string url) {
            this->fetchdata = std::make_shared<fetch_data>(manapi::net::fetch{ctx, std::move(url)}, ctx, ctx, ctx, true);
        }

        fetch2 (fetch2 &&n) noexcept {
            this->fetchdata = std::move(n.fetchdata);
        }

        fetch2 &operator=(fetch2 &&n) noexcept {
            this->fetchdata = std::move(n.fetchdata);
            return *this;
        }

        fetch2 (const fetch2 &n) {
            this->fetchdata = n.fetchdata;
        }

        fetch2 &operator=(const fetch2 &n) {
            this->fetchdata = n.fetchdata;
            return *this;

        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params = manapi::json::object()) {
            return fetch_(ctx, std::move(url), std::move(params), std::optional<std::string> {});
        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params, std::optional<curlformdata> body) {
            return fetch_(ctx, std::move(url), std::move(params), std::move(body));
        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params, std::optional<std::string> body) {
            return fetch_(ctx, std::move(url), std::move(params), std::move(body));
        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params, std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body) {
            return fetch_(ctx, std::move(url), std::move(params), std::move(body));
        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(char *, ssize_t)>> body) {
            fetch2 response (ctx, std::move(url));
            if (body.has_value()) {
                response.fetchdata->data.async_body(std::move(body.value()));
            }
            response.setup_fetch(std::move(params));
            co_await response.response();
            co_return std::move(response);
        }

        static manapi::future<fetch2> fetch (const async::shared_cthread &ctx, std::string url, manapi::json params, std::optional<file_transfer_info> body) {
            fetch2 response (ctx, std::move(url));
            if (body.has_value()) {
                co_await response.fetchdata->data.body(std::move(body.value()));
            }
            response.setup_fetch(std::move(params));
            co_await response.response();
            co_return std::move(response);
        }

        [[nodiscard]] bool ok () const {
            auto s = this->status();
            return s >= 200 && s <= 299;
        }

        [[nodiscard]] size_t status () const {
            return this->fetchdata->data.status_code();
        }

        std::map<std::string, std::string> headers () {
            return this->fetchdata->data.headers();
        }

        manapi::future<> callback_async (std::function<manapi::future<ssize_t>(char *buffer, ssize_t size)> cb) {
            if (!this->fetchdata->setup) { THROW_MANAPIHTTP_EXCEPTION2(ERR_BUG, "fetch2 must be initialized fetch2::fetch(...) only"); }
            this->fetchdata->data.handle_async_body(std::move(cb));
            co_await continue_receiving();
        }

        manapi::future<> callback_sync (std::function<ssize_t(char *buffer, ssize_t size)> cb) {
            if (!this->fetchdata->setup) { THROW_MANAPIHTTP_EXCEPTION2(ERR_BUG, "fetch2 must be initialized fetch2::fetch(...) only"); }
            this->fetchdata->data.handle_body(std::move(cb));
            co_await continue_receiving();
        }

        manapi::future<std::string> text () {
            std::string data;

            co_await this->callback_sync ([&data] (char *buffer, ssize_t size) -> ssize_t {
                data.append(buffer, size);
                return size;
            });

            co_return std::move(data);
        }

        manapi::future<manapi::json> json () {
            manapi::json_builder builder;
            co_await this->callback_sync([&builder] (char *buffer, ssize_t size) -> ssize_t {
                try {
                    builder << std::string_view(buffer, size);
                    return size;
                }
                catch (...) {}
                return -1;
            });
            co_return builder.get();
        }
    private:
        template<typename T>
        static manapi::future<fetch2> fetch_ (const async::shared_cthread &ctx, std::string url, manapi::json params, T body) {
            fetch2 response (ctx, std::move(url));
            if (body.has_value()) {
                response.fetchdata->data.body(std::move(body.value()));
            }
            response.setup_fetch(std::move(params));
            co_await response.response();
            co_return std::move(response);
        }

        void setup_send_body (std::string &&data) {
            this->fetchdata->data.body(std::forward<decltype(data)>(data));
        }

        manapi::future<> continue_receiving () {
            this->fetchdata->mx.unlock();
            auto exception = co_await this->fetchdata->async_run.get();
            if (exception) { std::rethrow_exception(exception); }
        }
        void setup_fetch (manapi::json params) {
            if (params.contains("method")) {
                this->fetchdata->data.method(std::move(params["method"].as_string()));
            }
            if (params.contains("ssl_verify")) {
                this->fetchdata->data.enable_ssl_verify(params["ssl_verify"].as_bool());
            }
            if (params.contains("verbose")) {
                this->fetchdata->data.verbose(params["verbose"].as_bool());
            }
            if (params.contains("alpn")) {
                this->fetchdata->data.enable_alpn(params["alpn"].as_bool());
            }
            if (params.contains("http1_1")&&params["http1_1"].as_bool()) {
                this->fetchdata->data.enable_http1_1();
            }
            if (params.contains("http2")&&params["http2"].as_bool()) {
                this->fetchdata->data.enable_http2();
            }
            if (params.contains("http3")&&params["http3"].as_bool()) {
                this->fetchdata->data.enable_http3();
            }

            if (params.contains("headers") && params["headers"].is_object()) {
                this->fetchdata->data.json_headers(std::move(params["headers"]));
            }

            this->fetchdata->setup = true;

            this->fetchdata->data.handle_body([fetchdata = this->fetchdata] (char *buffer, ssize_t size)
                -> ssize_t { return size; });
        }
        manapi::future<> response () {
            co_await this->fetchdata->mx.lock();
            co_await async::promise<void> (this->fetchdata->ctx, [this] (manapi::async::promise<void>::resolve_t resolve, manapi::async::promise<void>::reject_t reject) -> manapi::future<> {
                this->fetchdata->data.handle_async_headers([fetchdata = this->fetchdata, resolve = std::move(resolve)] (std::map<std::string, std::string> headers) -> manapi::future<bool> {
                    fetchdata->received = true;
                    resolve ();
                    auto lk = co_await fetchdata->mx.lock_guard();
                    co_return fetchdata->result;
                });

                this->fetchdata->async_run.run(manapi::async::invoke([reject, fetchdata = this->fetchdata] () -> manapi::future<std::exception_ptr> {
                    try {
                        co_await fetchdata->data.async_doit();
                    }
                    catch (...) {
                        if (!fetchdata->received) {
                            fetchdata->mx.unlock();
                            /* headers weren't received */
                            reject (std::current_exception());
                        }
                        else {
                            co_return std::current_exception();
                        }
                    }
                    co_return nullptr;
                }));

                co_return;
            });
        }
        std::shared_ptr<fetch_data> fetchdata;
    };
}

#endif