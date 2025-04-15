#pragma once

#include <filesystem>

#include "ManapiUtils.hpp"
#include "./ManapiAsync.hpp"
#include "./ManapiUtils.hpp"
#include "./ManapiJson.hpp"
#include "./async/ManapiAsyncContext.hpp"
#include "async/ManapiCancellation.hpp"

namespace manapi::filesystem {
    static char delimiter = std::filesystem::path::preferred_separator;
    static std::string string_delimiter (&delimiter, 1);

    std::string basename (const std::string &path);
    std::string extension (const std::string &path);

    bool exists (const std::string &path);
    manapi::future<bool> async_exists (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    std::filesystem::file_time_type last_time_write (const std::string &path);
    manapi::future<std::filesystem::file_time_type> async_last_time_write (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    bool mkdir (const std::string &path, bool recursive = true);
    manapi::future<bool> async_mkdir (std::shared_ptr<manapi::async::context> ctx, std::string path, bool recursive = true, manapi::async::cancellation_action cancellation = nullptr);

    void append_delimiter(std::string &path);

    ssize_t get_size (std::ifstream& f);
    ssize_t get_size (const std::string &path);
    manapi::future<ssize_t> async_get_size (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    void write (const std::string &path, const std::string &data);

    future<void> async_write (std::shared_ptr<manapi::async::context> ctx, std::string_view path, std::function<ssize_t(void *buff, ssize_t buff_size)> cb, unsigned int mode = 0644, std::function<future<void>()> *cancellation = nullptr);
    future<void> async_write (std::shared_ptr<manapi::async::context> ctx, std::string_view path, std::string_view data, unsigned int mode = 0644, std::function<future<void>()> *cancellation = nullptr);


    std::string read (const std::string &path);

    future<void> async_read (std::shared_ptr<manapi::async::context> ctx, std::string_view path, std::function<ssize_t(const void *buff, ssize_t buff_size)> cb, std::function<future<void>()> *cancellation = nullptr);
    future<std::string> async_read (std::shared_ptr<manapi::async::context> ctx, std::string_view path, std::function<future<void>()> *cancellation = nullptr);

    void copy (std::ifstream &f, const ssize_t &start, const ssize_t &back, std::ofstream &o);

    std::string clean (const std::string &str);

    bool is_dir (const std::string &str);
    bool is_file (const std::string &str);

    manapi::future<bool> async_is_dir (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);
    manapi::future<bool> async_is_file (std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation = nullptr);

    std::string back (std::string str);

    template <class... Args>
    inline std::string join(const std::string &path, Args&&...args) {
        return clean(path + (... + (string_delimiter + std::forward<Args>(args))));
    }
}