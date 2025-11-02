#include <iostream>

#include "ManapiTime.hpp"
#include "std/ManapiAsyncLogger.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "std/ManapiAsyncThreadsMutex.hpp"

const char debug_label[] = "DEBUG";
const char warning_label[] = "WARNING";
const char error_label[] = "ERROR";

const char manapi::logger::default_service[] = "manapihttp";

static std::mutex sync_mx_locker;
static std::shared_ptr<manapi::async::tmutex> async_mx_locker;

manapi::logger::logger(manapi::logger::callback_t callback) {
    this->data = std::make_shared<data_t>(std::move(callback));
    logger::setup_default_callback_(this->data);
}

manapi::logger::~logger() = default;

manapi::logger::logger(logger &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
}

manapi::logger & manapi::logger::operator=(logger &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
    return *this;
}

manapi::logger::logger(const logger &n) {
    this->data = n.data;
}

manapi::logger & manapi::logger::operator=(const logger &n) {
    this->data = n.data;
    return *this;
}

void manapi::logger::callback(callback_t callback) {
    this->data->callback = std::move(callback);
}

std::string_view manapi::logger::label_by_type(logger_type type) {
    switch (type) {
        case logger_type::LOGGER_DEBUG:
            return debug_label;
        case logger_type::LOGGER_WARNING:
            return warning_label;
        default:
            return error_label;
    }
}

void default_print_log (manapi::logger_type type, std::string_view service, int error_code, std::string msg) {
    (type == manapi::logger_type::LOGGER_ERROR ? std::cerr : std::cout)
                    << "[" << service << "][" << manapi::time::current_time(true) <<  "][" << error_code << "]: " << msg << "\n";
}

void manapi::logger::setup_default_callback_(const std::shared_ptr<data_t> &data) {
    if (manapi::async::internal::current_()) {
        std::lock_guard<std::mutex> lk (sync_mx_locker);
        if (!async_mx_locker)
            async_mx_locker = std::make_shared<async::tmutex>();

        data->callback = [] (logger_type type, std::string_view service, int error_code, std::string msg) -> void {
            manapi::async::run(manapi::async::invoke(+[](std::shared_ptr<manapi::async::tmutex> mx, manapi::logger_type type, std::string_view service, int error_code, std::string msg) -> manapi::future<> {
                auto lk = co_await mx->lock_guard();
                default_print_log(type, service, error_code, std::move(msg));
            }, async_mx_locker, type, service, error_code, std::move(msg)));
        };
    }
    else {
        data->callback = [data = std::weak_ptr(data)] (logger_type type, std::string_view service, int error_code, std::string msg) mutable -> void {
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
