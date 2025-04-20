#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstdarg>

#include "ManapiFilesystem.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <fileapi.h>
#   include <winsock2.h>
#   include <io.h>
#else
#   include <sys/stat.h>
#endif

#include <fcntl.h>
#include <cstring>

#include "ManapiBeforeDelete.hpp"
#include "async/ManapiAsyncFileStream.hpp"

static const std::string folder_configs;

#define MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE 4096LL
#define MANAPIHTTP_FS_MAKE_SYNC_ASYNC(func, returntype, cancellmsg, ...) co_return co_await manapi::async::promise<returntype, std::false_type> (ctx, [&] (manapi::async::promise<returntype>::resolve_t resolve, manapi::async::promise<returntype>::reject_t reject) -> void { \
    if (cancellation && cancellation.contains_cancel_callback()) { \
        cancellation.set_cancel_callback([reject] -> manapi::future<> { co_return reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(ERR_CANCELLED, cancellmsg))); }); \
    } \
    std::thread ([__VA_ARGS__, resolve = std::move(resolve), reject = std::move(reject)] () -> void { \
        try {  resolve(func); } catch (...) {  reject(std::current_exception()); } \
    }).detach(); if (cancellation) { cancellation.ready(); } });

std::string manapi::filesystem::basename(const std::string& path) {
    size_t pos = path.find_last_of(std::filesystem::path::preferred_separator);

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return path;
}

std::string manapi::filesystem::extension(const std::string& path) {
    size_t pos = path.find_last_of('.');

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return "";
}

bool manapi::filesystem::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

manapi::future<bool> manapi::filesystem::async_exists(std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::exists(path), bool, "async exists was cancelled", path);
}

std::filesystem::file_time_type manapi::filesystem::last_time_write (const std::string &path) {
    return std::filesystem::last_write_time(path);
}

manapi::future<std::filesystem::file_time_type> manapi::filesystem::async_last_time_write(std::shared_ptr<manapi::async::context> ctx,
    std::string path, manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::last_time_write(path), std::filesystem::file_time_type, "async last time write was cancenlled", path);
}

bool manapi::filesystem::mkdir (const std::string &path, bool recursive) {
    if (recursive) {
        return std::filesystem::create_directories(path);
    }

    return std::filesystem::create_directory(path);
}

manapi::future<bool> manapi::filesystem::async_mkdir(std::shared_ptr<manapi::async::context> ctx, std::string path,
    bool recursive, manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::mkdir(path, recursive), bool, "async mkdir was cancelled", path, recursive);
}

void manapi::filesystem::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
    {
        path.push_back(std::filesystem::path::preferred_separator);
    }
}

ssize_t manapi::filesystem::get_size (std::ifstream& f) {
    f.seekg(0, std::ifstream::end);
    const ssize_t fileSize = f.tellg();
    f.seekg(0, std::ifstream::beg);

    return fileSize;
}

ssize_t manapi::filesystem::get_size (const std::string& path) {
    return static_cast<ssize_t>(std::filesystem::file_size(path));
}

manapi::future<ssize_t> manapi::filesystem::async_get_size(std::shared_ptr<manapi::async::context> ctx, std::string path,
    manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::get_size(path), ssize_t, "async get size was cancelled", path);
}

void manapi::filesystem::write (const std::string &path, const std::string &data) {
    std::ofstream out (path);

    if (!out.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "cannot open config to write: {}", path);
    }

    out << data;
}


manapi::future<> manapi::filesystem::async_write(std::shared_ptr<manapi::async::context> ctx, std::string_view path, std::string_view data, unsigned int mode, manapi::async::cancellation_action cancellation) {
    manapi::filesystem::fstream f (ctx, path);
    co_await f.open(f.FILE_WRITE|f.FILE_CREATE, mode);

    if (!f.is_open()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "{} failed to open", path);
    }

    if (cancellation && cancellation.contains_cancel_callback()) {
        cancellation.set_cancel_callback([f] () mutable
            -> manapi::future<> { co_await f.close(); });
    }

}

std::string manapi::filesystem::read (const std::string &path) {
    std::ifstream in (path);

    if (!in.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "cannot open config to read: {}", path);
    }

    const ssize_t size = get_size(in);

    // 20 MB
    if (size >= 20 * 1024 * 1024) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "The size of the file: {} is too large for read with this function.", size);
    }

    std::string content;
    content.resize(size);

    in.read (content.data(), size);

    return content;
}

