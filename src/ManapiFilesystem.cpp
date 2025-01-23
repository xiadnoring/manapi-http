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

#include "ManapiBeforeDelete.hpp"

static const std::string folder_configs;
#define MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE 4096LL

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

void manapi::filesystem::config::write(const std::string &name, manapi::json &data) {
    manapi::filesystem::write(folder_configs + name, data.dump(4));
}

manapi::json manapi::filesystem::config::read(const std::string &name) {
    return json (manapi::filesystem::read (folder_configs + name), true);
}

std::string manapi::filesystem::last_time_write (const std::filesystem::path &f, bool time) {
    auto last_write_time = std::filesystem::last_write_time(f);
    if (time) {
        return std::format("{:%Y-%m-%d-%H-%M-%S}", last_write_time);
    }
    return std::format("{:%Y-%m-%d}", last_write_time);

}

std::string manapi::filesystem::last_time_write (const std::string &path, bool time) {
    std::filesystem::path f (path);
    return std::move(last_time_write(f, time));
}

void manapi::filesystem::mkdir (const std::string &path, bool recursive) {
    if (recursive) {
        std::filesystem::create_directories(path);
        return;
    }

    std::filesystem::create_directory(path);
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

void manapi::filesystem::write (const std::string &path, const std::string &data) {
    std::ofstream out (path);

    if (!out.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "cannot open config to write: {}", path);
    }

    out << data;
}

manapi::future<> manapi::filesystem::write_async(const std::shared_ptr<async::context> &ctx, std::string_view path, std::function<ssize_t(void *buff, ssize_t buff_size)> cb, const unsigned int &mode, std::function<future<void>()> *cancellation) {
#ifdef _WIN32
    HANDLE h = ::CreateFile(path.data(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, "Failed to open file: handle = INVALID_HANDLE_VALUE");
    }
    auto fd = _open_osfhandle((intptr_t)h, _O_CREAT|_O_RDWR|_O_TRUNC);
    u_long arg = 1;
    ioctlsocket(fd, FIONBIO, &arg);
#else
    auto fd = ::open(path.data(), O_NONBLOCK|O_CREAT|O_RDWR|O_TRUNC, mode);
#endif
    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, "Failed to open file: fd < 0");
    }

    try {
        std::string buffer;
        buffer.resize(BUFSIZ);

        auto written = cb (buffer.data(), static_cast<ssize_t>(buffer.size()));

        if (written >= 0) {
            std::string_view data (buffer.data(), written);

            co_await async::promise<void> (ctx, [cancellation, &buffer, fd, ctx, &data, cb = std::move(cb)](async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) mutable -> future<> {
                auto watcher = co_await ctx->eventloop()->watch_fd(fd, ev::WRITE, [&buffer, &data, resolve = std::move(resolve), reject = std::move(reject), ctx, cb = std::move(cb)] (ev::io &w, int revents) mutable  -> void {
                    if (revents & ev::WRITE) {
                        if (!data.empty()) {
                            auto rhs = ::write(w.fd, data.data(), data.size());

                            if (rhs <= 0) {
                                auto _reject = std::move(reject);
                                ctx->eventloop()->stop_watcher(w);

                                _reject(std::make_exception_ptr(manapi::exception (ERR_FILE_IO, "write_async(...): Failed to write file")));
                                return;
                            }

                            data = data.substr(rhs);
                        }

                        if (data.empty()) {
                            auto written = cb (buffer.data(), static_cast<ssize_t>(buffer.size()));

                            if (written < 0) {
                                auto _resolve = std::move(resolve);
                                ctx->eventloop()->stop_watcher(w);

                                _resolve();
                                return;
                            }

                            data = std::string_view(buffer.data(), written);
                        }
                    }
                });

                if (cancellation) {
                    (*cancellation) = [ctx, watcher]() -> future<void> {
                        co_await ctx->eventloop()->unwatch_fd(watcher);
                    };
                }
            });
        }
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
    }
    catch (...) {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif

        std::rethrow_exception(std::current_exception());
    }
}

