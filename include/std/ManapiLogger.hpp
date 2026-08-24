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
        typedef std::move_only_function<void(logger_type type, std::string_view service, int error_code, std::string msg)> callback_t;
    public:
        struct data_t;

        logger();

        logger(callback_t callback);

        logger(std::string service, callback_t callback);

        logger(std::string service, std::shared_ptr<data_t> data);

        ~logger();

        logger(logger &&n) MANAPIHTTP_NOEXCEPT;

        logger &operator=(logger &&n) MANAPIHTTP_NOEXCEPT;

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

        void callback (logger_type type, std::string_view service, int error_code, std::string msg) MANAPIHTTP_NOEXCEPT;

        void fcallback (logger_type type, std::string_view service, int error_code, const char *fmt, va_list args) MANAPIHTTP_NOEXCEPT;

        template<typename ...Args>
        void swarning (std::string_view service, std::string msg, Args &&...args) {
            this->callback(LOGGER_WARNING, service, 0, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        template<typename ...Args>
        void serror (std::string_view service, int error_code, std::string msg, Args &&...args){
            this->callback(LOGGER_ERROR, service, error_code, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        template<typename ...Args>
        void sdebug (std::string_view service, std::string msg, Args &&...args){
            this->callback(LOGGER_DEBUG, service, 0, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        template<typename ...Args>
        void warning (std::string msg, Args &&...args) {
            this->callback(LOGGER_WARNING, this->m_service, 0, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        template<typename ...Args>
        void error (int error_code, std::string msg, Args &&...args){
            this->callback(LOGGER_ERROR, this->m_service, error_code, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        template<typename ...Args>
        void debug (std::string msg, Args &&...args){
            this->callback(LOGGER_DEBUG, this->m_service, 0, (sizeof...(Args)) ? std::vformat(msg, std::make_format_args(args...)) : std::move(msg));
        }

        std::shared_ptr<manapi::logger> create (std::string service);

        void fsdebug (std::string_view service, const char *fmt, ...) MANAPIHTTP_NOEXCEPT;

        void fserror (std::string_view service, int error_code, const char *fmt, ...) MANAPIHTTP_NOEXCEPT;

        void fswarning (std::string_view service, const char *fmt, ...) MANAPIHTTP_NOEXCEPT;

        void fdebug (const char *fmt, ...) MANAPIHTTP_NOEXCEPT;

        void ferror (int error_code, const char *fmt, ...) MANAPIHTTP_NOEXCEPT;

        void fwarning (const char *fmt, ...) MANAPIHTTP_NOEXCEPT;
    private:
        std::string m_service;

        std::shared_ptr<data_t> m_data;
    };
}
