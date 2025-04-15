#include "async/ManapiAsyncLogger.hpp"
#include "async/ManapiAsyncContext.hpp"

const char debug_label[] = "DEBUG";
const char warning_label[] = "WARNING";
const char error_label[] = "ERROR";

const char manapi::logger::default_service[] = "\001manapihttp";

manapi::logger::logger(manapi::logger::callback_t callback) {
    this->data = std::make_shared<data_t>(std::move(callback));
    if (!this->data->callback) { this->setup_default_callback_(); }
}

manapi::logger::~logger() = default;

manapi::logger::logger(logger &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::logger & manapi::logger::operator=(logger &&n) noexcept {
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

void manapi::logger::setup_default_callback_() {
    this->data->callback = [] (logger_type type, std::string_view service, int error_code, std::string msg) -> void {
        //std::cout << std::format("[{:%H:%M:%S}][{}][{}][{}]: {}\n", time::current_time(true), logger::label_by_type(type), service, error_code, std::move(msg)) << "\n";
    };
}
