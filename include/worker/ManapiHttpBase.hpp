/**
 * @file worker/ManapiSite.hpp
 * @brief Provides the site structs
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <regex>
#include <chrono>
#include <atomic>

#include "../http/ManapiHttpUtils.hpp"
#include "../http/ManapiHttpConfig.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../json/ManapiJson.hpp"
#include "../json/ManapiJsonMask.hpp"
#include "../std/ManapiStopToken.hpp"

namespace manapi::net::worker {
    struct pool_worker_t {
        void *data;
        std::size_t ref;
    };

    struct pool_t {
        std::vector<pool_worker_t> data;
        std::unique_ptr<std::mutex> mx;
    };

    struct worker_data_t {
        std::atomic<ssize_t> count;
        std::vector<pool_t> pools;
    };

    class base_http {
    public:
        base_http ();
        /**
         * deconstructor
         */
        virtual ~base_http();

        /**
         * set a config file path
         * @param path file path
         * @return a future
         */
        virtual manapi::future<manapi::status> config (std::string path) = 0;

        /**
         * set a config JSON object
         * @param config config JSON object
         * @return a future
         */
        virtual manapi::future<manapi::status> config_object (json config) = 0;

        virtual manapi::future<manapi::status> stop () = 0;

        virtual void send_stop ( manapi::stoken token ) = 0;
    };
}