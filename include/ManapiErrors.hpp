/**
 * @file ManapiErrors.hpp
 * @brief Provides utilities to work with errors
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <memory>
#include <format>

#include "ManapiUtils.hpp"

namespace manapi {
    /**
     * base status codes
     */
    enum err_num {
        /**
         * Not an error;
         * returned on success.
         */
        ERR_OK = 0,
        /**
         * 	Unknown error. For example, this error may be returned when a Status
         * 	value received from another address space belongs to an error space
         * 	that is not known in this address space. Also errors raised by APIs
         * 	that do not return enough error information may be converted to this error.
         */
        ERR_UNKNOWN,
        /**
         * The client specified an invalid argument.
         * Note that this differs from FAILED_PRECONDITION.
         * INVALID_ARGUMENT indicates arguments that are problematic
         * regardless of the state of the system (e.g., a malformed file name).
         */
        ERR_INVALID_ARGUMENT,
        /**
         * Some requested entity (e.g., file or directory) was not found.
         * Note to server developers: if a request is denied for an entire class of users,
         * such as gradual feature rollout or undocumented allowlist,
         * NOT_FOUND may be used. If a request is denied for some users within
         * a class of users, such as user-based access control, PERMISSION_DENIED must be used.
         */
        ERR_NOT_FOUND,
        /**
         * The operation was cancelled, typically by the caller.
         */
        ERR_CANCELLED,
        /**
         * The deadline expired before the operation could complete.
         * For operations that change the state of the system,
         * this error may be returned even if the operation has completed successfully.
         * For example, a successful response from a server could
         * have been delayed long enough for the deadline to expire.
         */
        ERR_DEADLINE_EXCEEDED,
        /**
         * The entity that a client attempted to
         * create (e.g., file or directory) already exists.
         */
        ERR_ALREADY_EXISTS,
        /**
         * The caller does not have permission to execute the specified operation.
         * PERMISSION_DENIED must not be used for rejections caused by exhausting
         * some resource (use RESOURCE_EXHAUSTED instead for those errors).
         * PERMISSION_DENIED must not be used if the caller can not be
         * identified (use UNAUTHENTICATED instead for those errors).
         * This error code does not imply the request is valid or the
         * requested entity exists or satisfies other pre-conditions.
         */
        ERR_PERMISSION_DENIED,
        /**
         * The request does not have valid authentication credentials for the operation.
         */
        ERR_UNAUTHENTICATED,
        /**
         * Some resource has been exhausted, perhaps a per-user quota, or perhaps the entire file system is out of space.
         */
        ERR_RESOURCE_EXHAUSTED,
        /**
         * The operation was rejected because the system
         * is not in a state required for the operation’s execution.
         * For example, the directory to be deleted is non-empty,
         * an rmdir operation is applied to a non-directory, etc.
         */
        ERR_FAILED_PRECONDITION,
        /**
         * The operation was aborted, typically due to a concurrency
         * issue such as a sequencer check failure or transaction abort.
         * See the guidelines above for deciding
         * between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE.
         */
        ERR_ABORTED,
        /**
         * The service is currently unavailable.
         * This is most likely a transient condition, which can be corrected
         * by retrying with a backoff.
         * Note that it is not always safe to retry non-idempotent operations.
         */
        ERR_UNAVAILABLE,
        /**
         * The operation was attempted past the valid range.
         */
        ERR_OUT_OF_RANGE,
        /**
         * The operation is not implemented or is not supported/enabled in this service.
         */
        ERR_UNIMPLEMENTED,
        /**
         * Internal errors.
         * This means that some invariants expected by the underlying
         * system have been broken.
         * This error code is reserved for serious errors.
         */
        ERR_INTERNAL,
        /**
         * Unrecoverable data loss or corruption.
         */
        ERR_DATA_LOSS
    };

    /**
     * Get an error message as a string
     *
     * @param errnum the error code
     * @return
     */
    std::string_view get_msg_by_err_num(err_num errnum);

    /**
     * Extract data from the std::exception_ptr
     *
     * @param err the exception pointer
     * @param errnum the output error code
     * @param msg the output error message
     */
    void extract_exception_ptr (std::exception_ptr err, int *errnum, std::string *msg);

    /**
     * manapi exception
     */
    class exception : public std::exception {
    public:
        exception (manapi::err_num errnum, std::string message);

        [[nodiscard]] const char * what() const noexcept override;

        /**
         * Get the error code
         *
         * @return the error code
         */
        [[nodiscard]] int err_num () const;
    private:
        manapi::err_num errnum_;
        std::string message;
    };

    namespace error {
        class status {
        public:
            status ();

            virtual ~status ();

            status (err_num code, std::string_view msg);

            status (status &&n) noexcept;

            status& operator= (status &&n) noexcept;

            /**
             * Get the error message from the status
             *
             * @return the error message
             */
            [[nodiscard]] std::string_view msg () const;

            /**
             * Get the error code from the status
             *
             * @return the error code
             */
            [[nodiscard]] err_num code () const;

            /**
             * Is there no error
             *
             * @return true if there's no error
             */
            [[nodiscard]] bool ok () const;

            /**
             * do log using the status
             */
            virtual void log () const;

            /**
             * get the error code as a string
             *
             * @return the error code as a string
             */
            [[nodiscard]] std::string_view status_msg () const;

            /**
             * If there is error it throws an exception
             *
             * @throws manapi::exception with the error code from the status
             */
            virtual void unwrap () const;
        protected:
            std::string_view msg_;
            err_num code_;
        };

        template<typename T, typename E = manapi::error::status>
        requires(!std::is_same_v<E, T>)
        class status_or {
        public:
            status_or (T value) : err_() {
                this->value_ = std::move(value);
            }

            status_or (E st) {
                this->err_ = std::move(st);
            }

            status_or(status_or &&n) MANAPIHTTP_NOEXPECT = default;

            status_or&operator=(status_or &&n) MANAPIHTTP_NOEXPECT = default;

            /**
             * get the error code from the status
             *
             * @return the error code from the status
             */
            [[nodiscard]] manapi::err_num code () const {
                return this->err_.code();
            }

            /**
             * get the error code as a string from the status
             *
             * @return the error code as a string
             */
            [[nodiscard]] std::string_view status_msg () const {
                return this->err_.status_msg();
            }

            /**
             * get the error message from the status
             *
             * @return the error message
             */
            [[nodiscard]] std::string_view message () const {
                return this->err_.msg();
            }

            /**
             * get the result if it exists
             *
             * @return the result if it exists, but otherwise, it throws
             * the exception
             * @throws manapi::exception with error code from the status
             */
            T unwrap () {
                this->err_.unwrap();
                return std::move(this->value_.value());
            }

            /**
             * is there no error
             *
             * @return true if there's no error, otherwise it returns false
             */
            [[nodiscard]] bool ok () const {
                return this->err_.code() == manapi::ERR_OK;
            }

            /**
             * get the error status
             *
             * @return the error status
             */
            E err () {
                return std::move(this->err_);
            }
        protected:
            std::optional<T> value_;
            E err_;
        };

        status status_ok ();
        status status_unknown (std::string_view msg);
        status status_cancelled ();
        status status_cancelled (std::string_view msg);
        status status_invalid_argument (std::string_view msg);
        status status_deadline_exceeded (std::string_view msg);
        status status_not_found (std::string_view msg);
        status status_already_exists (std::string_view msg);
        status status_permission_denied (std::string_view msg);
        status status_unauthenticated (std::string_view msg);
        status status_resource_exhausted ();
        status status_resource_exhausted (std::string_view msg);
        status status_failed_precondition (std::string_view msg);
        status status_aborted (std::string_view msg);
        status status_unavailable (std::string_view msg);
        status status_out_of_range (std::string_view msg);
        status status_unimplemented (std::string_view msg);
        status status_internal (std::string_view msg);
        status status_internal ();
        status status_data_loss (std::string_view msg);
    }
}

template <>
struct std::formatter<manapi::err_num> : std::formatter<int> {};