#pragma once

#include <filesystem>

#include "ManapiUtils.hpp"
#include "./ManapiAsync.hpp"
#include "./ManapiUtils.hpp"
#include "./ManapiJson.hpp"
#include "./async/ManapiAsyncContext.hpp"
#include "async/ManapiCancellation.hpp"

namespace manapi::filesystem {
    manapi::future<bool> async_exists (std::string path, manapi::async::cancellation_action cancellation = nullptr);

    manapi::future<std::chrono::system_clock::time_point> async_last_time_write (std::string path, manapi::async::cancellation_action cancellation = nullptr);

    manapi::future<void> async_mkdir (std::string path, int mode = 0644, bool recursive = true, manapi::async::cancellation_action cancellation = nullptr);

    future<ev::file> async_open (std::string path, int flags, int mode, manapi::async::cancellation_action cancellation = nullptr);

    future<void> async_close (ev::file file, async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_write (ev::file file, const void *data, ssize_t size, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_read (ev::file file, void *data, ssize_t size, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<void> async_write (std::string path, std::string data, int mode, int flags = ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_APPEND, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<std::string> async_read (std::string path, int flags = ev::FS_O_RDONLY, int64_t offset = -1, manapi::async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_file_size (std::string path, manapi::async::cancellation_action cancellation = nullptr);

    future<void> async_unlink (std::string path, async::cancellation_action cancellation = nullptr);

    future<void> async_rmdir (std::string path, async::cancellation_action cancellation = nullptr);

    future<ev::dir_t *> async_opendir (std::string path, async::cancellation_action cancellation_action = nullptr);

    future<void> async_closedir (ev::dir_t *directory, async::cancellation_action cancellation = nullptr);

    future<bool> async_statfs (std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, async::cancellation_action cancellation = nullptr);

    /**
     * stat
     *
     * @param ctx Async Context
     * @param path Path to the file
     * @param callback Callback which accept a result. If the file doesn't exists, it has a @code nullptr@endcode value
     * @param cancellation Cancellation Token
     * @return
     */
    future<bool> async_stat (std::string path, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation = nullptr);

    /**
     * fstat
     *
     * @param ctx Async Context
     * @param file Path to the file
     * @param callback Callback which accept a result. If the file doesn't exists, it has a @code nullptr@endcode value
     * @param cancellation Cancellation token
     * @return
     */
    future<bool> async_fstat (ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation = nullptr);

    future<void> async_rename (std::string oldpath, std::string newpath, async::cancellation_action cancellation = nullptr);

    future<void> async_copyfile (std::string src, std::string dest, int flags, async::cancellation_action cancellation = nullptr);

    future<void> async_chmod (std::string path, int mode, async::cancellation_action cancellation = nullptr);

    future<void> async_fchmod (ev::file file, int mode, async::cancellation_action cancellation = nullptr);

    future<void> async_utime (std::string path, double atime, double mtime, async::cancellation_action cancellation = nullptr);

    future<void> async_futime (ev::file file, double atime, double mtime, async::cancellation_action cancellation = nullptr);

    future<void> async_link (std::string path, std::string newpath, async::cancellation_action cancellation = nullptr);

    future<void> async_symlink (std::string path, std::string newpath, int flags, async::cancellation_action cancellation = nullptr);

    future<std::string> async_readlink (std::string path, async::cancellation_action cancellation = nullptr);

    future<std::string> async_realpath (std::string path, async::cancellation_action cancellation = nullptr);

    future<void> async_chown (std::string path, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation = nullptr);

    future<void> async_fchown (ev::file file, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation = nullptr);

    future<void> async_fsync (ev::file file, async::cancellation_action cancellation = nullptr);

    future<std::string> async_mkdtemp (std::string tpl, async::cancellation_action cancellation = nullptr);

    future<std::pair<std::string, ev::file>> async_mkstemp (std::string tpl, async::cancellation_action cancellation = nullptr);

    future<void> async_fdatasync (ev::file file, async::cancellation_action cancellation = nullptr);

    future<void> async_ftruncate (ev::file file, int64_t offset, async::cancellation_action cancellation = nullptr);

    future<int> async_access (std::string path, int mode, async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_scandir (std::string path, int flags, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation = nullptr);

    future<ssize_t> async_readdir (ev::dir_t *dir, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation = nullptr);
}


namespace manapi::filesystem::path {
    static char delimiter = std::filesystem::path::preferred_separator;
    static std::string string_delimiter (&delimiter, 1);

    std::string basename (std::string_view path);
    std::string extension (std::string_view path);

    void append_delimiter(std::string &path);

    std::string clean (std::string_view str);
    std::string back (std::string str);

    template <class... Args>
    inline std::string join(std::string_view path, Args&&...args) {
        return clean(std::string{path} + (... + (string_delimiter + std::forward<Args>(args))));
    }
}