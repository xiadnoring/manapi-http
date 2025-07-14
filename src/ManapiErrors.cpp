#include "ManapiErrors.hpp"

#include "async/ManapiAsyncContext.hpp"
#include "ManapiDebug.hpp"
#include "ManapiJson.hpp"
#include "include/ManapiUtils.hpp"
#include <stdarg.h>
#include <time.h>
#include <string.h>

std::string_view manapi::get_msg_by_err_num (manapi::err_num err) {
    switch (err) {
        case manapi::ERR_OK: return "ERR_OK";
        case manapi::ERR_ABORTED: return "ERR_ABORTED";
        case manapi::ERR_UNKNOWN: return "ERR_UNKNOWN";
        case manapi::ERR_DATA_LOSS: return "ERR_DATA_LOSS";
        case manapi::ERR_INTERNAL: return "ERR_INTERNAL";
        case manapi::ERR_NOT_FOUND: return "ERR_NOT_FOUND";
        case manapi::ERR_CANCELLED: return "ERR_CANCELLED";
        case manapi::ERR_OUT_OF_RANGE: return "ERR_OUT_OF_RANGE";
        case manapi::ERR_UNAVAILABLE: return "ERR_UNAVAILABLE";
        case manapi::ERR_ALREADY_EXISTS: return "ERR_ALREADY_EXISTS";
        case manapi::ERR_UNIMPLEMENTED: return "ERR_UNIMPLEMENTED";
        case manapi::ERR_INVALID_ARGUMENT: return "ERR_INVALID_ARGUMENT";
        case manapi::ERR_UNAUTHENTICATED: return "ERR_UNAUTHENTICATED";
        case manapi::ERR_DEADLINE_EXCEEDED: return "ERR_DEADLINE_EXCEEDED";
        case manapi::ERR_PERMISSION_DENIED: return "ERR_PERMISSION_DENIED";
        case manapi::ERR_RESOURCE_EXHAUSTED: return "ERR_RESOURCE_EXHAUSTED";
        case manapi::ERR_FAILED_PRECONDITION: return "ERR_FAILED_PRECONDITION";
        case manapi::ERR_FILESYSTEM_FAILED: return "ERR_FILESYSTEM_FAILED";
    }

    return "ERR_UNKNOWN";
}

void manapi::extract_exception_ptr(std::exception_ptr err, int *errnum, std::string *msg) {
    try {
        std::rethrow_exception(std::move(err));
    }
    catch (manapi::exception &e) {
        if (errnum)
            *errnum = e.err_num();

        if (msg)
            *msg = e.what();
    }
    catch (std::exception const &e) {
        if (errnum)
            *errnum = ERR_UNKNOWN;

        if (msg)
            *msg = e.what();
    }
}

manapi::exception::exception(manapi::err_num errnum, std::string message): message(std::move(message)) {
    this->errnum_ = errnum;
}


const char *manapi::exception::what() const noexcept {
    return this->message.data();
}

int manapi::exception::err_num() const {
    return this->errnum_;
}

manapi::error::status::status() {
    this->code_ = manapi::ERR_OK;
}

manapi::error::status::status(err_num code, std::string_view msg) {
    this->code_ = code;
    this->msg_ = msg;
}

manapi::error::status::status(err_num code, std::string_view msg, manapi::json data) {
    this->code_ = code;
    this->msg_ = msg;
    this->data_ = std::move(data);
}

manapi::error::status::status(status &&n) noexcept = default;

manapi::error::status & manapi::error::status::operator=(status &&n) noexcept = default;

std::string_view manapi::error::status::msg() const {
    return this->msg_;
}

manapi::err_num manapi::error::status::code() const {
    return this->code_;
}

manapi::json &manapi::error::status::data() {
    return this->data_;
}

bool manapi::error::status::ok() const {
    return this->code_ == manapi::ERR_OK;
}

void manapi::error::status::log() const {
    MANAPIHTTP_LOG ("{}: msg: {}, data: {}", this->status_msg(), this->msg_, this->data_.dump());
}

std::string_view manapi::error::status::status_msg() const {
    return get_msg_by_err_num(this->code_);
}

void manapi::error::status::unwrap() const {
    if (this->code_ != ERR_OK)
        THROW_MANAPIHTTP_EXCEPTION(this->code_, "msg: {}, data: {}", this->msg_, this->data_.dump());
}

manapi::error::status manapi::error::status_ok() {
    return {};
}

manapi::error::status manapi::error::status_unknown(std::string_view msg) {
    return {ERR_UNKNOWN, msg};
}

manapi::error::status manapi::error::status_cancelled() {
    return {ERR_CANCELLED, {"cancelled"}};
}

manapi::error::status manapi::error::status_cancelled(std::string_view msg) {
    return {ERR_CANCELLED, msg};
}

manapi::error::status manapi::error::status_invalid_argument(std::string_view msg) {
    return {ERR_INVALID_ARGUMENT, msg};
}

manapi::error::status manapi::error::status_deadline_exceeded(std::string_view msg) {
    return {ERR_DEADLINE_EXCEEDED, msg};
}

manapi::error::status manapi::error::status_not_found(std::string_view msg) {
    return {ERR_NOT_FOUND, msg};
}

