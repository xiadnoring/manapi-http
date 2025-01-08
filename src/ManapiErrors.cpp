#include "ManapiErrors.hpp"

manapi::exception::exception(const err_num &errnum, std::string message_): message(std::move(message_)) {
    this->errnum = errnum;
}

const char *manapi::exception::what() const noexcept {
    return this->message.data();
}

const manapi::err_num & manapi::exception::get_err_num() const {
    return this->errnum;
}