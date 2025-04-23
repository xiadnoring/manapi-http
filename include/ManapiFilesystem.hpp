#pragma once

#include <filesystem>

#include "ManapiUtils.hpp"
#include "./ManapiAsync.hpp"
#include "./ManapiUtils.hpp"
#include "./ManapiJson.hpp"
#include "./async/ManapiAsyncContext.hpp"
#include "async/ManapiCancellation.hpp"

namespace manapi::filesystem {
    manapi::future<bool> async_exists (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    manapi::future<std::filesystem::file_time_type> async_last_time_write (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    manapi::future<void> async_mkdir (std::shared_ptr<manapi::async::context> ctx, std::string path, int mode = 0644, bool recursive = true, manapi::async::cancellation_action cancellation = nullptr);

    future<ev::file> async_open (std::shared_ptr<manapi::async::context> ctx, std::string path, int flags, int mode, manapi::async::cancellation_action cancellation = nullptr);

    future<void> async_close (std::shared_ptr<manapi::async::context> ctx, ev::file file, async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_write (std::shared_ptr<manapi::async::context> ctx, ev::file file, const void *data, ssize_t size, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_read (std::shared_ptr<manapi::async::context> ctx, ev::file file, const void *data, ssize_t size, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_file_size (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    /**
     * stat
     *
     * @param ctx Async Context
     * @param path Path to the file
     * @param callback Callback which accept a result. If the file doesn't exists, it has a @code nullptr@endcode value
     * @param cancellation Cancellation Token
     * @return
     */
    future<void> async_stat (std::shared_ptr<async::context> ctx, std::string path, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation = nullptr);

    /**
     * fstat
     *
     * @param ctx Async Context
     * @param file Path to the file
     * @param callback Callback which accept a result. If the file doesn't exists, it has a @code nullptr@endcode value
     * @param cancellation Cancellation token
     * @return
     */
    future<void> async_fstat (std::shared_ptr<async::context> ctx, ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation = nullptr);
}


namespace manapi::filesystem::path {
    static char delimiter = std::filesystem::path::preferred_separator;
    static std::string string_delimiter (&delimiter, 1);

    std::string basename (const std::string &path);
    std::string extension (const std::string &path);

    void append_delimiter(std::string &path);

    std::string clean (const std::string &str);
    std::string back (std::string str);

    template <class... Args>
    inline std::string join(const std::string &path, Args&&...args) {
        return clean(path + (... + (string_delimiter + std::forward<Args>(args))));
    }
}