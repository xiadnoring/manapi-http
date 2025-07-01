#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiFetch.hpp"
#include "../ManapiJsonBuilder.hpp"
#include "../components/ManapiFileTransferInfo.hpp"
#include "../async/ManapiAsyncTimer.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT

namespace manapi::net {
    class fetch2 {
        enum fetch_data_flags {
            FETCH2_DATA_FLAG_RECEIVED = 1,
            FETCH2_DATA_FLAG_RESULT = 2,
            FETCH2_DATA_FLAG_SETUP = 4
        };
        struct fetch_data {
            manapi::net::fetch data;
            manapi::async::parallel_run<std::exception_ptr> async_run{};
            async::mutex mx{};
            int flags {0};
        };


    public:
        ~fetch2() {
            if (this->fetchdata
                && this->fetchdata.use_count() == 1
                && !(this->fetchdata->flags & FETCH2_DATA_FLAG_RESULT)) {
                /* was skipped, need to be cancelled */
                std::cout << "CANCEN\n";

                manapi::async::run(
                    fetch2::continue_receiving(std::move(this->fetchdata)),
                    +[] (std::exception_ptr err) -> void {
                        std::string msg;
                        if (err) {
                            manapi::extract_exception_ptr(err, nullptr, &msg);
                            manapi::async::current()->logger()->error(
                                manapi::logger::default_service, ERR_INTERNAL,
                                "fetch2: failed to close stalled or cancelled connection due to {}", msg);
                        }
                    });
            }
        }

