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
#include "ManapiString.hpp"

static const std::string folder_configs;

#define MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE 4096LL

static const char *fs_err_msgs[] = {
    "failure of fs i/o operations: {}",
    "failure of fs callback: {}",
    "fs i/o operation has been cancelled",
    "fs i/o init watcher failure"
};

enum fs_err_codes {
    FS_ERR_FAILURE_FS_IO_OPERATIONS = 0,
    FS_ERR_FAILURE_CALLBACK,
    FS_ERR_CANCELLED,
    FS_ERR_FAILURE_INIT
};

std::string manapi::filesystem::path::basename(const std::string& path) {
    size_t pos = path.find_last_of(std::filesystem::path::preferred_separator);

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return path;
}

std::string manapi::filesystem::path::extension(const std::string& path) {
    size_t pos = path.find_last_of('.');

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return "";
}


void manapi::filesystem::path::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
    {
        path.push_back(std::filesystem::path::preferred_separator);
    }
}

template<typename T>
bool async_fs_operation_result_error (std::shared_ptr<manapi::ev::fs> &w, manapi::async::context *ctx, typename manapi::async::promise<T>::reject_t &reject, manapi::async::cancellation_action &cancellation) {
    if (w->result() < 0) {
        cancellation.disable_cancellation();
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FS_IO_RESULT, fs_err_msgs[FS_ERR_FAILURE_FS_IO_OPERATIONS], w->result())));
        return true;
    }

    return false;
}

template<typename T>
using async_fs_operation_event_cb =  std::move_only_function<void(manapi::async::context *ctx, std::shared_ptr<manapi::ev::fs>, typename manapi::async::promise<T>::resolve_t&, typename manapi::async::promise<T>::reject_t&, manapi::async::cancellation_action &cancellation)>;

template<typename T>
void async_fs_operation_event_handler (manapi::async::context*ctx, std::shared_ptr<manapi::ev::fs> w, typename manapi::async::promise<T>::resolve_t resolve, typename manapi::async::promise<T>::reject_t reject,
    manapi::async::cancellation_action cancellation, async_fs_operation_event_cb<T> &event_cb) {
    try {
        event_cb(ctx, w, resolve,reject, cancellation);
    }
    catch (std::exception const &e) {
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FS_IO, fs_err_msgs[FS_ERR_FAILURE_CALLBACK], e.what())));
    }

    ctx->eventloop()->stop_watcher(w);
}

struct async_fs_operation_deleter {
    manapi::async::cancellation_action c;
    ~async_fs_operation_deleter() {
        this->c.cancel();
    }
};

template<typename T>
manapi::future<T> async_fs_operation (std::shared_ptr<manapi::async::context> ctx, std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb,
      async_fs_operation_event_cb<T> event_cb,  manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<T>;
    async_fs_operation_deleter t {cancellation};

    co_return co_await promise(ctx, [&] (promise::resolve_t resolve, promise::reject_t reject)
        -> manapi::future<> {
        co_await ctx->eventloop()->custom_callback([ctx, start_cb = std::move(start_cb), event_cb = std::move(event_cb),
            resolve = std::move(resolve), reject = std::move(reject), cancellation] (manapi::event_loop *ev) mutable
            -> void {
            auto watcher = ev->create_watcher_fs([ctx, resolve = std::move(resolve), reject, event_cb = std::move(event_cb), cancellation] (std::shared_ptr<manapi::ev::fs> &w) mutable
                -> void {
                async_fs_operation_event_handler<T>(ctx.get(), w, std::move(resolve), std::move(reject), std::move(cancellation), event_cb);
            });

            if (!start_cb(watcher)) {
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO, fs_err_msgs[FS_ERR_FAILURE_INIT])));
            }

            if (cancellation.contains_cancel_callback()) {
                cancellation.cancel_callback([watcher = std::move(watcher), reject = std::move(reject)] (std::shared_ptr<manapi::async::context> ctx) mutable
                    -> void {
                    ctx->eventloop()->stop_watcher(std::move(watcher));
                    reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_CANCELLED, fs_err_msgs[FS_ERR_CANCELLED])));
                });
            }
        });
    });
}

