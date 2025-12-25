#include <iostream>
#include <cstdarg>
#include <ctime>
#include <cstring>

#include "ManapiTime.hpp"
#include "std/ManapiAsyncLogger.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "std/ManapiAsyncThreadsMutex.hpp"

const char debug_label[] = "DEBUG";
const char warning_label[] = "WARNING";
const char error_label[] = "ERROR";



static const char default_service[] = "manapihttp";

static std::mutex sync_mx_locker;

static std::shared_ptr<manapi::async::tmutex> async_mx_locker;

struct manapi::logger::data_t {
    callback_t callback;
};

static void setup_default_callback_(const std::shared_ptr<manapi::logger::data_t> &data);

manapi::logger::logger() : logger(nullptr) {

}

manapi::logger::logger(manapi::logger::callback_t callback) : logger(default_service, std::move(callback)) {
}

manapi::logger::logger(std::string service, callback_t callback) {
    this->m_service = std::move(service);
    this->m_data = std::make_shared<data_t>(std::move(callback));

    setup_default_callback_(this->m_data);
}

manapi::logger::logger(std::string service, std::shared_ptr<data_t> data) {
    this->m_service = std::move(service);
    this->m_data = std::move(data);
}

manapi::logger::~logger() = default;

manapi::logger::logger(logger &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
}

manapi::logger & manapi::logger::operator=(logger &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        this->m_data = std::move(n.m_data);
    }
    return *this;
}

manapi::logger::logger(const logger &n) {
    this->m_data = n.m_data;
}

manapi::logger & manapi::logger::operator=(const logger &n) {
    this->m_data = n.m_data;
    return *this;
}

void manapi::logger::callback(callback_t callback) {
    this->m_data->callback = std::move(callback);
}

void manapi::logger::callback(logger_type type, std::string_view service, int error_code, std::string msg) MANAPIHTTP_NOEXCEPT {
    try {
        if (this->m_data)
            this->m_data->callback (type, service, error_code, std::move(msg));
    }
    catch (std::exception const &e) {
        manapi_log_ferror(e.what());
    }
}

void manapi::logger::fcallback(logger_type type, std::string_view service, int error_code, const char *fmt, va_list args) MANAPIHTTP_NOEXCEPT {
    try {
        if (this->m_data) {
            va_list args_copy;
            va_copy(args_copy, args);
            std::size_t size = vsnprintf(nullptr, 0, fmt, args_copy);
            std::string buffer;
            va_end(args_copy);

            buffer.resize(size);
            vsnprintf(buffer.data(), buffer.size() + 1, fmt, args);

            this->callback(type, service, error_code, std::move(buffer));
        }
    }
    catch (std::exception const &e) {
        manapi_log_ferror(e.what());
    }
}

std::shared_ptr<manapi::logger> manapi::logger::create(std::string service) {
    return std::make_shared<logger>(std::move(service), this->m_data);
}

void manapi::logger::fsdebug(std::string_view service, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_DEBUG, service, 0, fmt, args);
    va_end(args);
}

void manapi::logger::fserror(std::string_view service, int error_code, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_ERROR, service, error_code, fmt, args);
    va_end(args);
}

void manapi::logger::fswarning(std::string_view service, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_WARNING, service, 0, fmt, args);
    va_end(args);
}

void manapi::logger::fdebug(const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_DEBUG, this->m_service, 0, fmt, args);
    va_end(args);

}

void manapi::logger::ferror(const char *fmt, int error_code, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_ERROR, this->m_service, error_code, fmt, args);
    va_end(args);
}

void manapi::logger::fwarning(const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    fcallback (logger_type::LOGGER_WARNING, this->m_service, 0, fmt, args);
    va_end(args);
}

static std::string_view label_by_type(manapi::logger_type type) {
    switch (type) {
        case manapi::logger_type::LOGGER_DEBUG:
            return debug_label;
        case manapi::logger_type::LOGGER_WARNING:
            return warning_label;
        default:
            return error_label;
    }
}

void default_print_log (manapi::logger_type type, std::string_view service, int error_code, std::string msg) {
    (type == manapi::logger_type::LOGGER_ERROR ? std::cerr : std::cout)
                    << "[" << service << "][" << manapi::time::current_time(true) <<  "][" << error_code << "]: " << msg << "\n";
}

static void setup_default_callback_(const std::shared_ptr<manapi::logger::data_t> &data) {
    if (manapi::async::internal::current_()) {
        std::lock_guard<std::mutex> lk (sync_mx_locker);
        if (!async_mx_locker)
            async_mx_locker = std::make_shared<manapi::async::tmutex>();

        data->callback = [] (manapi::logger_type type, std::string_view service, int error_code, std::string msg) -> void {
            manapi::async::run(manapi::async::invoke(+[](std::shared_ptr<manapi::async::tmutex> mx, manapi::logger_type type, std::string_view service, int error_code, std::string msg) -> manapi::future<> {
                auto lk = co_await mx->lock_guard();
                default_print_log(type, service, error_code, std::move(msg));
            }, async_mx_locker, type, service, error_code, std::move(msg)));
        };
    }
    else {
        data->callback = [data = std::weak_ptr(data)] (manapi::logger_type type, std::string_view service, int error_code, std::string msg) mutable -> void {
            auto tmp = data.lock();
            if (tmp && manapi::async::internal::current_()) {
                setup_default_callback_(tmp);
                tmp->callback(type, service, error_code, msg);
            }
            else {
                std::lock_guard <std::mutex> lk (sync_mx_locker);
                default_print_log(type, service, error_code, std::move(msg));
            }
        };
    }
}
