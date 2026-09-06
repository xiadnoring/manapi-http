#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>

#include "http/ManapiHttpPool.hpp"
#include "worker/ManapiHttpBase.hpp"
#include "ManapiHttp.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/http/ManapiHttp1.hpp"
#include "../include/http/ManapiHttp1Interface.hpp"

template<typename T>
std::string manapi__concat_keys_in_map (const std::unordered_map<std::string, T, manapi::text_hash, std::equal_to<>> &m) {
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

manapi::net::http_pool::http_pool(const manapi::json &config, size_t id)  {
    this->m_id = id;
    this->m_config = std::make_shared <http::config> (config);
}

manapi::net::http_pool::~http_pool() = default;

void manapi::net::http_pool::send_stop(manapi::stoken token) {
    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "shutdown net::http_pool");
    if (this->m_worker) {
        this->m_worker->stop(token);
    }
}

manapi::future<manapi::status> manapi::net::http_pool::run(std::shared_ptr<worker::base_http> site, manapi::net::worker::worker_data_t *wdata) {
    try {
        manapi_log_trace("http_pool:init №%zu", this->m_id);

        auto implementation = this->m_config->implementation;
        auto transport = this->m_config->transport;
        auto implementations = manapi::net::http::server::cast(site.get())->transport_protocol_worker(transport);

        if (implementations.contains(implementation)) {
            this->m_worker = implementations[implementation] (site, wdata, this->m_config.get());
            auto const workerptr = dynamic_cast<worker::interface_worker *> (this->m_worker.get());

            worker::wrk_interface_global_t wrk{};
            worker::default_wrk_http_all_global_init(&wrk, workerptr).unwrap();

            workerptr->wrk_global(&wrk);
            auto wrkptr = workerptr->wrk_global();

            if (implementation == "quiche") {
                if (!this->m_config->contains_http_version(http::versions::HTTP_v0_9)
                    && !this->m_config->contains_http_version(http::versions::HTTP_v1_0)
                    && !this->m_config->contains_http_version(http::versions::HTTP_v1_1)
                    && !this->m_config->contains_http_version(http::versions::HTTP_v2)
                    && this->m_config->contains_http_version(http::versions::HTTP_v3)
                    && this->m_config->http3_implementation == "quiche") {

                }
                else {
                    co_return manapi::status_internal("http: QUIC(quiche) must be only working with HTTP3(quiche)");
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
                        || this->m_config->contains_http_version(version)) {
                        if (version >= http::versions::HTTP_v0_9 && version < http::versions::HTTP_v1_1)
                            version = http::versions::HTTP_v1_1;

                        auto &http_implementation = manapi::net::http::server::cast(site.get())
                            ->http_protocol_worker(static_cast<http::versions::http>(version));
                        std::string *http_impl_name{nullptr};
                        switch (version) {
                            case http::versions::HTTP_v1_1: http_impl_name = &this->m_config->http1_implementation; break;
                            case http::versions::HTTP_v2: http_impl_name = &this->m_config->http2_implementation; break;
                            case http::versions::HTTP_v3: http_impl_name = &this->m_config->http3_implementation; break;
                        }

                        assert(http_impl_name);

                        auto it_http_impl = http_implementation.find(*http_impl_name);
                        if (it_http_impl == http_implementation.end()) {
                            manapi_log_error("http:implementation by %s not found. Available: [%s]", http_impl_name->data(), manapi__concat_keys_in_map(http_implementation).data());
                            co_return status_failed_precondition("http implementation not found");
                        }

                        worker::default_wrk_http_all_global_add_version(wrkptr, version, (
                                it_http_impl->second (workerptr).unwrap())).unwrap();
                    }
                }
            }

            workerptr->worker_pool_id(this->m_id);
            manapi::unwrap(co_await workerptr->init(0));
        }
        else {
            auto keys = manapi__concat_keys_in_map(implementations);
            manapi_log_error("http_pool: implementation by %.*s not found in %.*s. Available: [%.*s]", implementation.size(),
                implementation.data(), transport.size(), transport.data(), keys.size(), keys.data());
            co_return status_failed_precondition("http_pool:implementation not found");
        }
        co_return status_ok();
    }
    catch (std::bad_alloc const &) {
        co_return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http_pool:failed", e.what());
    }
    co_return status_unknown("http_pool:failed");
}

const std::shared_ptr <manapi::net::http::config> &manapi::net::http_pool::config() const {
    return this->m_config;
}