manapi::future<std::string> manapi::filesystem::async_read(std::shared_ptr<manapi::async::context> ctx, std::string_view path, manapi::async::cancellation_action cancellation) {
    manapi::filesystem::fstream f (ctx, path);
    co_await f.open(f.FILE_READ);

    if (!f.is_open()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "{} failed to open", path);
    }

    if (cancellation) {
        if (cancellation.contains_cancel_callback()) {
            cancellation.set_cancel_callback([f] () mutable
                -> manapi::future<> { co_await f.close(); });
        }

        if (cancellation.contains_timeout()) {
            cancellation.timeout_struct([f] () mutable
                -> void {
                    /* event thread */
                    f.sync_close();
            });
        }
    }

    std::string data;
    data.resize(f.total_size());

    ssize_t size = 0;

    while (true) {
        auto rhs = static_cast<ssize_t>(data.size() - size);

        if (!rhs && !f.eof()) {
            data.resize(f.total_size());
            continue;
        }

        if (rhs) {
            rhs = co_await f.read(data.data() + size, rhs);
        }

        if (rhs < 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "{} failed to read", path);
        }

        if (!rhs) {
            break;
        }

        size += rhs;
    }

    data.resize(size);

    co_return std::move(data);
}

void manapi::filesystem::copy (std::ifstream &f, const ssize_t &start, const ssize_t &back, std::ofstream &o) {
    if (!f.is_open() || !o.is_open()) {
        f.close();
        o.close();

        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "{}", "cannot open files for operations");
    }

    f.seekg (start);

    ssize_t size        = back - start + 1;

    char buff[MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE];

    while (size != 0) {
        const ssize_t block_size = size > MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE ? MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE : size;
        f.read(buff, block_size);

        o.write(buff, block_size);

        size -= block_size;
    }

    f.seekg(0);
}

std::string manapi::filesystem::back (std::string str) {
    size_t size = str.size();

    // clean delimiters at the end
    for (size_t i = size - 1; i > 1; i--) {
        if (delimiter == str[i]) {
            str.pop_back();
            size--;

            continue;
        }

        break;
    }

    bool delimiter_prev = false;
    for (size_t i = size - 1; i != 0; i--) {
        while (i >= 0 && str[i] == delimiter) {
            i--;

            str.pop_back();

            delimiter_prev = true;
        }


        if (delimiter_prev)
        {
            break;
        }

        else
        {
            str.pop_back();
        }
    }

    return str;
}

std::string manapi::filesystem::clean (const std::string &str) {
    std::string cleaned;
    size_t size = str.size();

    // clean delimiters at the end
    for (size_t i = size - 1; i > 1; i--) {
        if (delimiter == str[i]) {
            size --;
            continue;
        }

        break;
    }

    // skip double delimiters
    bool delimiter_prev = false;

    for (size_t i = 0; i < size; i++) {
        if (str[i] == delimiter) {
            if (i + 1 != size) {

                // check for . or ..
                if (str[i + 1] == '.') {
                    if (i + 2 != size) {
                        if (str[i + 2] == '/') {
                            i = i + 2 - 1;
                            continue;
                        }
                        else if (str[i + 2] == '.') {
                            bool can_back = cleaned.size() > 1;

                            if (i + 3 != size) {
                                if (str[i + 3] == '/' && can_back) {
                                    cleaned = back (cleaned);
                                    i = i + 3 - 1;

                                    continue;
                                }
                            }
                            else if (can_back) {
                                cleaned = back (cleaned);

                                break;
                            }
                        }
                    }
                    else {
                        break;
                    }
                }
            }

            if (delimiter_prev)
                continue;

            delimiter_prev = true;
        }
        else if (delimiter_prev)
            delimiter_prev = false;

        cleaned += str[i];
    }

    return cleaned;
}

bool manapi::filesystem::is_dir (const std::string &str) {
    return std::filesystem::is_directory(std::filesystem::path (str));
}

bool manapi::filesystem::is_file (const std::string &str) {
    std::filesystem::path p(str);
    return std::filesystem::is_regular_file(p) || std::filesystem::is_character_file(p) || std::filesystem::is_block_file(p);
}

manapi::future<bool> manapi::filesystem::async_is_dir(std::shared_ptr<manapi::async::context> ctx, std::string path,
    manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::is_dir(path), bool, "async is_dir was cancelled", path);
}

manapi::future<bool> manapi::filesystem::async_is_file(std::shared_ptr<manapi::async::context> ctx, std::string path,
    manapi::async::cancellation_action cancellation) {
    MANAPIHTTP_FS_MAKE_SYNC_ASYNC(manapi::filesystem::is_file(path), bool, "async is_file was cancelled", path);
}