manapi::future<bool> manapi::filesystem::async_exists(std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation) {
    bool result = false;
    co_await filesystem::async_stat(std::move(ctx), std::move(path), [&result] (ev::stat_t *stat)
        -> void { result = stat != nullptr; }, std::move(cancellation));
    co_return result;
}

manapi::future<void> manapi::filesystem::async_mkdir(std::shared_ptr<manapi::async::context> ctx, std::string path, int mode, bool recursive, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    if (recursive) {
        auto parts = manapi::string::split(path, path::delimiter);
        std::size_t size = 0;
        for (const auto &part : parts) {
            size += part.size();

            async::cancellation_action cancellation2 (ctx);
            cancellation2.ask_cancel_callback();

            co_await async_fs_operation<void>(ctx,
                [path = path.substr(size), mode, cancellation2, cancellation](std::shared_ptr<ev::fs> w) mutable
                -> bool {
                    if (w->mkdir(path.data(), mode)) {
                        return false;
                    }

                    cancellation2.cancel_callback(std::move(cancellation));

                    return true;
                },
                +[](manapi::async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
                -> void {
                    cancel.disable_cancellation();
                    if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                        return;
                    }
                    resolve();
                }, cancellation2);
        }
    }
    else {
        co_await async_fs_operation<void>(ctx,
            [path = std::move(path), mode](std::shared_ptr<ev::fs> w)
            -> bool {
                return !(w->mkdir(path.data(), mode));
            },
            +[](manapi::async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
            -> void {
                cancel.disable_cancellation();
                if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                    return;
                }
                resolve();
            }, cancellation);
    }
}

manapi::future<manapi::ev::file> manapi::filesystem::async_open(std::shared_ptr<manapi::async::context> ctx, std::string path, int flags, int mode, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ev::file>;

    auto fileno = co_await async_fs_operation<ev::file>(ctx,
        [path = std::move(path), flags, mode] (std::shared_ptr<ev::fs> w)
        -> bool {
        return !w->open(path.data(), flags, mode);
    }, +[] (async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
    -> void {
        cancel.disable_cancellation();
        if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
            return;
        }
        resolve (w->custom()->file);
    }, cancellation);

    co_return fileno;
}

