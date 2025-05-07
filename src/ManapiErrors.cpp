#include "ManapiErrors.hpp"
#include "ManapiJson.hpp"

const std::map <manapi::err_num, std::string> manapi::err_msg {
    {ERR_OK, "OK"},
    {ERR_FATAL, "Fatal"},
    {ERR_UNDEFINED, "Undefined"},
    {ERR_DEBUG, "Debug"},
    {ERR_ALGORITHM_NO_SUPPORT, "Algotihtm not supported"},
    {ERR_ALGORITHM_INIT_FAIL, "Failed to init an algorithm"},
    {ERR_QUIC_PROTOCOL_ERROR, "QUIC Protocol Error"},
    {ERR_BUG, "BUG"},
    {ERR_SUBSCRIBE_FAILURE, "Failed to subscribe"},
    {ERR_INTERRUPTED, "App was interrupted"},
    {ERR_THREAD_SAFE, "That method isn't thread safe"},
    {ERR_POSTGRE_ERROR, "PostgreSQL error"},
    {ERR_CONNECTION_TIMEOUT, "Connection Timeout"},
    {ERR_PARSE_ERROR, "Parse: Error"},
    {ERR_PARSE_INVALID_CHAR, "Parse: invalid char"},
    {ERR_PARSE_INVALID_SYMBOL, "Parse: invalid symbol"},
    {ERR_PARSE_UNEXPECTED_END, "Parse: Unexpected end"},
    {ERR_SOCKET, "Socket Error"},
    {ERR_FILE_DESCRIPTOR, "FD error"},
    {ERR_INCOMPATIBLE_SETTING, "Incompatible setting"}
};

void manapi::rethrow_exception_ptr(std::exception_ptr err, int *errnum, std::string *msg, json *data) {
    try {
        std::rethrow_exception(std::move(err));
    }
    catch (manapi::exception &e) {
        if (errnum)
            *errnum = e.err_num();

        if (msg)
            *msg = e.what();

        if (data && e.data())
            *data = std::move(*e.data());
    }
    catch (std::exception const &e) {
        if (errnum)
            *errnum = ERR_UNHANDLED_EXCEPTION;

        if (msg)
            *msg = e.what();

        if (data)
            *data = nullptr;
    }
}

manapi::exception::exception(manapi::err_num errnum, std::string message): message(std::move(message)) {
    this->errnum_ = errnum;
    this->data_ = nullptr;
}

manapi::exception::exception(manapi::err_num errnum, std::string message, std::unique_ptr<manapi::json> data) {
    this->errnum_ = errnum;
    this->message = std::move(message);
    this->data_ = std::move(data);
}

const char *manapi::exception::what() const noexcept {
    return this->message.data();
}

int manapi::exception::err_num() const {
    return this->errnum_;
}

std::shared_ptr<manapi::json> manapi::exception::data() {
    return this->data_;
}

