#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstdarg>
#include <cstring>

#include "ManapiString.hpp"
#include "ManapiEventLoop.hpp"
#include "std/ManapiAsyncPromise.hpp"
#include "std/ManapiCancellation.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "std/ManapiBeforeDelete.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiDefaultErrors.hpp"

std::string_view manapi::fs::path::basename(std::string_view path) {
    std::size_t pos = path.find_last_of(std::filesystem::path::preferred_separator);
    if (pos != std::string::npos) {
        pos ++;
        return std::string_view{path.data() + pos, path.size() - pos};
    }
    return path;
}

std::string_view manapi::fs::path::extension(std::string_view path) {
    size_t pos = path.find_last_of('.');

    if (pos != std::string::npos) {
        pos ++;
        return std::string_view{path.data() + pos, path.size() - pos};
    }
    return {};
}


void manapi::fs::path::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
        path.push_back(std::filesystem::path::preferred_separator);
}

template<typename T>
bool async_fs_operation_result_error (std::shared_ptr<manapi::ev::fs> &w, typename manapi::async::promise_sync<T>::resolve_t &resolve, manapi::ctoken &cancellation) {
    cancellation.disable();
    ssize_t rhs;
    if ((rhs = w->result()) < 0) {
        resolve(manapi::ev::status_internal("filesystem error", static_cast<int>(rhs)));
        return true;
    }

    return false;
}

template<typename T>
using async_fs_operation_event_cb = std::move_only_function<void(std::shared_ptr<manapi::ev::fs>, typename manapi::async::promise_sync<T>::resolve_t&, manapi::ctoken &cancellation)>;

template<typename T>
void async_fs_operation_event_handler (std::shared_ptr<manapi::ev::fs> w, typename manapi::async::promise_sync<T>::resolve_t resolve, manapi::ctoken cancellation, async_fs_operation_event_cb<T> &event_cb) {
    try {
        event_cb(w, resolve, cancellation);
    }
    catch (std::bad_alloc const &) {
        resolve(manapi::ev::status_resource_exhausted());
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fs callback failed", e.what());
        resolve(manapi::ev::status_internal("fs callback failed", manapi::ev::ERR_UNKNOWN));
    }
}

template<typename T>
struct async_fs_operation_deleter {
    manapi::ctoken *c;
    async_fs_operation_event_cb<T> *event_cb;
    std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> *start_cb;

    ~async_fs_operation_deleter() {
        if (this->c) {
            this->c->cancel();
        }
    }
};

template<typename T>
manapi::future<T> async_fs_operation (std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb,
      async_fs_operation_event_cb<T> event_cb,  manapi::ctoken cancellation) {
    typedef manapi::async::promise_sync<T> promise_sync;
    async_fs_operation_deleter<T> t {};

    t.c = &cancellation;
    t.event_cb = &event_cb;
    t.start_cb = &start_cb;

    try {
        co_return co_await promise_sync([&t] (typename promise_sync::resolve_t resolve, typename promise_sync::reject_t reject)
            -> void {
            auto watcher_res = manapi::async::current()->eventloop()->create_watcher_fs([resolve, event_cb = std::move(*t.event_cb), cancellation = *t.c] (const std::shared_ptr<manapi::ev::fs> &w) mutable
                -> void {
                async_fs_operation_event_handler<T>(w, std::move(resolve), std::move(cancellation), event_cb);
            });

            manapi::ev::shared_fs watcher;

            if (watcher_res)
                watcher = watcher_res.unwrap();

            if (!watcher_res || !((*t.start_cb)(watcher))) {
                resolve(manapi::ev::status_internal("fs i/o init watcher failed", manapi::ev::ERR_UNKNOWN));
                return;
            }

            if (t.c->contains_cancel_callback()) {
                t.c->cancel_callback([watcher = std::move(watcher), resolve = std::move(resolve)] () mutable
                    -> void {
                    manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(watcher));
                    resolve (manapi::ev::status_cancelled("fs i/o operation has been cancelled"));
                });
            }
        });
    }
    catch (std::bad_alloc const &) {
        co_return manapi::ev::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "fs operation failed", e.what());
        co_return manapi::ev::status_internal("fs operation failed", manapi::ev::ERR_UNKNOWN);
    }
}