        fetch2 (std::string url, async::cancellation_action cancellation = nullptr) {
            this->fetchdata = std::make_shared<fetch_data>(manapi::net::fetch{std::move(url), std::move(cancellation)});
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

        static manapi::future<fetch2> fetch (std::string url, manapi::json params = manapi::json::object(), async::cancellation_action cancellation = nullptr) {
            return fetch_(std::move(url), std::move(params), std::optional<std::string> {}, std::move(cancellation));
        }

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<curlformdata> body, async::cancellation_action cancellation = nullptr) {
            return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
        }

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::string> body, async::cancellation_action cancellation = nullptr) {
            return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
        }

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<ssize_t(char *, ssize_t)>> body, async::cancellation_action cancellation = nullptr) {
            return fetch_(std::move(url), std::move(params), std::move(body), std::move(cancellation));
        }

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<std::move_only_function<manapi::future<ssize_t>(char *, ssize_t)>> body, async::cancellation_action cancellation = nullptr) {
            fetch2 response (std::move(url), std::move(cancellation));
            if (body.has_value()) {
                response.fetchdata->data.async_body(std::move(body.value()));
            }
            response.setup_fetch(std::move(params));
            co_await response.response();
            co_return std::move(response);
        }

        static manapi::future<fetch2> fetch (std::string url, manapi::json params, std::optional<file_transfer_info> body, async::cancellation_action cancellation = nullptr) {
            fetch2 response (std::move(url), std::move(cancellation));
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
            if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP)) { THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "fetch2 must be initialized fetch2::fetch(...) only"); }
            this->fetchdata->data.handle_async_body(std::move(cb));
            this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
            co_await continue_receiving(this->fetchdata);
        }

        manapi::future<> callback_sync (std::function<ssize_t(char *buffer, ssize_t size)> cb) {
            if (!(this->fetchdata->flags & FETCH2_DATA_FLAG_SETUP)) { THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "fetch2 must be initialized fetch2::fetch(...) only"); }
            this->fetchdata->data.handle_body(std::move(cb));
            this->fetchdata->flags |= FETCH2_DATA_FLAG_RESULT;
            co_await continue_receiving(this->fetchdata);
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
            co_return builder.get().value();
        }
    private:
        template<typename T>
        static manapi::future<fetch2> fetch_ (std::string url, manapi::json params, T body, async::cancellation_action cancellation = nullptr) {
            fetch2 response (std::move(url), std::move(cancellation));
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

        static manapi::future<> continue_receiving (std::shared_ptr<fetch2::fetch_data> fetchdata) {
            fetchdata->mx.unlock();
            auto exception = co_await fetchdata->async_run.get_or(std::exception_ptr{nullptr});
            if (exception) { std::rethrow_exception(exception); }
        }

        void setup_fetch (manapi::json params) {
            try {
                auto it = params.as_object().find("method");
                if (it != params.as_object().end()) {
                    this->fetchdata->data.method(std::move(it->second.as_string()));
                }

                it = params.as_object().find("verify_peer");
                if (it != params.as_object().end()) {
                    this->fetchdata->data.enable_verify_peer(it->second.as_bool_cast());
                }

                it = params.as_object().find("verify_host");
                if (it != params.as_object().end()) {
                    this->fetchdata->data.enable_verify_host(it->second.as_bool_cast());
                }

                it = params.as_object().find("verbose");
                if (it != params.as_object().end()) {
                    this->fetchdata->data.verbose(it->second.as_bool_cast());
                }
                it = params.as_object().find("alpn");
                if (it != params.as_object().end()) {
                    this->fetchdata->data.enable_alpn(params["alpn"].as_bool());
                }

                it = params.as_object().find("timeout");
                if (it != params.as_object().end())
                    this->fetchdata->data.timeout(it->second.as_integer_cast());

                it = params.as_object().find("http");
                if (it != params.as_object().end()) {
                    auto const version = std::stold(it->second.as_string_cast());
                    if (version >= 0.9 && version <= 1.1) {
                        this->fetchdata->data.enable_http1_1();
                    }
                    else if (version == 2.0) {
                        this->fetchdata->data.enable_http2();
                    }
                    else if (version == 3.0) {
                        this->fetchdata->data.enable_http3();
                    }
                }

                it = params.as_object().find("headers");
                if (it != params.as_object().end()
                    && it->second.is_object()) {
                    this->fetchdata->data.json_headers(std::move(it->second));
                }
            }
            catch (std::exception const &e) {
                THROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FAILED_PRECONDITION, "param is invalid: {}", e.what());
            }

            this->fetchdata->flags |= FETCH2_DATA_FLAG_SETUP;

            this->fetchdata->data.handle_body([fetchdata = this->fetchdata.get()] (char *buffer, ssize_t size)
                -> ssize_t { return size; });
        }
        manapi::future<> response () {
            co_await this->fetchdata->mx.lock();
            std::exception_ptr err{nullptr};
            try {
                using promise = async::promise<void, std::false_type>;
                co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> void {
                    try {
                        this->fetchdata->data.handle_async_headers([fetchdata = this->fetchdata.get(), resolve = std::move(resolve)] (std::map<std::string, std::string> headers) mutable
                            -> manapi::future<bool> {
                            auto resolve_ = std::move(resolve);

                            fetchdata->flags |= FETCH2_DATA_FLAG_RECEIVED;
                            resolve_ ();
                            auto lk = co_await fetchdata->mx.lock_guard();
                            co_return fetchdata->flags & FETCH2_DATA_FLAG_RESULT;
                        });

                                assert((fetchdata.use_count() <= 100&&fetchdata.use_count()>=0));

                        auto p = manapi::async::invoke([reject, fetchdata = this->fetchdata.get()] ()
                            -> manapi::future<std::exception_ptr> {

                            try {
                                co_await fetchdata->data.async_doit();
                            }
                            catch (...) {
                                reject(std::current_exception());
                                co_return std::current_exception();
                            }
                            co_return nullptr;
                        });

                        this->fetchdata->async_run.run(
                            std::move(p));
                    }
                    catch (...) {
                        reject(std::current_exception());
                    }
                });
            }
            catch (...) {
                err = std::current_exception();
            }

            if (err) {
                co_await this->fetchdata->async_run.get();
                std::rethrow_exception(std::move(err));
            }
        }
        std::shared_ptr<fetch_data> fetchdata;
    };
}

#endif