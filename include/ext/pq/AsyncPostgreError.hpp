#pragma once

#include "../../ManapiUtils.hpp"
#include "../../std/ManapiAsyncContext.hpp"
#include "../../std/ManapiAsyncSocket.hpp"

namespace manapi::ext::pq {
    enum sql_states {
	    // Class 00 - Successful Completion
        SQL_STATE_OK = 0,
	    // Class 01 - Warning
        SQL_STATE_WARNING = 79507,
        SQL_STATE_DYNAMIC_RESULT_SETS_RETURNED = 79519,
        /* TODO: sql states (warning and etc) */
	    // Class 02 - No Data (this is also a warning class per the SQL standard)
        SQL_STATE_NODATA = 159014,
        // Class 03 - SQL Statement Not Yet Complete
        SQL_STATE_NOT_YET_COMPLETE = 238521,
        // Class 08 - Connection Exception
        SQL_STATE_CONN_EXCEPTION = 636056,
        SQL_STATE_CONN_NOT_EXISTS = 636059,
        SQL_STATE_CONN_FAILURE = 636062,
        SQL_STATE_CONN_NON_ESTABLISHED = 636057,
        SQL_STATE_CONN_REJECTED = 636060,
        SQL_STATE_TRANS_RESOLUITON_UKNOWN = 636063,
        SQL_STATE_PROTOCOL_VIOLATION = 682282,
        // Class 23 - Integrity Constraint Violation
        SQL_STATE_INTEGRITY_CONSTRAINT_VIOLATION = 7076123,
        SQL_STATE_RESTRICT_VIOLATION = 7076124,
        SQL_STATE_NOT_NULL_VIOLATION = 7085370,
        SQL_STATE_FOREIGN_KEY_VIOLATION = 7085371,
        SQL_STATE_UNIQUE_VIOLATION = 7085373,
        SQL_STATE_CHECK_VIOLATION = 7085415,
        SQL_STATE_EXCLUSION_VIOLATION = 7122349,
        // Class 3D - Invalid Catalog Name
        SQL_STATE_INVALID_CATALOG_NAME = 11289994,
	    // Class 3F - Invalid Schema Name
        SQL_STATE_INVALID_SCHEMA_NAME = 11449008,
        // Class XX - Internal Error
        SQL_STATE_INTERNAL_ERROR = 115444164,
        SQL_STATE_DATA_CORRUPTED = 115444165,
        SQL_STATE_DATA_INDEX_CORRUPTED = 115444166
    };
    /**
     * error status for the OS event
     */
    class status final : public manapi::error::status {
    public:
        /**
         * initialize error status
         */
        status () : manapi::error::status() {

        }

        ~status () override = default;

        /**
         * initialize error status
         *
         * @param code error code
         * @param msg error msg
         * @param sqlcode sql status code
         * @param sqlmsg sql error message
         */
        status (manapi::err_num code, std::string_view msg, std::size_t sqlcode, std::string_view sqlmsg) : manapi::error::status(code, msg) {
            this->sqlmsg_ = (sqlmsg);
            this->sqlcode_ = sqlcode;
        }

        status (status &&n) MANAPIHTTP_NOEXCEPT = default;

        status &operator=(status &&n) MANAPIHTTP_NOEXCEPT = default;

        status (const error::status &n) : error::status(n) {
            this->sqlcode_ = 0;
        }

        /**
         * print log to the logger() if it exists,
         * otherwise it prints to the stdout
         */
        void log () const override {
            manapi_log_debug ("%.*s: msg: %.*s sqlmsg: %.*s",
                this->status_msg().size(), this->status_msg().data(), this->msg_.size(), this->msg_.data(),
                this->sqlmsg_.size(), this->sqlmsg_.data());
        }

        /**
         * throw a error if it exists, otherwise it does nothing
         */
        void unwrap() const override {
            if (this->code_) {
                throw manapi::exception (this->code_, std::format("{} sql-{}={}",
                    this->msg_, this->sqlcode_, this->sqlmsg_));
            }
        }

        MANAPIHTTP_NODISCARD bool is_sqlerr () const MANAPIHTTP_NOEXCEPT {
            return !this->sqlmsg_.empty();
        }

        /**
         * Get the sql error msg
         * @return sql error msg
         */
        MANAPIHTTP_NODISCARD std::string_view sqlmsg () const MANAPIHTTP_NOEXCEPT {
            return this->sqlmsg_;
        }

        /**
         * Get the sql status code
         * @return sql status code
         */
        MANAPIHTTP_NODISCARD sql_states sqlcode () const MANAPIHTTP_NOEXCEPT {
            return static_cast<sql_states>(this->sqlcode_);
        }

    private:
        std::size_t sqlcode_;
        std::string_view sqlmsg_;
    };

    template<typename T, typename E = manapi::ext::pq::status>
    class status_or final : public manapi::error::status_or<T, E> {
    public:
        /**
         * Initialize the status_or() instence
         * @param n the status error
         */
        status_or (ext::pq::status n) : error::status_or<T, E>(std::move(n)) {}

        status_or (T &&n) : error::status_or<T, E>(std::forward<decltype(n)>(n)) {}

        status_or (const T &n) : error::status_or<T, E>(n) {}

        status_or(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        status_or&operator=(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        /**
         * Get the sql error message
         * @return sql error message
         */
        MANAPIHTTP_NODISCARD std::string_view sqlmsg () const MANAPIHTTP_NOEXCEPT {
            return this->err_.sqlmsg();
        }

        MANAPIHTTP_NODISCARD int sqlcode () const MANAPIHTTP_NOEXCEPT {
            return this->err_.sqlcode();
        }

        MANAPIHTTP_NODISCARD bool is_sqlerr () const MANAPIHTTP_NOEXCEPT {
            return this->err_.is_sqlerr();
        }
    };
}