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
#include <optional>

#include "./ManapiUtils.hpp"

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
     * @param err exception pointer
     * @param errnum output error code
     * @param msg output error message
     */
    void extract_exception_ptr (std::exception_ptr err, int *errnum, std::string *msg);
    /**
     * Extract data from the std::exception_ptr
     *
     * @param err exception pointer
     * @param errnum output error code
     * @param msg output error message
     * @param msg_size output error message size
     */
    void extract_exception_ptr (std::exception_ptr err, int *errnum, char *msg, std::size_t *msg_size);

    union messages_storage {
        std::string_view m_view{};
        std::string m_str;

        ~messages_storage();
    };

    class messages {
    public:
        messages();

        ~messages();

        messages (messages &&n) MANAPIHTTP_NOEXCEPT;

        messages& operator= (messages &&n) MANAPIHTTP_NOEXCEPT;

        messages (const messages &n);

        messages& operator= (const messages &n);

        void errnum (manapi::err_num code) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD manapi::err_num errnum () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::string_view msg_view () const MANAPIHTTP_NOEXCEPT;

        std::string msg () MANAPIHTTP_NOEXCEPT;

        void msg_view (std::string_view msg) MANAPIHTTP_NOEXCEPT;

        void msg (std::string msg) MANAPIHTTP_NOEXCEPT;
    private:
        messages_storage m_data;
        uint32_t m_errnum;
    };

    /**
     * manapi exception
     */
    class exception final : public std::exception {
    public:
        explicit exception (messages msg);
        /**
         * initialize exception
         * @param errnum error code
         * @param message error message
         */
        explicit exception (manapi::err_num errnum, std::string message);

        /**
         * initialize exception
         * @param errnum error code
         * @param message error message. must contains a zero end
         */
        explicit exception (manapi::err_num errnum, std::string_view message);

        /**
         * initialize exception
         * @param errnum error code
         * @param message error message
         */
        explicit exception (manapi::err_num errnum, const char *message);

        exception (const exception &n);

        exception &operator=(const exception &n);

        ~exception() override;

        MANAPIHTTP_NODISCARD const char * what() const MANAPIHTTP_NOEXCEPT override;

        /**
         * Get the error code
         *
         * @return the error code
         */
        MANAPIHTTP_NODISCARD manapi::err_num err_num () const;
    private:
        messages m_data;
    };

    class status {
    public:
        status ();

        virtual ~status ();

        status (messages msg);

        status (err_num code, const char *msg);

        status (err_num code, std::string_view msg);

        status (err_num code, std::string msg);

        status (status &&n) MANAPIHTTP_NOEXCEPT;

        status& operator= (status &&n) MANAPIHTTP_NOEXCEPT;

        status (const status &n);

        status& operator= (const status &n);

        /**
         * Get the error message from the status
         *
         * @return the error message
         */
        MANAPIHTTP_NODISCARD std::string_view msg () const;

        /**
         * Get the error code from the status
         *
         * @return the error code
         */
        MANAPIHTTP_NODISCARD err_num code () const;

        /**
         * Is there no error
         *
         * @return true if there's no error
         */
        MANAPIHTTP_NODISCARD bool ok () const;

        /**
         * do log using the status
         */
        void log () const;

        /**
         * get the error code as a string
         *
         * @return the error code as a string
         */
        MANAPIHTTP_NODISCARD std::string_view status_msg () const;

        /**
         * If there is error it throws an exception
         *
         * @throws manapi::exception with the error code from the status
         */
        void unwrap () const;

        MANAPIHTTP_NODISCARD virtual std::string fullmsg () const;

        void stacktrace () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT;

        void data (messages data);

        messages data ();

        messages copy_data ();
    protected:
        messages m_data;
    };

    template<typename T, typename E = manapi::status>
    requires(!std::is_same_v<E, T>)
    class status_or {
    public:
        status_or (T value) : err_() {
            this->value_ = std::move(value);
        }

        status_or (E st) {
            this->err_ = std::move(st);
        }

        status_or(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        status_or&operator=(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        /**
         * get the error code from the status
         *
         * @return the error code from the status
         */
        MANAPIHTTP_NODISCARD manapi::err_num code () const {
            return this->err_.code();
        }

        /**
         * get the error code as a string from the status
         *
         * @return the error code as a string
         */
        MANAPIHTTP_NODISCARD std::string_view status_msg () const {
            return this->err_.status_msg();
        }

        /**
         * get the error message from the status
         *
         * @return the error message
         */
        MANAPIHTTP_NODISCARD std::string_view message () const {
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
        MANAPIHTTP_NODISCARD bool ok () const MANAPIHTTP_NOEXCEPT {
            return this->err_.code() == manapi::ERR_OK;
        }

        /**
         * get the error status
         *
         * @return the error status
         */
        E err () MANAPIHTTP_NOEXCEPT {
            return std::move(this->err_);
        }

        MANAPIHTTP_NODISCARD operator bool () MANAPIHTTP_NOEXCEPT {
            return this->err_.ok();
        }
    protected:
        std::optional<T> value_;
        E err_;
    };

    status status_ok ();

    status status_unknown ();

    status status_cancelled ();

    status status_invalid_argument ();

    status status_deadline_exceeded ();

    status status_not_found ();

    status status_already_exists ();

    status status_permission_denied ();

    status status_resource_exhausted ();

    status status_failed_precondition ();

    status status_aborted ();

    status status_unavailable ();

    status status_out_of_range ();

    status status_unimplemented ();

    status status_internal ();

    status status_data_loss ();

    status status_data_loss (std::string msg);

    status status_unknown (std::string msg);

    status status_cancelled (std::string msg);

    status status_invalid_argument (std::string msg);

    status status_deadline_exceeded (std::string msg);

    status status_not_found (std::string msg);

    status status_already_exists (std::string msg);

    status status_permission_denied (std::string msg);

    status status_unauthenticated (std::string msg);

    status status_resource_exhausted (std::string msg);

    status status_failed_precondition (std::string msg);

    status status_aborted (std::string msg);

    status status_unavailable (std::string msg);

    status status_out_of_range (std::string msg);

    status status_unimplemented (std::string msg);

    status status_internal (std::string msg);

    status status_data_loss (const char * msg);

    status status_unknown (const char * msg);

    status status_cancelled (const char * msg);

    status status_invalid_argument (const char * msg);

    status status_deadline_exceeded (const char * msg);

    status status_not_found (const char * msg);

    status status_already_exists (const char * msg);

    status status_permission_denied (const char * msg);

    status status_unauthenticated (const char * msg);

    status status_resource_exhausted (const char * msg);

    status status_failed_precondition (const char * msg);

    status status_aborted (const char * msg);

    status status_unavailable (const char * msg);

    status status_out_of_range (const char * msg);

    status status_unimplemented (const char * msg);

    status status_internal (const char * msg);

    status status_data_loss (std::string_view msg);

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

    template<typename T>
    auto unwrap (T status) {
       return status.unwrap();
    }
}

template <>
struct std::formatter<manapi::err_num> : std::formatter<int> {};