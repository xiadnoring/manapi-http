#pragma once

#include <functional>
#include <memory>
#include <format>

#include "../ManapiUtils.hpp"

namespace manapi {
    namespace async {
        class cthread;
    }

    enum logger_type {
        LOGGER_DEBUG = 0,
        LOGGER_WARNING = 1,
        LOGGER_ERROR = 2
    };

    class logger {
    private:
        typedef std::move_only_function<void(logger_type type, std::string_view service, int error_code, std::string msg)> callback_t;
        struct data_t {
            callback_t callback;
        };
    public:
        logger(callback_t callback = nullptr);

        ~logger();

        logger(logger &&n) noexcept;

        logger &operator=(logger &&n) noexcept;

        logger(const logger &n);

        logger &operator=(const logger &n);

        /**
         * Set callback on message event.
         * Can't be called after async context start, otherwise it will be undefined behavior
         *
         * @param callback Callback
         *
         * @code
         * GCTX_OBJ = manapi::async::context::create();
         * GCTX_OBJ->eventloop()->setup_handle_interrupt();
         *
         * GCTX_OBJ->logger()->callback([mx = std::make_shared<manapi::async::mutex>(GCTX_OBJ)] (manapi::logger_type type, std::string_view service, int error_code, std::string msg) mutable
         *     -> void {
         *     manapi::async::run (GCTX(manapi::async::invoke(
         *         +[](std::shared_ptr<manapi::async::mutex> mx, int error_code, std::string msg) -> manapi::future<> {
         *             auto lk = co_await mx->lock_guard();
         *             std::cout << error_code << " " << msg << "\n";
         *         }, mx, error_code, std::move(msg))
         *     ));
         * });
         * ...
         * GCTX_OBJ->sync_start();
         * @endcode
         */
        void callback (callback_t callback);

        template<typename ...Args>
        void warning (std::string_view service, std::string msg, Args &&...args) {
            this->call_(LOGGER_WARNING, service, 0, std::move(msg), args...);
        }

        template<typename ...Args>
        void error (std::string_view service, int error_code, std::string msg, Args &&...args){
            this->call_(LOGGER_ERROR, service, error_code, std::move(msg), args...);
        }

        template<typename ...Args>
        void debug (std::string_view service, std::string msg, Args &&...args){
            this->call_(LOGGER_DEBUG, service, 0, std::move(msg), args...);
        }

        [[nodiscard]] static std::string_view label_by_type (logger_type type);

        static const char default_service[];
    private:
        static void setup_default_callback_(const std::shared_ptr<data_t> &data);
        template<typename ...Args>
        void call_(logger_type type, std::string_view service, int error_code, std::string msg, Args &&...args) {
            const std::size_t n = sizeof...(Args);

            this->data->callback(type, service, error_code, n ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }
        std::shared_ptr<data_t> data;
    };
}
