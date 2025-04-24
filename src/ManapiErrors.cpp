#include "ManapiErrors.hpp"

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

namespace manapi::error {
    const char *default_msgs[] = {
        "file not found",
        "file exists",
        "size isn't the same",
        "by error: {}",
        "error when receiving additional data",
        "failure of fs i/o operations: {}",
        "failure of fs callback: {}",
        "fs i/o operation has been cancelled",
        "fs i/o init watcher failure",
        "watcher command failed"
    };
}

manapi::exception::exception(const manapi::err_num &errnum, std::string message_): message(std::move(message_)) {
    this->errnum_ = errnum;
}

manapi::exception::exception(const manapi::err_num &errnum, int addititonal_num_data, std::string message_) {
    this->errnum_ = errnum;
    this->addititonal_num_data = addititonal_num_data;
    this->message = std::move(message_);
}

const char *manapi::exception::what() const noexcept {
    return this->message.data();
}

const manapi::err_num & manapi::exception::err_num() const {
    return this->errnum_;
}

const int & manapi::exception::get_additional_num_data() const {
    return this->addititonal_num_data;
}
