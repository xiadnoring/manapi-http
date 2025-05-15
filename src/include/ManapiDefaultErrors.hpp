#pragma once

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
        ERRMSG_FS_FAILURE_INIT,
        ERRMSG_WATCHER_COMMAND_FAILED,
        ERRMSG_WATCHER_BIND_FAILED,
        ERRMSG_RANDOM_STRING_FAILED,
        ERRMSG_UNHANDLED_EXCEPTION,
        ERRMSG_CONNECTION_WAS_CLOSED,
        ERRMSG_CUSTOM_CALLBACK_ERR1
    };

    const constexpr char *default_msgs[] = {
        "file not found",
        "file exists",
        "size isn't the same",
        "by error: {}",
        "error when receiving additional data",
        "fs i/o operations failed: {}",
        "fs callback failed: {}",
        "fs i/o operation has been cancelled",
        "fs i/o init watcher failed",
        "watcher command failed",
        "fs watcher bind failed",
        "random string failed",
        "unhandled exception. errnum: {}; errmsg: {}; data: {}",
        "connection was closed",
        "custom callback failed due to {}"
    };
}

template <>
struct std::formatter<manapi::error::default_msgs_types> : std::formatter<int> {};