manapi::future<void> manapi::filesystem::async_close(std::shared_ptr<manapi::async::context> ctx, ev::file file,
    async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_await async_fs_operation<void>(ctx,
        [file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->close(file);
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve();
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_write(std::shared_ptr<manapi::async::context> ctx, ev::file file, const void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    struct async_write_data_t {
        ssize_t result;
        uv_buf_t buff[1];
        ev::file file;
        int64_t offset;
        async::context *ctx;
        async_fs_operation_event_cb<ssize_t> event_cb;
    } dd {
        0,
        {{(char *)data, static_cast<std::size_t>(size)}},
        file,
        offset,
        ctx.get(),
        nullptr
    };

    dd.event_cb = [&dd](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
        if (async_fs_operation_result_error<ssize_t>(w, ctx, reject, cancel)) {
            return;
        }

        auto rhs = w->result();

        if (rhs < 0) {
            if (dd.result) {
                rhs = dd.result;
            }
            cancel.disable_cancellation();
            resolve(rhs);
            return;
        }

        dd.result += rhs;

        if (rhs < dd.buff->len) {
            /* retry */
            auto w = dd.ctx->eventloop()->create_watcher_fs([&dd, resolve, reject, cancel] (std::shared_ptr<ev::fs> w) mutable
                -> void {
                async_fs_operation_event_handler<ssize_t>(dd.ctx,  std::move(w), std::move(resolve), std::move(reject), std::move(cancel), dd.event_cb);
            });

            if (dd.offset >= 0) {
                dd.offset += rhs;
            }

            dd.buff->base += rhs;
            dd.buff->len -= rhs;

            if (w->write(dd.file, dd.buff, 1, dd.offset)) {
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO, fs_err_msgs[FS_ERR_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w)] (std::shared_ptr<async::context> ctx) mutable
                    -> void { ctx->eventloop()->stop_watcher(std::move(w)); });
            }
        }
        else {
            cancel.disable_cancellation();
            resolve(dd.result);
        }
    };

    auto rhs = co_await async_fs_operation<ssize_t>(ctx,
        [&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->write(dd.file, dd.buff, 1, dd.offset);
        }, [&dd] (async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) -> void {
            dd.event_cb(ctx, std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return rhs;
}

manapi::future<ssize_t> manapi::filesystem::async_read(std::shared_ptr<manapi::async::context> ctx, ev::file file, const void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    struct async_read_data_t {
        ssize_t result;
        uv_buf_t buff[1];
        ev::file file;
        int64_t offset;
        async::context *ctx;
        async_fs_operation_event_cb<ssize_t> event_cb;
    } dd {
            0,
            {{(char *)data, static_cast<std::size_t>(size)}},
            file,
            offset,
            ctx.get(),
            nullptr
        };

    dd.event_cb = [&dd](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
        if (async_fs_operation_result_error<ssize_t>(w, ctx, reject, cancel)) {
            return;
        }

        auto rhs = w->result();

        if (rhs < 0) {
            if (dd.result) {
                rhs = dd.result;
            }
            cancel.disable_cancellation();
            resolve(rhs);
            return;
        }

        dd.result += rhs;

        if (rhs < dd.buff->len) {
            /* retry */
            auto w = dd.ctx->eventloop()->create_watcher_fs([&dd, resolve, reject, cancel] (std::shared_ptr<ev::fs> w) mutable
                -> void {
                async_fs_operation_event_handler<ssize_t>(dd.ctx,  std::move(w), std::move(resolve), std::move(reject), std::move(cancel), dd.event_cb);
            });

            if (dd.offset >= 0) {
                dd.offset += rhs;
            }

            dd.buff->base += rhs;
            dd.buff->len -= rhs;

            if (w->read(dd.file, dd.buff, 1, dd.offset)) {
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO, fs_err_msgs[FS_ERR_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w)] (std::shared_ptr<async::context> ctx) mutable
                    -> void { ctx->eventloop()->stop_watcher(std::move(w)); });
            }
        }
        else {
            cancel.disable_cancellation();
            resolve(dd.result);
        }
    };

    auto rhs = co_await async_fs_operation<ssize_t>(ctx,
        [&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->read(dd.file, dd.buff, 1, dd.offset);
        }, [&dd](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
            dd.event_cb(ctx, std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return rhs;
}

manapi::future<ssize_t> manapi::filesystem::async_file_size(std::shared_ptr<manapi::async::context> ctx, std::string path, manapi::async::cancellation_action cancellation) {
    ssize_t size;

    co_await filesystem::async_stat(std::move(ctx), std::move(path), [&size] (ev::stat_t *stat)
        -> void {
        if (stat) {
            size = stat->st_size;
        }
        else {
            size = -1;
        }
    }, std::move(cancellation));

    if (size == -1) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, "error when getting the file size");
    }

    co_return size;
}

manapi::future<> manapi::filesystem::async_stat(std::shared_ptr<async::context> ctx, std::string path, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_await async_fs_operation<void>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->stat(path.data());
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (w->result()) {
                callback(nullptr);
            }
            else {
                callback(&w->custom()->statbuf);
            }

            resolve();
        }, cancellation);
}

manapi::future<> manapi::filesystem::async_fstat(std::shared_ptr<async::context> ctx, ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_await async_fs_operation<void>(ctx,
        [file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->fstat(file);
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (w->result()) {
                callback(nullptr);
            }
            else {
                callback(&w->custom()->statbuf);
            }

            resolve();
        }, cancellation);
}

std::string manapi::filesystem::path::back (std::string str) {
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

std::string manapi::filesystem::path::clean (const std::string &str) {
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