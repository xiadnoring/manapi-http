#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#if defined(__unix__)||defined(__APPLE__)
#   include <arpa/inet.h>
#   include <netdb.h>
#endif
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include "../include/ManapiUtils.hpp"
#include "../../include/http/ManapiHttpPool.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "../include/http/ManapiHttp1.hpp"

#include "../include/http/ManapiHttp1Interface.hpp"

manapi::net::http_pool::http_pool(const json &config, std::shared_ptr<multithread_storage::worker_t> worker_config, class http::site site, size_t id, std::shared_ptr<event_loop> events) : site(std::move(site)) {
    this->events = std::move(events);
    this->config = std::make_shared <http::config> (config);
    this->id = id;
    this->worker_config = std::move(worker_config);
    this->mx = std::make_shared<async::mutex>();

    this->config->function_contains_compressor([this] (const std::string &name) -> bool {
        return this->site.contains_compressor_for_file(name)
            || this->site.contains_compressor_for_string(name);
    });
}

manapi::net::http_pool::~http_pool() = default;

manapi::future<manapi::error::status> manapi::net::http_pool::stop() {
    try {
        auto lk = co_await this->mx->lock_guard();
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "shutdown socket");
        if (this->worker) {
            using promise = manapi::async::promise<void, std::false_type>;
            co_await promise ([this] (promise::resolve_t resolve, promise::reject_t reject) -> void {
                try {
                    this->worker->stop(std::move(resolve));
                }
                catch (...) {
                    reject(std::current_exception());
                }
            });
        }
        co_return error::status_ok();
    }
    catch (std::bad_alloc const &) {
        co_return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stop() failed", e.what());
    }
    co_return error::status_internal("stop() failed");
}

manapi::future<manapi::error::status> manapi::net::http_pool::run() {
    co_return co_await this->pool_();
}

template<typename T>
std::string concat_keys_in_map (const std::map<std::string, T> &m) {
    std::string available;
    if (!m.empty()) {
        auto it = m.begin();
        goto skip;
        for (; it != m.end(); ++it) {
            available += ',';
            skip:
            available += it->first;
        }
    }
    return std::move(available);
}

manapi::future<manapi::error::status> manapi::net::http_pool::pool_() {
    try {
        manapi_log_trace("http: pool init №%zu", this->id);

        auto lk = co_await this->mx->lock_guard();

        manapi_log_trace("http: pool start №%zu", this->id);

        auto implementation = this->config->implementation;
        auto transport = this->config->transport;
        auto implementations = this->site.transport_protocol_worker(transport);

        http::site::implemenet_http_cb const *implement_http_callback{nullptr};

        if (implementations.contains(implementation))
        {
            try {
                auto &generate = implementations[implementation];
                this->worker = generate (this->site, this->worker_config, this->config);
                auto const workerptr = dynamic_cast<worker::interface_worker *> (this->worker.get());


                worker::wrk_interface_global_t wrk{};
                auto res = worker::default_wrk_http_all_global_init(&wrk, workerptr);

                if (!res.ok()) {
                    res.log();
                    res.unwrap();
                }

                workerptr->wrk_global(&wrk);
                auto wrkptr = workerptr->wrk_global();

                if (implementation == "quiche") {
                    if (!this->config->contains_http_version(http::versions::HTTP_v0_9)
                        && !this->config->contains_http_version(http::versions::HTTP_v1_0)
                        && !this->config->contains_http_version(http::versions::HTTP_v1_1)
                        && !this->config->contains_http_version(http::versions::HTTP_v2)
                        && this->config->contains_http_version(http::versions::HTTP_v3)
                        && this->config->http3_implementation == "quiche") {

                    }
                    else {
                        co_return manapi::error::status_internal("http: QUIC(quiche) must be only working with HTTP3(quiche)");
                    }
                }
                else {
                    std::vector<int> hlist = {
                        http::versions::HTTP_v0_9,
                        http::versions::HTTP_v1_0,
                        http::versions::HTTP_v1_1,
                        http::versions::HTTP_v2,
                        http::versions::HTTP_v3
                    };

                    for (auto version : hlist) {
                        if (version == http::versions::HTTP_v1_1
                            || this->config->contains_http_version(version)) {
                            if (version >= http::versions::HTTP_v0_9 && version < http::versions::HTTP_v1_1)
                                version = http::versions::HTTP_v1_1;

                            auto &http_implementation = this->site.http_protocol_worker(static_cast<http::versions::http>(version));
                            std::string *http_impl_name{nullptr};
                            switch (version) {
                                case http::versions::HTTP_v1_1: http_impl_name = &this->config->http1_implementation; break;
                                case http::versions::HTTP_v2: http_impl_name = &this->config->http2_implementation; break;
                                case http::versions::HTTP_v3: http_impl_name = &this->config->http3_implementation; break;
                            }

                            assert(http_impl_name);

                            auto it_http_impl = http_implementation.find(*http_impl_name);
                            if (it_http_impl == http_implementation.end()) {
                                MANAPIHTTP_LOG("http: implementation by {} not found. Available: [{}]",*http_impl_name, concat_keys_in_map(http_implementation));
                                co_return error::status_failed_precondition("http implementation not found");
                            }

                            implement_http_callback = &it_http_impl->second;
                            auto httpwrk = it_http_impl->second (workerptr);
                            if (!httpwrk.ok())
                                httpwrk.unwrap();

                            res = worker::default_wrk_http_all_global_add_version(wrkptr, version, std::move(httpwrk.unwrap()));
                            if (!res.ok())
                                res.unwrap();
                        }
                    }
                }

                workerptr->worker_pool_id(this->id);
                res = co_await workerptr->init(0);

                if (!res.ok()) {
                    res.log();
                    res.unwrap();
                }
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("http: worker init failed due to {}", e.what());
            }
        }
        else
        {
            auto keys = concat_keys_in_map(implementations);
            manapi_log_error("http: implementation by %.*s not found in %.*s. Available: [%.*s]", implementation.size(),
                implementation.data(), transport.size(), transport.data(), keys.size(), keys.data());
            co_return error::status_failed_precondition("implementation not found");
        }
        co_return error::status_ok();
    }
    catch (std::bad_alloc const &) {
        co_return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http: pool() failed", e.what());
    }
    co_return error::status_internal("pool() failed");
}

const manapi::net::http::site & manapi::net::http_pool::get_site() const {
    return this->site;
}
