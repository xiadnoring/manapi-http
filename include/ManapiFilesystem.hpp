#pragma once

#include <filesystem>

#include "ManapiUtils.hpp"
#include "./ManapiAsync.hpp"
#include "./ManapiUtils.hpp"
#include "./ManapiJson.hpp"
#include "./async/ManapiAsyncContext.hpp"

namespace manapi::filesystem {
    static char delimiter = std::filesystem::path::preferred_separator;
    static std::string string_delimiter (&delimiter, 1);

    std::string basename (const std::string &path);
    std::string extension (const std::string &path);

    bool exists (const std::string &path);

    std::string last_time_write (const std::filesystem::path &f,    bool time = false);
    std::string last_time_write (const std::string &path,           bool time = false);
    void mkdir (const std::string &path,           bool recursive = true);
    void append_delimiter(std::string &path);
    ssize_t get_size (std::ifstream& f);
    ssize_t get_size (const std::string &path);


    void write (const std::string &path, const std::string &data);

    future<void> write_async (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path, std::function<ssize_t(void *buff, ssize_t buff_size)> cb, const unsigned int &mode = 0644, std::function<future<void>()> *cancellation = nullptr);
    future<void> write_async (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path, std::string_view data, const unsigned int &mode = 0644, std::function<future<void>()> *cancellation = nullptr);


    std::string read (const std::string &path);

    future<void> read_async (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path, std::function<ssize_t(const void *buff, ssize_t buff_size)> cb, std::function<future<void>()> *cancellation = nullptr);
    future<std::string> read_async (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path, std::function<future<void>()> *cancellation = nullptr);

    void copy (std::ifstream &f, const ssize_t &start, const ssize_t &back, std::ofstream &o);

    std::string clean (const std::string &str);

    bool is_dir (const std::string &str);
    bool is_file (const std::string &str);

    std::string back (std::string str);

    template <class... Args>
    inline std::string join(const std::string &path, Args&&...args) {
        return clean(path + (... + (string_delimiter + std::forward<Args>(args))));
    }
}

namespace manapi::filesystem::config {
    void write (const std::string &name, json &data);
    json read (const std::string &name);
}