manapi::future<> manapi::filesystem::write_async(const std::shared_ptr<async::context> &ctx, std::string_view path, std::string_view data, const unsigned int &mode, std::function<future<void>()> *cancellation) {
    auto cb = [data](void *buff, ssize_t buff_size) mutable -> ssize_t {
        if (data.empty()) {
            return -1;
        }

        buff_size = std::min(buff_size, static_cast<ssize_t>(data.size()));
        memcpy(buff, data.data(), buff_size);
        data = data.substr(buff_size);
        return buff_size;
    };

    return write_async(ctx, path, cb, mode, cancellation);
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
        MANAPIHTTP_LOG("The size of the file: {} is too large for read with this function.", size);
    }

    std::string content;
    content.resize(size);

    in.read (content.data(), size);

    return content;
}

manapi::future<> manapi::filesystem::read_async(const std::shared_ptr<async::context> &ctx, std::string_view path, std::function<ssize_t(const void *buff, ssize_t buff_size)> cb, std::function<future<void>()> *cancellation) {
#ifdef _WIN32
    HANDLE h = ::CreateFile(path.data(), GENERIC_READ, 0, 0, 0, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, "Failed to open file: handle = INVALID_HANDLE_VALUE");
    }
    auto fd = _open_osfhandle((intptr_t)h, _O_CREAT|_O_RDWR|_O_TRUNC);
    u_long arg = 1;
    ioctlsocket(fd, FIONBIO, &arg);
#else
    auto fd = ::open(path.data(), O_RDWR|O_NONBLOCK);
#endif
    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to read: fd = {}", fd);
    }

    try {
        std::string buffer;
        buffer.resize(BUFSIZ);

        co_await async::promise<void> (ctx, [cancellation, fd, ctx, &buffer, cb = std::move(cb)] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {
            auto watcher = co_await ctx->eventloop()->watch_fd(fd, ev::READ, [&buffer, resolve = std::move(resolve), reject = std::move(reject), ctx, cb = std::move(cb)] (ev::io &w, int revents) mutable  -> void {
                if (revents & ev::READ) {
                    auto rhs = ::read(w.fd, buffer.data(), buffer.size());

                    if (rhs < 0) {
                        auto _reject = std::move(reject);
                        ctx->eventloop()->stop_watcher(w);

                        _reject(std::make_exception_ptr(manapi::exception (ERR_FILE_IO, "read_async(...): Failed to read a file")));
                        return;
                    }

                    if (rhs == 0) {
                        auto _resolve = std::move(resolve);
                        ctx->eventloop()->stop_watcher(w);

                        _resolve();
                        return;
                    }

                    while (rhs) {
                        auto written = cb (buffer.data(), rhs);

                        if (written < 0) {
                            auto _reject = std::move(reject);
                            ctx->eventloop()->stop_watcher(w);

                            _reject(std::make_exception_ptr(manapi::exception (ERR_FILE_IO, std::format("read_async(...): cb returned {}", written))));
                            return;
                        }

                        rhs -= written;
                    }
                }
            });

            if (cancellation) {
                (*cancellation) = [ctx, watcher]() -> future<void> {
                    co_await ctx->eventloop()->unwatch_fd(watcher);
                };
            }
        });

        ::close(fd);
    }
    catch (...) {
        ::close(fd);

        std::rethrow_exception(std::current_exception());
    }
}

manapi::future<std::string> manapi::filesystem::read_async(const std::shared_ptr<async::context> &ctx, std::string_view path, std::function<future<void>()> *cancellation) {
    std::string data;

    co_await read_async(ctx, path, [&data] (const void *buff, ssize_t buff_size) -> ssize_t {
        try {
            data.append(static_cast<const char *>(buff), buff_size);
            return buff_size;
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("read_async(...) failed: {}", e.what());
            return -1;
        }
    }, cancellation);

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
    std::filesystem::path p (str);

    return std::filesystem::is_directory(p);
}

bool manapi::filesystem::is_file (const std::string &str) {
    return !is_dir (str);
}