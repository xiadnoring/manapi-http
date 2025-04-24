#pragma once

#include <map>
#include <string>
#include "ManapiUtils.hpp"

namespace manapi {
    enum err_num {
        ERR_OK = 0,
        ERR_FATAL = 1,
        ERR_UNDEFINED = 2,
        ERR_DEBUG = 3,
        ERR_COMPRESS_DATA = 4,
        ERR_FILE_EXISTS = 5,
        ERR_FILE_IO = 6,
        ERR_HTTP_PARAM_MISSING = 7,
        ERR_HTTP_BODY_MISSING = 8,
        ERR_HTTP_BODY_TOO_LONG = 9,
        ERR_HTTP_CONTENT_TYPE_MISSING = 10,
        ERR_HTTP_BODY_BOUNDARY_MISSING = 12,
        ERR_HTTP_INVALID_CONTENT_TYPE = 13,
        ERR_HTTP_BODY_MASK_FAILED = 14,
        ERR_HTTP_BODY_NOT_CONTAINS_FILE = 15,
        ERR_HTTP_IMPORTANT_HEADER_MISSING = 16,
        ERR_HTTP_PROTOCOL_ERROR = 17,
        ERR_HTTP_QUERY_PARAM_MISSING = 18,
        ERR_DIVIDED_BY_ZERO = 19,
        ERR_EXTERNAL_LIB_CRASH = 20,
        ERR_CONFIG_ERROR = 21,
        ERR_HTTP_HEADER_MISSING = 22,
        ERR_HTTP_HEADER_INVALID = 23,
        ERR_HTTP_ADD_PAGE = 24,
        ERR_HTTP_SETTINGS_INCOMPATIBILITY = 25,
        ERR_HTTP_UNSUPPORTED = 26,
        ERR_UNSUPPORTED = 27,
        ERR_STORAGE_OBJECT_IS_NULL = 28,
        ERR_FUNCTION_IS_NULL = 29,
        ERR_HTTP_CONNECTION_WAS_CLOSED = 30,
        ERR_HTTP_PARSER_BUG = 31,
        ERR_ALGORITHM_NO_SUPPORT = 32,
        ERR_ALGORITHM_INIT_FAIL = 33,
        ERR_QUIC_PROTOCOL_ERROR = 34,
        ERR_BUG = 35,
        ERR_SUBSCRIBE_FAILURE = 36,
        ERR_INTERRUPTED = 37,
        ERR_THREAD_SAFE = 38,
        ERR_POSTGRE_ERROR = 39,
        ERR_POSTGRE_RESULT = 40,
        ERR_CONNECTION_TIMEOUT = 41,
        ERR_PARSE_ERROR = 42,
        ERR_PARSE_INVALID_SYMBOL = 43,
        ERR_PARSE_UNEXPECTED_END = 44,
        ERR_PARSE_INVALID_CHAR = 45,
        ERR_HTTP_GET_PARAMS_MASK_FAILED = 46,
        ERR_SOCKET = 47,
        ERR_FILE_DESCRIPTOR = 48,
        ERR_INCOMPATIBLE_SETTING = 49,
        ERR_CANCELLED = 50,
        ERR_CANCELLATION_FAILED = 51,
        ERR_FS_IO = 52,
        ERR_FS_IO_RESULT = 53,
        ERR_FILE_NOT_FOUND = 54
    };

    extern const std::map <err_num, std::string> err_msg;

    const inline std::string & get_msg_by_err_num(const err_num &errnum) {
        if (err_msg.contains(errnum))
        {
            return err_msg.at(errnum);
        }
        return err_msg.at(ERR_UNDEFINED);
    }

    class exception : public std::exception {
    public:
        explicit exception (const err_num &errnum, std::string message_);
        explicit exception (const err_num &errnum, int addititonal_num_data, std::string message_);
        [[nodiscard]] const char * what() const noexcept override;
        [[nodiscard]] const manapi::err_num &err_num () const;
        [[nodiscard]] const int &get_additional_num_data () const;
    private:
        manapi::err_num errnum_;
        int addititonal_num_data = -1;
        std::string message;
    };
}

namespace manapi::error {
    enum default_msgs_types {
        ERRMSG_FILE_NOT_FOUND = 0,
        ERRMSG_FILE_EXISTS,
        ERRMSG_SIZE_NOT_SAME,
        /* 1 param */
        ERRMSG_BY_ERROR,
        ERRMSG_WHEN_RECV_ADDITIONAL,
        ERRMSG_FS_FAILURE_FS_IO_OPERATIONS,
        ERRMSG_FS_FAILURE_CALLBACK,
        ERRMSG_FS_CANCELLED,
        ERRMSG_FS_FAILURE_INIT
    };

    extern const char *default_msgs[];
}