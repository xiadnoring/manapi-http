/**
 * @file worker/ManapiSite.hpp
 * @brief Provides the site structs
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <regex>
#include <chrono>

#include "../http/ManapiHttpUtils.hpp"
#include "../http/ManapiHttpConfig.hpp"
#include "../http/ManapiHttpCtx.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../json/ManapiJson.hpp"
#include "../json/ManapiJsonMask.hpp"

namespace manapi::net::worker {
    class site {
    public:
        site ();
        /**
         * deconstructor
         */
        virtual ~site();

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

        // const manapi::json &config ();

        virtual manapi::future<manapi::status> stop () = 0;
    protected:
        // std::shared_ptr<data_t> data;

    };
}