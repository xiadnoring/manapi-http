#pragma once

namespace manapi::net::http::internal {
    enum response_type {
        RESPONSE_PROXY = 0,
        RESPONSE_TEXT,
        RESPONSE_NO_DATA,
        RESPONSE_FILE,
        RESPONSE_FORMDATA,
        RESPONSE_SYNC_CALLBACK,
        RESPONSE_ASYNC_CALLBACK,
        RESPONSE_STREAM
    };

    enum response_flags {
        RESPONSE_FLAG_COMPRESS_ENABLED = 1,
        RESPONSE_FLAG_PARTITIAL_ENABLED = 2
    };

    enum request_flags {
        REQUEST_FLAG_IS_NO_PROPAGATION = 1
    };

    enum request_data_flags {
        REQ_DATA_FLAG_BODY_LIMITED = 1<<0
    };
}