manapi::error::status manapi::error::status_already_exists(std::string_view msg) {
    return {ERR_ALREADY_EXISTS, msg};
}

manapi::error::status manapi::error::status_permission_denied(std::string_view msg) {
    return {ERR_PERMISSION_DENIED, msg};
}

manapi::error::status manapi::error::status_unauthenticated(std::string_view msg) {
    return {ERR_UNAUTHENTICATED, msg};
}

manapi::error::status manapi::error::status_resource_exhausted() {
    return {ERR_RESOURCE_EXHAUSTED, "bad alloc"};
}

manapi::error::status manapi::error::status_resource_exhausted(std::string_view msg) {
    return {ERR_RESOURCE_EXHAUSTED, msg};
}

manapi::error::status manapi::error::status_failed_precondition(std::string_view msg) {
    return {ERR_FAILED_PRECONDITION, msg};
}

manapi::error::status manapi::error::status_aborted(std::string_view msg) {
    return {ERR_ABORTED, msg};
}

manapi::error::status manapi::error::status_unavailable(std::string_view msg) {
    return {ERR_UNAVAILABLE, msg};
}

manapi::error::status manapi::error::status_out_of_range(std::string_view msg) {
    return {ERR_OUT_OF_RANGE, msg};
}

manapi::error::status manapi::error::status_unimplemented(std::string_view msg) {
    return {ERR_UNIMPLEMENTED, msg};
}

manapi::error::status manapi::error::status_internal(std::string_view msg) {
    return {ERR_INTERNAL, msg};
}

manapi::error::status manapi::error::status_internal() {
    return error::status_internal("failed");
}

manapi::error::status manapi::error::status_data_loss(std::string_view msg) {
    return {ERR_DATA_LOSS, msg};
}

manapi::error::status manapi::error::status_filesystem_failed(std::string_view msg) {
    return {ERR_FILESYSTEM_FAILED, msg};
}

manapi::error::status manapi::error::status_parse_failed(std::string_view msg) {
    return {ERR_PARSE_FAILED, msg};
}

manapi::error::status manapi::error::status_ok(manapi::json data) {
    return {ERR_OK, "ok", std::move(data)};
}

manapi::error::status manapi::error::status_unknown(std::string_view msg, manapi::json data) {
    return {ERR_UNKNOWN, msg, std::move(data)};
}

manapi::error::status manapi::error::status_cancelled(std::string_view msg, manapi::json data) {
    return {ERR_CANCELLED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_invalid_argument(std::string_view msg, manapi::json data) {
    return {ERR_INVALID_ARGUMENT, msg, std::move(data)};
}

manapi::error::status manapi::error::status_deadline_exceeded(std::string_view msg, manapi::json data) {
    return {ERR_DEADLINE_EXCEEDED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_not_found(std::string_view msg, manapi::json data) {
    return {ERR_NOT_FOUND, msg, std::move(data)};
}

manapi::error::status manapi::error::status_already_exists(std::string_view msg, manapi::json data) {
    return {ERR_ALREADY_EXISTS, msg, std::move(data)};
}

manapi::error::status manapi::error::status_permission_denied(std::string_view msg, manapi::json data) {
    return {ERR_PERMISSION_DENIED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_unauthenticated(std::string_view msg, manapi::json data) {
    return {ERR_UNAUTHENTICATED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_resource_exhausted(std::string_view msg, manapi::json data) {
    return {ERR_RESOURCE_EXHAUSTED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_failed_precondition(std::string_view msg, manapi::json data) {
    return {ERR_FAILED_PRECONDITION, msg, std::move(data)};
}

manapi::error::status manapi::error::status_aborted(std::string_view msg, manapi::json data) {
    return {ERR_ABORTED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_unavailable(std::string_view msg, manapi::json data) {
    return {ERR_UNAVAILABLE, msg, std::move(data)};
}

manapi::error::status manapi::error::status_out_of_range(std::string_view msg, manapi::json data) {
    return {ERR_OUT_OF_RANGE, msg, std::move(data)};
}

manapi::error::status manapi::error::status_unimplemented(std::string_view msg, manapi::json data) {
    return {ERR_UNIMPLEMENTED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_internal(std::string_view msg, manapi::json data) {
    return {ERR_INTERNAL, msg, std::move(data)};
}

manapi::error::status manapi::error::status_data_loss(std::string_view msg, manapi::json data) {
    return {ERR_DATA_LOSS, msg, std::move(data)};
}

manapi::error::status manapi::error::status_filesystem_failed(std::string_view msg, manapi::json data) {
    return {ERR_FILESYSTEM_FAILED, msg, std::move(data)};
}

manapi::error::status manapi::error::status_parse_failed(std::string_view msg, manapi::json data) {
    return {ERR_PARSE_FAILED, msg, std::move(data)};
}

void manapi::debug::log_log(log_level level, const char *file, int line, const char *fmt, ...) {

    // Remove path from filename
    const char* base = strrchr(file, '/');
    if (!base) base = strrchr(file, '\\');
    base = base ? base + 1 : file;

    // Print timestamp, log level, and file info
    fprintf(
        stderr,
        "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
        level_colors[level],
        level_colors[level],
        level_strings[level],
        base,
        line
    );

    // Print user message
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    // Newline and flush
    fprintf(stderr, "\n");
    fflush(stderr);

}
