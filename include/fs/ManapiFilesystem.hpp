#pragma once

#include <filesystem>

#include "../ManapiUtils.hpp"
#include "../json/ManapiJson.hpp"
#include "../std/ManapiAsyncContext.hpp"
#include "../ManapiAsync.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::filesystem {
    manapi::future<ev::status_or<bool>> async_exists (std::string path, manapi::ctoken cancellation = nullptr);

    manapi::future<ev::status_or<std::chrono::system_clock::time_point>> async_last_time_write (std::string path, manapi::ctoken cancellation = nullptr);

    manapi::future<ev::status> async_mkdir (std::string path, int mode = 0644, bool recursive = true, manapi::ctoken cancellation = nullptr);

    future<ev::status_or<ev::file>> async_open (std::string path, int flags, int mode, manapi::ctoken cancellation = nullptr);

    future<ev::status> async_close (ev::file file, ctoken cancellation = nullptr);

    future<ev::status_or<ssize_t>> async_write (ev::file file, const void *data, ssize_t size, int64_t offset = -1, manapi::ctoken cancellation = nullptr);

    future<ev::status_or<ssize_t>> async_read (ev::file file, void *data, ssize_t size, int64_t offset = -1, manapi::ctoken cancellation = nullptr);

    future<ev::status> async_write (std::string path, std::string data, int mode, int flags = ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_APPEND, int64_t offset = -1, manapi::ctoken cancellation = nullptr);

    future<ev::status_or<std::string>> async_read (std::string path, int flags = ev::FS_O_RDONLY, int64_t offset = -1, manapi::ctoken cancellation = nullptr);

    /**
     * Async Write
     *
     * @param file
     * @param buff
     * @param nbuff
     * @param offset
     * @param cancellation
     * @return
     */
    future<ev::status_or<ssize_t>> async_write (ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset = -1, ctoken cancellation = nullptr);

    /**
     * Async Read
     *
     * @param file
     * @param buff
     * @param nbuff
     * @param offset
     * @param cancellation
     * @return
     */
    future<ev::status_or<ssize_t>> async_read (ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset = -1, ctoken cancellation = nullptr);

    future<ev::status_or<ssize_t>> async_write (ev::file file, slice_view slice, int64_t offset = -1, ctoken cancellation = nullptr);

    future<ev::status_or<ssize_t>> async_read (ev::file file, slice_view slice, int64_t offset = -1, ctoken cancellation = nullptr);

    future<ev::status_or<ssize_t>> async_file_size (std::string path, manapi::ctoken cancellation = nullptr);

    future<ev::status> async_unlink (std::string path, ctoken cancellation = nullptr);

    future<ev::status> async_rmdir (std::string path, ctoken cancellation = nullptr);

    future<ev::status_or<ev::dir_t *>> async_opendir (std::string path, ctoken ctoken = nullptr);

    future<ev::status> async_closedir (ev::dir_t *directory, ctoken cancellation = nullptr);

    future<ev::status> async_statfs (std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, ctoken cancellation = nullptr);

    /**
     * stat
     *
     * @param path Path to the file
     * @param callback Callback which accept a result. If the file doesn't exist, it has a @code nullptr@endcode value
     * @param cancellation Cancellation Token
     * @return
     */
    future<ev::status> async_stat (std::string path, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation = nullptr);

    /**
     * fstat
     *
     * @param file Path to the file
     * @param callback Callback which accept a result. If the file doesn't exist, it has a @code nullptr@endcode value
     * @param cancellation Cancellation token
     * @return
     */
    future<ev::status> async_fstat (ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation = nullptr);

    future<ev::status> async_rename (std::string oldpath, std::string newpath, ctoken cancellation = nullptr);

    future<ev::status> async_copyfile (std::string src, std::string dest, int flags, ctoken cancellation = nullptr);

    future<ev::status> async_chmod (std::string path, int mode, ctoken cancellation = nullptr);

    future<ev::status> async_fchmod (ev::file file, int mode, ctoken cancellation = nullptr);

    future<ev::status> async_utime (std::string path, double atime, double mtime, ctoken cancellation = nullptr);

    future<ev::status> async_futime (ev::file file, double atime, double mtime, ctoken cancellation = nullptr);

    future<ev::status> async_link (std::string path, std::string newpath, ctoken cancellation = nullptr);

    future<ev::status> async_symlink (std::string path, std::string newpath, int flags, ctoken cancellation = nullptr);

    future<ev::status_or<std::string>> async_readlink (std::string path, ctoken cancellation = nullptr);

    future<ev::status_or<std::string>> async_realpath (std::string path, ctoken cancellation = nullptr);

    future<ev::status> async_chown (std::string path, ev::uid_t uid, ev::gid_t gid, ctoken cancellation = nullptr);

    future<ev::status> async_fchown (ev::file file, ev::uid_t uid, ev::gid_t gid, ctoken cancellation = nullptr);

    future<ev::status> async_fsync (ev::file file, ctoken cancellation = nullptr);

    future<ev::status_or<std::string>> async_mkdtemp (std::string tpl, ctoken cancellation = nullptr);

    future<ev::status_or<std::pair<std::string, ev::file>>> async_mkstemp (std::string tpl, ctoken cancellation = nullptr);

    future<ev::status> async_fdatasync (ev::file file, ctoken cancellation = nullptr);

    future<ev::status> async_ftruncate (ev::file file, int64_t offset, ctoken cancellation = nullptr);

    future<ev::status> async_access (std::string path, int mode, ctoken cancellation = nullptr);

    future<ev::status_or<std::size_t>> async_scandir (std::string path, int flags, std::move_only_function<void(ev::dir_t *, std::size_t)> callback, ctoken cancellation = nullptr);

    future<ev::status_or<std::size_t>> async_readdir (ev::dir_t *dir, std::move_only_function<void(ev::dir_t *, std::size_t)> callback, ctoken cancellation = nullptr);
}


namespace manapi::filesystem::path {
    static constexpr char delimiter = std::filesystem::path::preferred_separator;

    static constexpr std::string_view string_delimiter (&delimiter, 1);

    std::string_view basename (std::string_view path);

    std::string_view extension (std::string_view path);

    void append_delimiter(std::string &path);

    std::string serialize (std::string_view str);

    std::string absolute (std::string_view path);

    std::string root_directory ();

    std::string_view back (std::string_view str);

    void append (std::string &path, std::string_view next, bool root);

    void append (std::string &path, std::string_view next);

    std::string current_path ();

    template <class... Args>
    std::string join(std::string path, Args&&...args) {
        std::size_t i = 0;
        (..., path::append(path, std::forward<Args>(args), !(i++) ));
        return std::move(path);
    }
}