manapi::future<manapi::ev::status> async_fs_simple_operation (std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb, manapi::ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status>(std::move(start_cb),
        [](std::shared_ptr<manapi::ev::fs> w,
            promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            if (async_fs_operation_result_error<manapi::ev::status>(w, resolve, cancel))
                return;

            resolve(manapi::ev::status_ok());
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<bool>> manapi::fs::async_exists(std::string path, manapi::ctoken cancellation) {
    bool exists = false;

    auto res = co_await manapi::fs::async_stat(std::move(path),
        [&exists] (ev::stat_t *st) -> void {
        exists = (st->st_mode & ev::IFMT);
    }, std::move(cancellation));

    if (res.ok())
        co_return exists;

    if (res.syserr() == ev::ERR_NOENT)
        co_return false;

    co_return std::move(res);
}

manapi::future<manapi::ev::status_or<std::chrono::system_clock::time_point>> manapi::fs::async_last_time_write(std::string path, manapi::ctoken cancellation) {
    ev::status res;
    try {
#if MANAPHTTP_UV_SINCE_AT(1,45,0)
        uv_timespec64_t mtime;
#else
        uv_timespec_t mtime;
#endif
        res = co_await manapi::fs::async_stat(std::move(path), [&mtime] (ev::stat_t *stat)
            -> void {
            assert(stat);
            if (stat) {
                mtime.tv_nsec = stat->st_mtim.tv_nsec;
                mtime.tv_sec = stat->st_mtim.tv_sec;
            }
        }, std::move(cancellation));

        if (!res.ok())
            goto err;

        /**
         * Windows doesn't accept std::nano in the time_point,
         * so we need to remove it.
         *
         * also we can continue using it in the Linux system
         * to saving accuracy
         */
        co_return std::chrono::system_clock::time_point (std::chrono::seconds{mtime.tv_sec}/* + std::chrono::nanoseconds{mtime.tv_nsec} */);
    }
    catch (std::bad_alloc const &) {
        res = ev::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "failed", e.what());
        res = ev::status_internal("failed", ev::ERR_UNKNOWN);
    }
err:
    co_return std::move(res);
}

manapi::future<manapi::ev::status> manapi::fs::async_mkdir(std::string path, int mode, bool recursive, manapi::ctoken cancellation) {
    typedef manapi::async::promise_sync<ev::status> promise_sync;

    if (recursive) {
        auto parts = manapi::string::split(path, path::delimiter);
        std::size_t size = 0;
        for (const auto &part : parts) {
            size += 1;

            if (part.empty())
                continue;

            size += part.size();

            ctoken cancellation2 = ctoken::unit (cancellation);

            auto err = co_await async_fs_operation<manapi::ev::status>([path = path.substr(0, size), mode, cancellation2, cancellation](std::shared_ptr<ev::fs> w) mutable
                -> bool {
                    return !w->mkdir(path.data(), mode);
                },
                +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
                -> void {
                    auto rhs = w->result();
                    if (rhs != ev::ERR_EXIST && rhs != ev::ERR_PERM) {
                        if (async_fs_operation_result_error<manapi::ev::status>(w, resolve, cancel)) {
                            return;
                        }
                    }
                    resolve(ev::status_ok());
                }, cancellation2);
            if (!err.ok())
                co_return std::move(err);
        }
        co_return ev::status_ok();
    }
    else {
        co_return co_await async_fs_operation<manapi::ev::status>([path = std::move(path), mode](std::shared_ptr<ev::fs> w)
            -> bool {
                return !(w->mkdir(path.data(), mode));
            },
            +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
            -> void {
                auto rhs = w->result();
                if ((rhs != ev::ERR_EXIST&&rhs != ev::ERR_PERM)) {
                    if (async_fs_operation_result_error<manapi::ev::status>(w, resolve, cancel)) {
                        return;
                    }
                }
                resolve(ev::status_ok());
            }, cancellation);
    }
}

manapi::future<manapi::ev::status_or<manapi::ev::file>> manapi::fs::async_open(std::string path, int flags, int mode, manapi::ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<ev::file>> promise_sync;
    
    auto fileno = co_await async_fs_operation<manapi::ev::status_or<ev::file>>([path = std::move(path), flags, mode] (std::shared_ptr<ev::fs> w)
        -> bool {
        return !w->open(path.data(), flags, mode);
    }, +[] (std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
    -> void {
        if (async_fs_operation_result_error<manapi::ev::status_or<ev::file>>(w, resolve, cancel)) {
            return;
        }
        resolve (static_cast<manapi::ev::file>(w->result()));
    }, cancellation);
    co_return fileno;
}

manapi::future<manapi::ev::status> manapi::fs::async_close(ev::file file, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "fs:fd %d close", file);

    co_return co_await async_fs_operation<manapi::ev::status>([file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->close(file);
        }, [file](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
        -> void {
            if (async_fs_operation_result_error<manapi::ev::status>(w, resolve, cancel)) {
                manapi_log_error("fs:fd %d close failed due to %s",
                    file, ev::strerror(w->result()));
                return;
            }
            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "fs:fd %d finished", file);
            resolve(ev::status_ok());
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_write(ev::file file, const void *data, ssize_t size, int64_t offset, manapi::ctoken cancellation) {
    ev::buff_t buff;
    buff.base = (char *)(data);
    buff.len = static_cast<std::size_t>(size);
    co_return co_await async_write(file, &buff, 1,  offset, std::move(cancellation));
}

struct async_read_data_t {
    ssize_t result;
    manapi::ev::buff_t *buff;
    uint32_t nbuff;
    manapi::ev::file file;
    int64_t offset;
    async_fs_operation_event_cb<manapi::ev::status_or<ssize_t>> event_cb;
};

struct async_write_data_t {
    ssize_t result;
    manapi::ev::buff_t *buff;
    uint32_t nbuff;
    manapi::ev::file file;
    int64_t offset;
    async_fs_operation_event_cb<manapi::ev::status_or<ssize_t>> event_cb;
};

struct fileno_deleter {
    manapi::ev::file fileno;

    ~fileno_deleter() {
        std::move_only_function<void(std::exception_ptr, manapi::ev::status *)> cb;

        auto b = manapi::ctokens::timeout(64000);
        MANAPIHTTP_MUST_ALLOC_START
        cb = [fileno = this->fileno] (std::exception_ptr err, manapi::ev::status *s)
            -> void {
            if (err) {
                int errnum;
                char msg[256];
                std::size_t msg_size = sizeof (msg);
                std::string_view constexpr status = "exception";

                manapi::extract_exception_ptr(std::move(err), &errnum, msg, &msg_size);
                manapi_log_error("fs(%.*s):fd %d close failed %.*s",
                    status.size(), status.data(),
                    fileno, msg_size, msg);
            }
            else {
                if (s->ok()) {
                    return;
                }

                auto const status = s->status_msg();
                auto const msg = s->msg();
                manapi_log_error("fs(%.*s):fd %d close failed %.*s",
                     status.size(), status.data(),
                    fileno, msg.size(), msg.data());
            }
        };
        MANAPIHTTP_MUST_ALLOC_END

        manapi::future<manapi::ev::status> task(nullptr);

        MANAPIHTTP_MUST_ALLOC_START
        task = manapi::fs::async_close(this->fileno, b);
        MANAPIHTTP_MUST_ALLOC_END

        manapi::async::run<manapi::ev::status>(std::move(task), std::move(cb));
    }
};

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_read(ev::file file, void *data, ssize_t size, int64_t offset, manapi::ctoken cancellation) {
    ev::buff_t buff;
    buff.base = static_cast<char*>(data);
    buff.len = size;
    co_return co_await async_read (file, &buff, 1, offset, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_write(std::string path, std::string data, int mode, int flags, int64_t offset, manapi::ctoken cancellation) {
    ev::file fileno;
    {
        auto fileno_res = co_await async_open(path, flags, mode, manapi::ctoken::unit(cancellation));
        if (!fileno_res.ok())
            co_return fileno_res.err();
        fileno = fileno_res.unwrap();
    }
    fileno_deleter deleter (fileno);
    auto res = co_await async_write(fileno, data.data(),
        static_cast<ssize_t>(data.size()), offset, manapi::ctoken::unit(cancellation));
    if (!res.ok())
        co_return std::move(res.err());
    auto rhs = res.unwrap();
    if (rhs != data.size())
        co_return ev::status_internal("size isn't the same", ev::ERR_UNKNOWN);
    co_return ev::status_ok();
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_read(std::string path, int flags, int64_t offset, manapi::ctoken cancellation) {
    ev::file fileno;
    std::string data;
    ssize_t len = 0;

    {
        auto fileno_res = co_await async_open(path, flags, 0, manapi::ctoken::unit(cancellation));
        if (!fileno_res.ok())
            co_return fileno_res.err();
        fileno = fileno_res.unwrap();
    }

    fileno_deleter deleter (fileno);

    auto res = co_await async_fstat(fileno, [&len] (ev::stat_t *stat)
        -> void {
        len = stat ? static_cast<ssize_t>(stat->st_size) : -1;
    }, manapi::ctoken::unit(cancellation));

    if (!res.ok())
        co_return std::move(res);

    if (len == -1)
        co_return ev::status_not_found("not found");

    data.resize(len);

    auto rhs = co_await async_read(fileno, data.data(), len, offset, manapi::ctoken::unit(cancellation));
    if (!rhs.ok())
        co_return rhs.err();

    if (rhs.unwrap() != len)
        co_return ev::status_internal("size isn't the same", ev::ERR_UNKNOWN);

    co_return std::move(data);
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_write(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<ssize_t>> promise_sync;

    if (!nbuff)
        co_return 0;

    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        auto rhs = ev::fs::try_write(file, buff->base + shift, buff->len - shift, offset);

        if (rhs < 0) {
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else {
                if (res)
                    rhs = 0;
                else
                    co_return rhs;
            }
        }

        if (!rhs)
            break;

        if (offset >= 0)
            offset += rhs;

        res += rhs;
        shift += rhs;

        if (shift == buff[0].len) {
            nbuff--;
            buff++;
            shift = 0;
        }
    }

    if (res)
        co_return static_cast<ssize_t>(res);

    async_write_data_t dd{};

    buff->base += shift;
    buff->len -= shift;

    dd.event_cb = nullptr;
    dd.buff = buff;
    dd.result = res;
    dd.nbuff = nbuff;
    dd.file = file;
    dd.offset = offset;
    dd.event_cb = [&dd](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
        -> void {
        try {
            if (async_fs_operation_result_error<ev::status_or<ssize_t>>(w, resolve, cancel))
                return;

            auto rhs = w->result();

            if (rhs < 0) {
                /* ignore ? */
                if (dd.result) {
                    rhs = dd.result;
                }
                cancel.disable();
                resolve(static_cast<ssize_t>(rhs));
                return;
            }

            dd.result += rhs;

            if (dd.offset >= 0)
                dd.offset += rhs;

            while (dd.nbuff
                && rhs >= dd.buff->len) {
                rhs -= dd.buff->len;
                dd.buff++;
                dd.nbuff--;
                }

            if (dd.nbuff) {
                /* retry */
                auto wres = manapi::async::current()->eventloop()->create_watcher_fs(
                    [&dd, resolve, cancel] (std::shared_ptr<ev::fs> w) mutable
                    -> void {
                    async_fs_operation_event_handler<manapi::ev::status_or<ssize_t>>(std::move(w), std::move(resolve), std::move(cancel), dd.event_cb);
                });

                if (!wres) {
                    resolve(wres.err());
                    return;
                }

                auto w1 = wres.unwrap();

                dd.buff->base += rhs;
                dd.buff->len -= rhs;

                if (w1->write(dd.file, dd.buff, dd.nbuff, dd.offset)) {
                    resolve(ev::status_internal("fs i/o init watcher failed", ev::ERR_UNKNOWN));
                    return;
                }

                if (cancel.contains_cancel_callback()) {
                    cancel.cancel_callback([w = std::move(w1)] () mutable
                        -> void { manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
                }
            }
            else {
                cancel.disable();
                resolve(static_cast<ssize_t>(dd.result));
            }
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "async_write:Failed", e.what());
            resolve(ev::status_internal("async_write:Failed", ev::ERR_UNKNOWN));
        }
    };

    auto status = co_await async_fs_operation<manapi::ev::status_or<ssize_t>>([&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->write(dd.file, dd.buff, dd.nbuff, dd.offset);
        }, [&dd] (std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) -> void {
            dd.event_cb(std::move(w), resolve, cancel);
        }, cancellation);

    if (!status.ok())
        co_return status.err();

    res = status.unwrap();

    co_return res;
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_read(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<ssize_t>> promise_sync;

    if (!nbuff)
        co_return 0;

    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        ssize_t rhs = ev::fs::try_read(file, buff->base + shift, buff->len - shift, offset);

        if (rhs < 0) {
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else {
                if (res)
                    rhs = 0;
                else
                    co_return rhs;
            }
        }

        if (!rhs)
            break;

        if (offset >= 0)
            offset += rhs;

        shift += rhs;
        res += rhs;

        if (shift == buff->len) {
            shift = 0;
            buff++;
            nbuff--;
        }
    }

    if (res || !nbuff)
        co_return res;

    async_read_data_t dd {};

    buff->base += shift;
    buff->len -= shift;

    dd.result = res;
    dd.file = file;
    dd.offset = offset;
    dd.nbuff = nbuff;
    dd.buff = buff;

    dd.event_cb = [&dd](std::shared_ptr<ev::fs> wlocal, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
        -> void {
        try {
            if (async_fs_operation_result_error<manapi::ev::status_or<ssize_t>>(wlocal, resolve, cancel)) {
                return;
            }

            auto rhs = wlocal->result();

            if (rhs < 0) {
                if (dd.result) {
                    rhs = dd.result;
                }
                cancel.disable();
                resolve(static_cast<ssize_t>(rhs));
                return;
            }

            if (dd.offset >= 0)
                dd.offset += rhs;

            dd.result += rhs;

            while (dd.nbuff
                && rhs >= dd.buff->len) {
                rhs -= dd.buff->len;
                dd.nbuff--;
                dd.buff++;
                }

            if (dd.nbuff && rhs) {
                /* retry */
                auto wres  = manapi::async::current()->eventloop()->create_watcher_fs([&dd, resolve, cancel] (std::shared_ptr<ev::fs> w) mutable
                    -> void {
                    async_fs_operation_event_handler<manapi::ev::status_or<ssize_t>>( std::move(w), std::move(resolve), std::move(cancel), dd.event_cb);
                });

                if (!wres) {
                    cancel.disable();
                    resolve(wres.err());
                    return;
                }

                auto w = wres.unwrap();

                dd.buff->base += rhs;
                dd.buff->len -= rhs;

                if (w->read(dd.file, dd.buff, dd.nbuff, dd.offset)) {
                    resolve(ev::status_internal("fs i/o init watcher failed", ev::ERR_UNKNOWN));
                    return;
                }

                if (cancel.contains_cancel_callback()) {
                    cancel.cancel_callback([w = std::move(w)] () mutable
                        -> void { manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
                }
            }
            else {
                cancel.disable();
                resolve(static_cast<ssize_t>(dd.result));
            }
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "async_read:Failed", e.what());
            cancel.disable();
            resolve(ev::status_internal("async_read:Failed", ev::ERR_UNKNOWN));
        }
    };

    auto status = co_await async_fs_operation<ev::status_or<ssize_t>>([&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->read(dd.file, dd.buff, dd.nbuff, dd.offset);
        }, [&dd](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel)
        -> void {
            dd.event_cb(std::move(w), resolve, cancel);
        }, cancellation);

    if (!status.ok())
        co_return status.err();

    res = status.unwrap();

    co_return res;
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_write(ev::file file, slice_view slice, int64_t offset, ctoken cancellation) {
    auto buffs = slice.slices_buffs();
    co_return co_await async_write(file, buffs.get(), slice.slices_size(), offset, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_read(ev::file file, slice_view slice, int64_t offset, ctoken cancellation) {
    auto buffs = slice.slices_buffs();
    co_return co_await async_read(file, buffs.get(), slice.slices_size(), offset, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<ssize_t>> manapi::fs::async_file_size(std::string path, manapi::ctoken cancellation) {
    ssize_t size;

    co_await fs::async_stat(std::move(path), [&size] (ev::stat_t *stat)
        -> void {
        if (stat) {
            size = static_cast<ssize_t>(stat->st_size);
        }
        else {
            size = -1;
        }
    }, std::move(cancellation));

    if (size == -1)
        co_return ev::status_not_found("file not found");

    co_return size;
}

manapi::future<manapi::ev::status> manapi::fs::async_stat(std::string path, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->stat(path.data());
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();

            if (w->result()) {
                resolve(ev::status_internal("async_stat failed", w->result()));
                return;
            }

            if (callback) {
                callback(&w->custom()->statbuf);
            }
            resolve(ev::status_ok());
        }, cancellation);
}

manapi::future<manapi::ev::status> manapi::fs::async_fstat(ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status>([file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->fstat(file);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();

            if (w->result()) {
                resolve(ev::status_internal("async_fstat failed", w->result()));
                return;
            }

            if (callback) {
                callback(&w->custom()->statbuf);
            }
            resolve(ev::status_ok());
        }, cancellation);
}

static void clean_delimiters_at_end (std::string_view &str) {
    // clean delimiters at the end
    while (!str.empty() && str.back() == manapi::fs::path::delimiter)
        str = str.substr(0, str.size() - 1);
}

std::string_view manapi::fs::path::back (std::string_view str) {
    clean_delimiters_at_end(str);
    auto it = str.rfind(fs::path::delimiter);
    if (it != std::string_view::npos)
        str = str.substr(0, it);
    return str;
}

static void append_ (std::string &path, std::string_view next, bool delimiter, bool root) {
    using namespace manapi::fs;

    if (delimiter) {
        while (true) {
            auto it = next.find(path::delimiter);
            if (it == std::string_view::npos)
                break;
            if (it) {
                append_(path, next.substr(0, it), false, root && path.empty());
            }
            else if (path.empty() || path.back() != path::delimiter) {
                path.push_back(path::delimiter);
            }
            next = next.substr(it + 1);
        }
    }

    if (next.empty())
        return;

    if (next == ".") {
        if (path.empty() && root) {
            path = path::current_path();
        }

        return;
    }

    if (next == "..") {
        if (path.empty() && root) {
            path = path::current_path();
        }

        auto it = path.rfind(path::delimiter);
        if (it == std::string_view::npos) {
            path.clear();
        }
        else {
            if (!it)
                it++;
            path.resize(it);
        }
    }
    else {
        if (!path.empty() && path.back() != path::delimiter)
            path.push_back(path::delimiter);
        clean_delimiters_at_end(next);
        path.append(next.data(), next.size());
    }
}


void manapi::fs::path::append(std::string &path, std::string_view next) {
    path::append(path, next, false);
}

void manapi::fs::path::append(std::string &path, std::string_view next, bool root) {
    auto const c_size = path.size();
#ifdef _MSC_VER 
    char *c = static_cast<char*>(alloca(c_size));
#else
    char c[c_size];
#endif
    memcpy (c, path.data(), c_size);
    path.resize(0);
    append_(path, std::string_view(c, c_size), true, root);
    append_(path, next, true, false);
}

std::string manapi::fs::path::current_path() {
    return std::filesystem::current_path().string();
}

std::string manapi::fs::path::serialize (std::string_view str) {
    std::string path;
    append_ (path, str, true, true);
    return std::move(path);
}

std::string manapi::fs::path::absolute(std::string_view path) {
    return std::filesystem::absolute(path).string();
}

std::string manapi::fs::path::root_directory() {
    return std::filesystem::current_path().root_directory().string();
}

manapi::future<manapi::ev::status> manapi::fs::async_unlink (std::string path, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->unlink(path.data()); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_rmdir (std::string path, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rmdir(path.data()); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_closedir (manapi::ev::dir_t *directory, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([directory] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->closedir(directory); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_statfs (std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->statfs(path.data());
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();

            if (w->result()) {
                resolve(ev::status_internal("async_statfs failed", w->result()));
                return;
            }

            if (callback) {
                callback(static_cast<ev::statfs_t*>(w->custom()->ptr));
            }
            resolve(ev::status_ok());
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_rename (std::string oldpath, std::string newpath, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([oldpath = std::move(oldpath), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rename(oldpath.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_copyfile (std::string src, std::string dest, int flags, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([src = std::move(src), dest = std::move(dest), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->copyfile(src.data(), dest.data(), flags); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_chmod (std::string path, int mode, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chmod(path.data(), mode); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fchmod (ev::file file, int mode, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([file, mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchmod(file, mode); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_access (std::string path, int mode, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status>([path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->access(path.data(), mode);
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (w->result())
                resolve(ev::status_internal("access failed", w->result()));
            else
                resolve(ev::status_ok());
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_utime (std::string path, double atime, double mtime, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path), atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->utime(path.data(), atime, mtime); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_futime (ev::file file, double atime, double mtime, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([file, atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->futime(file, atime, mtime); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_link (std::string path, std::string newpath, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->link(path.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_symlink (std::string path, std::string newpath, int flags, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path = std::move(path), newpath = std::move(newpath), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->symlink(path.data(), newpath.data(), flags); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fsync (ev::file file, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fsync(file); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fdatasync(ev::file file, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fdatasync(file); }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<manapi::ev::dir_t *>> manapi::fs::async_opendir(std::string path, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<manapi::ev::dir_t *>> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status_or<manapi::ev::dir_t *>>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->opendir(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<manapi::ev::status_or<ev::dir_t *>>(w, resolve, cancel))
                return;
            resolve(static_cast<ev::dir_t *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_readlink (std::string path, ctoken cancellation) {
    typedef manapi::async::promise_sync<ev::status_or<std::string>> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status_or<std::string>>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readlink(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<manapi::ev::status_or<std::string>>(w, resolve, cancel))
                return;
            resolve(std::string{static_cast<const char *> (w->custom()->ptr)});
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_realpath (std::string path, ctoken cancellation) {
    typedef manapi::async::promise_sync<ev::status_or<std::string>> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status_or<std::string>>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->realpath(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<manapi::ev::status_or<std::string>>(w, resolve, cancel)) {
                return;
            }
            resolve(std::string{static_cast<const char *> (w->custom()->ptr)});
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_chown(std::string path, ev::uid_t uid, ev::gid_t gid, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([path=std::move(path), uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chown(path.data(), uid, gid); }, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fchown(ev::file file, ev::uid_t uid, ev::gid_t gid, ctoken cancellation) {
    co_return co_await async_fs_simple_operation ([file, uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchown(file, uid, gid); }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_mkdtemp(std::string tpl, ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<std::string>> promise_sync;

    co_return co_await async_fs_operation<manapi::ev::status_or<std::string>>([tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkdtemp(tpl.data());
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<manapi::ev::status_or<std::string>>(w, resolve, cancel)) {
                return;
            }
            resolve(std::string{w->custom()->path});
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::pair<std::string, manapi::ev::file>>> manapi::fs::async_mkstemp(std::string tpl, ctoken cancellation) {
    typedef ev::status_or<std::pair<std::string, manapi::ev::file>> val;
    typedef manapi::async::promise_sync<val> promise_sync;

    co_return co_await async_fs_operation<val>([tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkstemp(tpl.data());
        }, +[](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<val>(w, resolve, cancel))
                return;
            resolve(std::make_pair(std::string{w->custom()->path}, static_cast<ev::file>(w->custom()->result)));
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::size_t>> manapi::fs::async_scandir (std::string path, int flags, std::move_only_function<void(ev::dir_t *dir, std::size_t result)> callback, ctoken cancellation) {
    typedef manapi::async::promise_sync<ev::status_or<std::size_t>> promise_sync;

    co_return co_await async_fs_operation<ev::status_or<std::size_t>>([path = std::move(path), flags] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->scandir(path.data(), flags);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<ev::status_or<std::size_t>>(w, resolve, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr, w->result());
            resolve(w->result());
        }, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::size_t>> manapi::fs::async_readdir (ev::dir_t *dir, std::move_only_function<void(ev::dir_t *, std::size_t)> callback, ctoken cancellation) {
    typedef manapi::async::promise_sync<ev::status_or<std::size_t>> promise_sync;

    co_return co_await async_fs_operation<ev::status_or<std::size_t>>([dir] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readdir(dir);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise_sync::resolve_t &resolve, manapi::ctoken &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<ev::status_or<std::size_t>>(w, resolve, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr, w->result());
            resolve(w->result());
        }, std::move(cancellation));
}