#include "ManapiErrors.hpp"

manapi::exception::exception(const err_num &errnum, std::string message_): message(std::move(message_)) {
    this->errnum = errnum;
}

manapi::exception::exception(const err_num &errnum, int addititonal_num_data, std::string message_) {
    this->errnum = errnum;
    this->addititonal_num_data = addititonal_num_data;
    this->message = std::move(message_);
}

const char *manapi::exception::what() const noexcept {
    return this->message.data();
}

const manapi::err_num & manapi::exception::get_err_num() const {
    return this->errnum;
}

const int & manapi::exception::get_additional_num_data() const {
    return this->addititonal_num_data;
}
