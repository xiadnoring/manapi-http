#pragma once

#include <map>
#include <string>
#include <memory>
#include <format>

#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"

namespace manapi {
    enum err_num {
        ERR_OK = 0,
        ERR_UNKNOWN,
        ERR_INVALID_ARGUMENT,
        ERR_NOT_FOUND,
        ERR_CANCELLED,
        ERR_DEADLINE_EXCEEDED,
        ERR_ALREADY_EXISTS,
        ERR_PERMISSION_DENIED,
        ERR_UNAUTHENTICATED,
        ERR_RESOURCE_EXHAUSTED,
        ERR_FAILED_PRECONDITION,
        ERR_ABORTED,
        ERR_UNAVAILABLE,
        ERR_OUT_OF_RANGE,
        ERR_UNIMPLEMENTED,
        ERR_INTERNAL,
        ERR_DATA_LOSS,
        ERR_FILESYSTEM_FAILED,
        ERR_PARSE_FAILED
    };

    std::string_view get_msg_by_err_num(err_num errnum);

    void extract_exception_ptr (std::exception_ptr err, int *errnum, std::string *msg);

    class exception : public std::exception {
    public:
        exception (manapi::err_num errnum, std::string message);
        [[nodiscard]] const char * what() const noexcept override;
        [[nodiscard]] int err_num () const;
    private:
        manapi::err_num errnum_;
        std::string message;
    };

    namespace error {
        class status {
        public:
            status ();
            status (err_num code, std::string_view msg);
            status (err_num code, std::string_view msg, manapi::json data);

            status (status &&n) noexcept;
            status& operator= (status &&n) noexcept;

            [[nodiscard]] std::string_view msg () const;
            [[nodiscard]] err_num code () const;
            [[nodiscard]] manapi::json &data ();
            [[nodiscard]] bool ok () const;
            [[nodiscard]] std::string_view status_msg () const;
            void unwrap () const;
        private:
            manapi::json data_;
            std::string_view msg_;
            err_num code_;
        };

        template<typename T>
        requires(!std::is_same_v<status, T>)
        class status_or {
        public:
            status_or (T value) : err_() {
                this->value_ = std::move(value);
            }

            status_or (status status) {
                this->err_ = std::move(status);
            }

            [[nodiscard]] manapi::err_num code () const {
                return this->err_.code();
            }

            [[nodiscard]] std::string_view status_msg () const {
                return this->err_.status_msg();
            }

            [[nodiscard]] std::string_view message () const {
                return this->err_.msg();
            }

            T value () {
                this->unwrap();
                return std::move(this->value_.value());
            }

            [[nodiscard]] bool ok () const {
                return this->err_.code() == manapi::ERR_OK;
            }

            error::status err () {
                return std::move(this->err_);
            }

            void unwrap () const {
                this->err_.unwrap();
            }
        private:
            std::optional<T> value_;
            error::status err_;
        };

        status status_ok ();
        status status_unknown (std::string_view msg);
        status status_cancelled (std::string_view msg);
        status status_invalid_argument (std::string_view msg);
        status status_deadline_exceeded (std::string_view msg);
        status status_not_found (std::string_view msg);
        status status_already_exists (std::string_view msg);
        status status_permission_denied (std::string_view msg);
        status status_unauthenticated (std::string_view msg);
        status status_resource_exhausted (std::string_view msg);
        status status_failed_precondition (std::string_view msg);
        status status_aborted (std::string_view msg);
        status status_unavailable (std::string_view msg);
        status status_out_of_range (std::string_view msg);
        status status_unimplemented (std::string_view msg);
        status status_internal (std::string_view msg);
        status status_data_loss (std::string_view msg);
        status status_filesystem_failed (std::string_view msg);
        status status_parse_failed (std::string_view msg);

        status status_ok (manapi::json data);
        status status_unknown (std::string_view msg, manapi::json data);
        status status_cancelled (std::string_view msg, manapi::json data);
        status status_invalid_argument (std::string_view msg, manapi::json data);
        status status_deadline_exceeded (std::string_view msg, manapi::json data);
        status status_not_found (std::string_view msg, manapi::json data);
        status status_already_exists (std::string_view msg, manapi::json data);
        status status_permission_denied (std::string_view msg, manapi::json data);
        status status_unauthenticated (std::string_view msg, manapi::json data);
        status status_resource_exhausted (std::string_view msg, manapi::json data);
        status status_failed_precondition (std::string_view msg, manapi::json data);
        status status_aborted (std::string_view msg, manapi::json data);
        status status_unavailable (std::string_view msg, manapi::json data);
        status status_out_of_range (std::string_view msg, manapi::json data);
        status status_unimplemented (std::string_view msg, manapi::json data);
        status status_internal (std::string_view msg, manapi::json data);
        status status_data_loss (std::string_view msg, manapi::json data);
        status status_filesystem_failed (std::string_view msg, manapi::json data);
        status status_parse_failed (std::string_view msg, manapi::json data);
    }
}

template <>
struct std::formatter<manapi::err_num> : std::formatter<int> {};