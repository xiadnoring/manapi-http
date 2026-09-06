#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstdarg>
#include <cstring>

#include "ManapiString.hpp"
#include "ManapiEventLoop.hpp"
#include "std/ManapiPromise.hpp"
#include "std/ManapiCancelToken.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "std/ManapiBeforeDelete.hpp"
#include "std/ManapiEasyCancelToken.hpp"
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

static std::vector<std::size_t> manapi__fs_find_all(std::string_view s) {
    std::vector<std::size_t> n;
    std::size_t last_pos = std::numeric_limits<std::size_t>::max();
    std::size_t j = s.find(manapi::fs::path::delimiter);

    // TODO: support C:\ like format

    goto skip;

    while (!s.empty()) {
        j = s.find(manapi::fs::path::delimiter, last_pos);
skip:
        if (j == std::string::npos) {
            break;
        }

        if (last_pos != j) {
            n.push_back( j );
        }

        last_pos = j + 1;
    }

    if (last_pos != s.size()) {
        n.push_back(s.size());
    }

    return std::move(n);
}

void manapi::fs::path::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
        path.push_back(std::filesystem::path::preferred_separator);
}

typedef manapi::async::promise_sync<manapi::ev::status>::resolve_t manapi__fs_resolve;

typedef bool (*manapi__fs_operation_event_cb)(std::shared_ptr<manapi::ev::fs>, manapi__fs_resolve, void *);
typedef int (*manapi__fs_operation_start_cb)(std::shared_ptr<manapi::ev::fs>, void *);

struct manapi__fs_operation_t {
    void *source;
    manapi::ctoken *c;
    manapi::ev::shared_fs w;
    manapi__fs_resolve resolve;
    manapi__fs_operation_event_cb event_cb;
    manapi__fs_operation_start_cb start_cb;

    ~manapi__fs_operation_t() {
        assert(!!c);
        if (*c) { c->disable(); }
    }
};

static bool manapi__fs_operation_result_error (std::shared_ptr<manapi::ev::fs> &w, manapi__fs_resolve &resolve) {
    ssize_t rhs;
    if ((rhs = w->result()) < 0) {
        if (rhs == manapi::ev::ERR_CANCELED) resolve(manapi::ev::status_cancelled("fs:failed", static_cast<int>(rhs)));
        else resolve(manapi::ev::status_unknown("fs:failed", static_cast<int>(rhs)));
        return true;
    }
    return false;
}

static manapi::future<manapi::ev::status> manapi__fs_operation (manapi__fs_operation_start_cb start_cb, manapi__fs_operation_event_cb event_cb, void *source, manapi::ctoken cancellation) {
    typedef manapi::async::promise_sync<manapi::ev::status> promise_sync;
    manapi__fs_operation_t t {};

    t.c = &cancellation;
    t.event_cb = event_cb;
    t.start_cb = start_cb;
    t.source = source;

    co_return co_await promise_sync([&t] (manapi__fs_resolve resolve)
        -> void {
        t.resolve = std::move(resolve);
        if (t.c->contains_cancel_callback()) {
            t.c->cancel_callback([&t] () mutable -> void { manapi::async::eventloop()->stop_watcher (t.w); });
        }
        t.w = manapi::async::eventloop()->create_watcher_fs([&t]
                (std::shared_ptr<manapi::ev::fs> w) mutable
            -> bool {
            bool res = true;
            try {
                if (!t.event_cb (t.w, t.resolve, t.source)) {
                    w->cleanup();
                    if (int rhs = ((*t.start_cb)(t.w, t.source))) {
                        t.resolve (manapi::ev::status_unknown("fs:i/o init watcher failed", rhs));
                    }
                    else { res = false; }
                }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "fs:failed", e.what());
                t.resolve (manapi::ev::status_unknown("fs:failed", manapi::ev::ERR_UNKNOWN));
            }
            return res;
        }).unwrap();
        if (int rhs = ((*t.start_cb)(t.w, t.source))) {
            t.resolve (manapi::ev::status_unknown("fs:i/o init watcher failed", rhs));
        }
    });
}

static manapi::future<manapi::ev::status> manapi__fs_easy_operation (manapi__fs_operation_start_cb start_cb, void *source, manapi::ctoken cancellation) {
    co_return co_await manapi__fs_operation ((start_cb),
        +[](std::shared_ptr<manapi::ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (!manapi__fs_operation_result_error (w, resolve)) { resolve (manapi::ev::status_ok()); }
            return true;
        }, source, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<bool>> manapi::fs::async_exists(std::string path, manapi::ctoken cancellation) {
    auto res = co_await manapi::fs::async_stat(std::move(path),
        [] (ev::stat_t *st) -> void { }, std::move(cancellation));
    if (res.ok()) co_return true;
    if (res.syserr() == ev::ERR_NOENT) co_return false;
    co_return std::move(res);
}

manapi::future<manapi::ev::status_or<std::chrono::system_clock::time_point>> manapi::fs::async_last_time_write(std::string path, manapi::ctoken cancellation) {
#if MANAPHTTP_UV_SINCE_AT(1,45,0)
    uv_timespec64_t mtime;
#else
    uv_timespec_t mtime;
#endif
    auto res = co_await manapi::fs::async_stat(std::move(path), [&mtime] (ev::stat_t *stat)
        -> void {
        assert(stat);
        mtime.tv_nsec = static_cast<decltype(mtime.tv_nsec)>(stat->st_mtim.tv_nsec);
        mtime.tv_sec = static_cast<decltype(mtime.tv_sec)>(stat->st_mtim.tv_sec);
    }, std::move(cancellation));

    if (!res.ok()) co_return std::move(res);

    /**
     * Windows doesn't accept std::nano in the time_point,
     * so we need to remove it.
     *
     * also we can continue using it in the Linux system
     * to saving accuracy
     */
    co_return std::chrono::system_clock::time_point(
            std::chrono::seconds{mtime.tv_sec}/* + std::chrono::nanoseconds{mtime.tv_nsec} */);
}

manapi::future<manapi::ev::status> manapi::fs::async_mkdir(std::string path, int mode, bool recursive, manapi::ctoken cancellation) {
    enum data_flags { MANAPI__MKDIR_DATA_FLAG_NONE = 0, MANAPI__MKDIR_DATA_FLAG_FIND, MANAPI__MKDIR_DATA_FLAG_BUILD };
    struct data_t { char *path; int mode; std::vector < std::size_t > parts; std::size_t l, r; int state; };
    data_t data ( path.data(), mode );
    if (recursive) {
        data.parts = manapi__fs_find_all(path);
        data.l = 0; data.r = data.parts.size();
        data.state = MANAPI__MKDIR_DATA_FLAG_FIND;
    }
    co_return co_await manapi__fs_operation ( +[] ( manapi::ev::shared_fs w, void *source )
        -> int {
        auto data = static_cast<data_t *> (source);
        if (data->state == MANAPI__MKDIR_DATA_FLAG_NONE) {
            return w->mkdir( data->path, data->mode );
        }
        else {
            std::size_t indx;
            if (data->state == MANAPI__MKDIR_DATA_FLAG_BUILD) indx = data->l;
            else indx = (data->l + data->r) / 2;
            auto c = std::exchange(data->path[ data->parts[ indx ]], '\0');
            auto rhs = w->mkdir ( data->path, data->mode );
            data->path[ data->parts[ indx ]] = c;
            return rhs;
        }
    }, +[] ( manapi::ev::shared_fs w, manapi__fs_resolve resolve, void *source ) -> bool {
        auto data = static_cast<data_t *> (source);
        if (data->state == MANAPI__MKDIR_DATA_FLAG_FIND) {
            auto result = w->result();
            if (result == manapi::ev::ERR_EXIST) data->l = (data->l + data->r) / 2 + 1;
            else if (result == manapi::ev::ERR_NOENT) data->r = (data->l + data->r) / 2;
            else {
                if (manapi__fs_operation_result_error(w, resolve)) return true;
                else {
                    data->state = MANAPI__MKDIR_DATA_FLAG_BUILD;
                    data->l = (data->l + data->r) / 2;
                }
            }
            if (data->state == MANAPI__MKDIR_DATA_FLAG_FIND) {
                if (data->l < data->r) return false;
            }
        }
        else if (manapi__fs_operation_result_error(w, resolve)) return true;
        if (data->state == MANAPI__MKDIR_DATA_FLAG_BUILD && data->l + 1 < data->parts.size()) {
            data->l++;
            return false;
        }
        resolve (manapi::ev::status_ok());
        return true;
    }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<manapi::ev::unique_file>> manapi::fs::async_open(std::string path, int flags, int mode, manapi::ctoken cancellation) {
    struct data_t { const char *path; int flags; int mode; manapi::ev::unique_file result; };
    data_t data ( path.data(), flags, mode );
    auto st = co_await manapi__fs_operation (
            +[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
        return w->open(
                static_cast<data_t *> (source)->path,
                static_cast<data_t *> (source)->flags,
                static_cast<data_t *> (source)->mode
                );
    }, +[] (std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source)
    -> bool {
        if (manapi__fs_operation_result_error (w, resolve)) return true;
        auto file = static_cast<manapi::ev::file>(w->result());
        manapi_log_trace2("manapihttp::fs", manapi::debug::LOG_TRACE_LOW, "fs:fd %d open", file);
        static_cast<data_t *> (source)->result = manapi::ev::unique_file (file);
        resolve (manapi::ev::status_ok());
        return true;
    }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status> manapi::fs::async_close(ev::file file, ctoken cancellation) {
    manapi_log_trace2("manapihttp::fs", manapi::debug::LOG_TRACE_LOW, "fs:fd %d close", file);
    co_return co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->close(*static_cast<ev::file *> (source));
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source)
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            manapi_log_trace2("manapihttp::fs", manapi::debug::LOG_TRACE_LOW, "fs:fd %d finished", *static_cast<ev::file *> (source));
            resolve(manapi::ev::status_ok());
            return true;
        }, &file, std::move(cancellation));
}

manapi::future<ssize_t> manapi::fs::async_write(ev::file file, const void *data, std::size_t size, int64_t offset, manapi::ctoken cancellation) {
    ev::buff_t buff;
    buff.base = (char *)(data);
    buff.len = static_cast<decltype(buff.len)>(size);
    co_return co_await async_write(file, &buff, 1,  offset, std::move(cancellation));
}

manapi::future<ssize_t> manapi::fs::async_read(ev::file file, void *data, std::size_t size, int64_t offset, manapi::ctoken cancellation) {
    ev::buff_t buff;
    buff.base = static_cast<char*>(data);
    buff.len = static_cast<decltype(buff.len)>(size);
    co_return co_await async_read (file, &buff, 1, offset, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_write(std::string path, std::string data, int mode, int flags, int64_t offset, manapi::ctoken cancellation) {
    ev::unique_file fileno;
    {
        auto fileno_res = co_await async_open(path, flags, mode, cancellation.sub());
        if (!fileno_res.ok()) co_return fileno_res.err();
        fileno = fileno_res.unwrap();
    }
    auto sv = std::string_view (data);
    while (!sv.empty()) {
        auto rhs = co_await async_write(fileno.get(), sv.data(), (sv.size()), offset, cancellation.sub());
        if (rhs < 0) co_return ev::status_unknown("fs:write failed", static_cast<int>(rhs));
        if (offset >= 0) offset += static_cast<int64_t> (rhs);
        sv = sv.substr ( static_cast<std::size_t> (rhs) );
    }
    co_return ev::status_ok();
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_read(std::string path, int flags, int64_t offset, manapi::ctoken cancellation) {
    ev::unique_file fileno;
    std::string data;

    {
        auto fileno_res = co_await async_open(path, flags, 0, cancellation.sub());
        if (!fileno_res.ok()) co_return fileno_res.err();
        fileno = fileno_res.unwrap();
    }

    auto res = co_await async_file_size(fileno.get(), cancellation.sub());

    if (!res.ok()) co_return res.err();

    data.resize(res.unwrap());

    std::string_view sv (data);

    while (!sv.empty()) {
        auto rhs = co_await async_read(fileno.get(), const_cast<char *>(sv.data()), sv.size(), offset, cancellation.sub());
        if (rhs < 0) co_return ev::status_unknown("fs:read failed", static_cast<int>(rhs));
        if (offset >= 0) offset += static_cast<int64_t> (rhs);
        sv = sv.substr (static_cast<std::size_t> (rhs));
    }

    co_return std::move(data);
}

manapi::future<ssize_t> manapi::fs::async_write(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, ctoken cancellation) {
    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        auto rhs = ev::fs::try_write(file, buff->base + shift, buff->len - shift, offset);

        if (rhs < 0) {
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else {
                if (res) rhs = 0;
                else co_return rhs;
            }
        }

        if (!rhs)
            break;

        if (offset >= 0)
            offset += rhs;

        res += rhs;
        shift += static_cast<std::size_t>(rhs);

        if (shift == buff[0].len) {
            nbuff--;
            buff++;
            shift = 0;
        }
    }

    if (res || !nbuff)
        co_return static_cast<ssize_t>(res);

    assert ( !shift );

    struct data_t { ev::file file; ev::buff_t *buff; uint32_t nbuff; int64_t offset; ssize_t result; };
    data_t data ( file, buff, nbuff, offset );

    auto st = co_await manapi__fs_operation ( +[] ( ev::shared_fs w, void * source)
        -> int {
        return w->write (
                static_cast< data_t * > (source)->file,
                static_cast< data_t * > (source)->buff,
                static_cast< data_t * > (source)->nbuff,
                static_cast< data_t * > (source)->offset
            );
    }, +[] (ev::shared_fs w, manapi__fs_resolve resolve, void *source ) -> bool {
        static_cast<data_t *> (source)->result = w->result();
        resolve(manapi::ev::status_ok());
        return true;
    }, &data, std::move(cancellation));

    if (!st) {
        if (st.syserr() < 0) co_return st.syserr();
        co_return manapi::ev::ERR_UNKNOWN;
    }

    co_return data.result;
}

manapi::future<ssize_t> manapi::fs::async_read(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, ctoken cancellation) {
    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        ssize_t rhs = ev::fs::try_read(file, buff->base + shift, buff->len - shift, offset);

        if (rhs < 0) {
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else {
                if (res) rhs = 0;
                else co_return rhs;
            }
        }

        if (!rhs)
            break;

        if (offset >= 0)
            offset += rhs;

        shift += static_cast<std::size_t>(rhs);
        res += rhs;

        if (shift == buff->len) {
            shift = 0;
            buff++;
            nbuff--;
        }
    }

    if (res || !nbuff) co_return res;

    assert ( !shift );

    struct data_t { ev::file file; ev::buff_t *buff; uint32_t nbuff; int64_t offset; ssize_t result; };
    data_t data ( file, buff, nbuff, offset );

    auto st = co_await manapi__fs_operation ( +[] ( ev::shared_fs w, void * source)
            -> int {
        return w->read (
                static_cast< data_t * > (source)->file,
                static_cast< data_t * > (source)->buff,
                static_cast< data_t * > (source)->nbuff,
                static_cast< data_t * > (source)->offset
        );
    }, +[] (ev::shared_fs w, manapi__fs_resolve resolve, void *source ) -> bool {
        static_cast<data_t *> (source)->result = w->result();
        resolve(manapi::ev::status_ok());
        return true;
    }, &data, std::move(cancellation));

    if (!st) {
        if (st.syserr() < 0) co_return st.syserr();
        co_return manapi::ev::ERR_UNKNOWN;
    }

    co_return data.result;
}

manapi::future<ssize_t> manapi::fs::async_write(ev::file file, slice_view slice, int64_t offset, ctoken cancellation) {
    ssize_t total = 0;

    while (true) {
        std::size_t buffs_sz = 0;
        std::size_t buffs_cnt = 0;
        auto buffs = slice.slices_buffs(128, &buffs_cnt, &buffs_sz);
        auto z = co_await async_write(file, buffs.get(), static_cast<uint32_t>(buffs_cnt), offset, cancellation.sub());
        if (z < 0) co_return z;
        total += z;

        if (static_cast<std::size_t>(z) == buffs_sz) {
            slice = slice.subslice(buffs_sz).unwrap();
            if (offset >= 0) offset += z;
            if (!slice.empty()) continue;
        }

        break;
    }

    co_return total;
}

manapi::future<ssize_t> manapi::fs::async_read(ev::file file, slice_view slice, int64_t offset, ctoken cancellation) {
    ssize_t total = 0;

    while (true) {
        std::size_t buffs_sz = 0;
        std::size_t buffs_cnt = 0;
        auto buffs = slice.slices_buffs(128, &buffs_cnt, &buffs_sz);
        auto z = co_await async_read(file, buffs.get(), static_cast<uint32_t>(buffs_cnt), offset, cancellation.sub());
        if (z < 0) co_return z;
        total += z;
        if (static_cast<std::size_t>(z) == buffs_sz) {
            slice = slice.subslice(buffs_sz).unwrap();
            if (offset >= 0) offset += z;
            if (!slice.empty()) continue;
        }

        break;
    }

    co_return total;
}

manapi::future<manapi::ev::status_or<uint64_t>> manapi::fs::async_file_size(std::string path, manapi::ctoken cancellation) {
    uint64_t size;

    auto res = co_await fs::async_stat(std::move(path), [&size] (ev::stat_t *stat)
        -> void { size = stat->st_size; }, std::move(cancellation));

    if (!res) co_return std::move(res);
    co_return size;
}

manapi::future<manapi::ev::status_or<uint64_t>> manapi::fs::async_file_size(ev::file file, manapi::ctoken cancellation) {
    uint64_t size;

    auto res = co_await fs::async_fstat(file, [&size] (ev::stat_t *stat)
        -> void { size = stat->st_size; }, std::move(cancellation));

    if (!res) co_return std::move(res);
    co_return size;
}

manapi::future<manapi::ev::status> manapi::fs::async_stat(std::string path, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation) {
    struct data_t { const char *path;
        decltype(callback) *cb; };
    data_t data ( path.data(), &callback );
    co_return co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->stat(static_cast<data_t *> (source)->path);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            auto &cb = *static_cast<data_t *> (source)->cb;
            if (!!cb) cb(&w->custom()->statbuf);
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fstat(ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, ctoken cancellation) {
    struct data_t { ev::file file;
        decltype(callback) *cb; };
    data_t data ( file, &callback );
    co_return co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->fstat(static_cast<data_t *> (source)->file);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            auto &cb = *static_cast<data_t *> (source)->cb;
            if (cb) cb(&w->custom()->statbuf);
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
}

static void manapi__clean_delimiters_at_end (std::string_view &str) {
    // clean delimiters at the end
    while (!str.empty() && str.back() == manapi::fs::path::delimiter)
        str = str.substr(0, str.size() - 1);
}

std::string_view manapi::fs::path::back (std::string_view str) {
    manapi__clean_delimiters_at_end(str);
    auto it = str.rfind(fs::path::delimiter);
    if (it != std::string_view::npos)
        str = str.substr(0, it);
    return str;
}

static void manapi__fs_path_append (std::string &path, std::string_view next, bool delimiter, bool root) {
    using namespace manapi::fs;

    if (delimiter) {
        while (true) {
            auto it = next.find(path::delimiter);
            if (it == std::string_view::npos)
                break;
            if (it) {
                manapi__fs_path_append(path, next.substr(0, it), false, root && path.empty());
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
        manapi__clean_delimiters_at_end(next);
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
    manapi__fs_path_append(path, std::string_view(c, c_size), true, root);
    manapi__fs_path_append(path, next, true, false);
}

std::string manapi::fs::path::current_path() {
    return std::filesystem::current_path().string();
}

std::string manapi::fs::path::serialize (std::string_view str) {
    std::string path;
    manapi__fs_path_append (path, str, true, true);
    return std::move(path);
}

std::string manapi::fs::path::absolute(std::string_view path) {
    return std::filesystem::absolute(path).string();
}

std::string manapi::fs::path::root_directory() {
    return std::filesystem::current_path().root_directory().string();
}

manapi::future<manapi::ev::status> manapi::fs::async_unlink (std::string path, ctoken cancellation) {
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->unlink( static_cast<const char *> (source) ); }, path.data(), std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_rmdir (std::string path, ctoken cancellation) {
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->rmdir( static_cast<const char *>(source) ); }, path.data(), std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_rmdir_all(std::string path, ctoken cancellation) {
    std::unique_ptr<manapi::ev::dirent_t, manapi::ev::impl_array_deleter<manapi::ev::dirent_t>> dirents (new manapi::ev::dirent_t[128]);
    std::vector<std::string> folders;
    std::vector<std::unique_ptr<uv_dir_s, ev::dir_deleter_t>> dirs;
    std::vector<std::string> paths;

    std::exception_ptr err{nullptr};

    auto fd_res = co_await manapi::fs::async_opendir(path, cancellation.sub());
    if (!fd_res) co_return fd_res.err();

    dirs.push_back(fd_res.unwrap());
    paths.push_back(path);

    while (!dirs.empty()) {
        auto &b = dirs.back();

        if (!b) {
            auto rm_res = co_await manapi::fs::async_rmdir(paths.back(), cancellation.sub());
            if (!rm_res && rm_res.syserr() != ev::ERR_NOENT)
                co_return std::move(rm_res);

            dirs.pop_back();
            paths.pop_back();

            continue;
        }

        b->dirents = dirents.get();
        b->nentries = 128;

        std::vector<std::string> files;

        while (true) {
            std::size_t cnt;
            auto res = co_await manapi::fs::async_readdir(b.get(), [&files, &folders, &paths, &err, &cnt] (ev::dir_t *handle, std::size_t cnt1) -> bool {
                try {
                    cnt = cnt1;
                    for (std::size_t i = 0; i < cnt1; i++) {
                        std::string_view name = handle->dirents[i].name;
                        auto type = handle->dirents[i].type;

                        if (type == UV_DIRENT_DIR) {
                            folders.push_back(manapi::fs::path::join(paths.back(), name));
                        }
                        else if (type == UV_DIRENT_LINK) {
                            files.emplace_back(name);
                        }
                        else {
                            files.emplace_back(name);
                        }
                    }
                }
                catch (...) {
                    err = std::current_exception();
                }
                return true;
            }, cancellation.sub());

            if (!res)
                co_return std::move(res);

            if (err)
                std::rethrow_exception(std::move(err));

            while (!files.empty()) {
                auto &z = files.back();

                auto rmres = co_await manapi::fs::async_unlink(manapi::fs::path::join(paths.back(), z), cancellation.sub());
                if (!rmres && rmres.syserr() != ev::ERR_NOENT)
                    co_return std::move(rmres);

                files.pop_back();
            }

            if (cnt != b->nentries) {
                dirs.back() = nullptr;

                break;
            }

            if (!folders.empty())
                break;
        }

        if (!folders.empty()) {
            fd_res = co_await manapi::fs::async_opendir(folders.back(), cancellation.sub());
            if (!fd_res) {
                if (fd_res.syserr() == ev::ERR_NOENT) {
                    folders.pop_back();
                    continue;
                }

                co_return fd_res.err();
            }

            dirs.push_back(fd_res.unwrap());
            paths.push_back(folders.back());

            folders.pop_back();
        }
    }

    co_return manapi::status_ok();
}

manapi::future<manapi::ev::status> manapi::fs::async_closedir (manapi::ev::dir_t *directory, ctoken cancellation) {
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->closedir(static_cast<manapi::ev::dir_t *> ( source )); }, directory, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_statfs (std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, ctoken cancellation) {
    struct data_t { const char *path;
        decltype(callback) *cb; };
    data_t data ( path.data(), &callback );
    co_return co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->statfs(static_cast<data_t *> (source)->path);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            auto &cb = *static_cast<data_t *> (source)->cb;
            if (!!cb) cb (static_cast<ev::statfs_t*>(w->custom()->ptr));
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_rename (std::string oldpath, std::string newpath, ctoken cancellation) {
    struct data_t { const char *oldpath, *newpath; };
    data_t data ( oldpath.data(), newpath.data() );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->rename(
            static_cast<data_t *> (source)->oldpath, static_cast<data_t *> (source)->newpath
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_copyfile (std::string src, std::string dest, int flags, ctoken cancellation) {
    struct data_t { const char *src, *dest; int flags; };
    data_t data ( src.data(), dest.data(), flags );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->copyfile(
            static_cast<data_t *> (source)->src,
            static_cast<data_t *> (source)->dest,
            static_cast<data_t *> (source)->flags
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_chmod (std::string path, int mode, ctoken cancellation) {
    struct data_t { const char *path; int mode; };
    data_t data ( path.data(), mode );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->chmod(
            static_cast<data_t *> (source)->path,
            static_cast<data_t *> (source)->mode
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fchmod (ev::file file, int mode, ctoken cancellation) {
    struct data_t { ev::file file; int mode; };
    data_t data ( file, mode );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->fchmod(
                static_cast<data_t *> (source)->file,
                static_cast<data_t *> (source)->mode
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_access (std::string path, int mode, ctoken cancellation) {
    struct data_t { const char *path; int mode; };
    data_t data ( path.data(), mode );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->access(
                    static_cast<data_t *> (source)->path,
                    static_cast<data_t *> (source)->mode
                    ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_utime (std::string path, double atime, double mtime, ctoken cancellation) {
    struct data_t { const char *path; double atime; double mtime; };
    data_t data ( path.data(), atime, mtime );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->utime(
                static_cast<data_t *> (source)->path,
                static_cast<data_t *> (source)->atime,
                static_cast<data_t *> (source)->mtime
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_futime (ev::file file, double atime, double mtime, ctoken cancellation) {
    struct data_t { ev::file file; double atime; double mtime; };
    data_t data (file, atime, mtime);
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->futime(
                static_cast<data_t *> (source)->file,
                static_cast<data_t *> (source)->atime,
                static_cast<data_t *> (source)->mtime
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_link (std::string path, std::string newpath, ctoken cancellation) {
    struct data_t { const char *path,* newpath; };
    data_t data (path.data(), newpath.data());
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->link(
                static_cast<data_t *> (source)->path,
                static_cast<data_t *> (source)->newpath
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_symlink (std::string path, std::string newpath, int flags, ctoken cancellation) {
    struct data_t { const char *path, *newpath; int flags; };
    data_t data (path.data(), newpath.data(), flags);
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->symlink(
                static_cast<data_t *> (source)->path,
                static_cast<data_t *> (source)->newpath,
                static_cast<data_t *> (source)->flags
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fsync (ev::file file, ctoken cancellation) {
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->fsync(*static_cast<ev::file *> (source)); }, &file, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fdatasync(ev::file file, ctoken cancellation) {
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->fdatasync(*static_cast<ev::file *> (source)); }, &file, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::unique_ptr<manapi::ev::dir_t, manapi::ev::dir_deleter_t>>> manapi::fs::async_opendir(std::string path, ctoken cancellation) {
    struct data_t { const char *path; std::unique_ptr<manapi::ev::dir_t, manapi::ev::dir_deleter_t> result; };
    data_t data (path.data());
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->opendir(static_cast<data_t *> (source)->path);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            static_cast<data_t *> (source)->result = std::unique_ptr<manapi::ev::dir_t, manapi::ev::dir_deleter_t>(static_cast<ev::dir_t *> (w->custom()->ptr));
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_readlink (std::string path, ctoken cancellation) {
    struct data_t { const char *path; std::string result; };
    data_t data (path.data());
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->readlink(static_cast<data_t *> (source)->path);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            static_cast<data_t *> (source)->result = std::string{static_cast<const char *> (w->custom()->ptr)};
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_realpath (std::string path, ctoken cancellation) {
    struct data_t { const char *path; std::string result; };
    data_t data ( path.data() );
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->realpath(static_cast<data_t *> (source)->path);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            static_cast<data_t *> (source)->result = std::string{static_cast<const char *> (w->custom()->ptr)};
            resolve( manapi::ev::status_ok() );
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status> manapi::fs::async_chown(std::string path, ev::uid_t uid, ev::gid_t gid, ctoken cancellation) {
    struct data_t { const char *path; ev::uid_t uid; ev::gid_t gid; };
    data_t data ( path.data(), uid, gid);
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->chown(
            static_cast<data_t *> (source)->path,
            static_cast<data_t *> (source)->uid,
            static_cast<data_t *> (source)->gid
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status> manapi::fs::async_fchown(ev::file file, ev::uid_t uid, ev::gid_t gid, ctoken cancellation) {
    struct data_t { manapi::ev::file file; ev::uid_t uid; ev::gid_t gid; };
    data_t data ( file, uid, gid );
    co_return co_await manapi__fs_easy_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int { return w->fchown(
            static_cast<data_t *> (source)->file,
            static_cast<data_t *> (source)->uid,
            static_cast<data_t *> (source)->gid
                ); }, &data, std::move(cancellation));
}

manapi::future<manapi::ev::status_or<std::string>> manapi::fs::async_mkdtemp(std::string tpl, ctoken cancellation) {
    struct data_t { const char *tpl; std::string result; };
    data_t data ( tpl.data() );
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->mkdtemp(static_cast<data_t *> (source)->tpl);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            static_cast<data_t *>(source)->result = std::string{w->custom()->path};
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status_or<std::pair<std::string, manapi::ev::unique_file>>> manapi::fs::async_mkstemp(std::string tpl, ctoken cancellation) {
    typedef std::pair<std::string, manapi::ev::unique_file> value_t;
    struct data_t { const char *tpl; value_t result; };
    data_t data (tpl.data());
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->mkstemp(static_cast<data_t *> (source)->tpl);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            static_cast<data_t *> (source)->result = std::make_pair(std::string{w->custom()->path},
                    manapi::ev::unique_file (static_cast<ev::file>(w->custom()->result)));
            resolve (manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return std::move(data.result);
}

manapi::future<manapi::ev::status_or<std::size_t>> manapi::fs::async_scandir (std::string path, int flags, std::move_only_function<void(ev::dir_t *dir, std::size_t result)> callback, ctoken cancellation) {
    struct data_t { const char *path; int flags;
        decltype(callback) *cb; std::size_t result; };
    data_t data ( path.data(), flags, &callback, 0 );
    auto st = co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->scandir(
                    static_cast<data_t *> (source)->path,
                    static_cast<data_t *> (source)->flags
                    );
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            (*static_cast<data_t *> (source)->cb)(ptr, static_cast<std::size_t>(w->result()));
            static_cast<data_t *> (source)->result = static_cast<std::size_t>(w->result());
            resolve(manapi::ev::status_ok());
            return true;
        }, &data, std::move(cancellation));
    if (!st) co_return std::move(st);
    co_return data.result;
}

manapi::future<manapi::ev::status> manapi::fs::async_readdir (ev::dir_t *dir, std::move_only_function<bool (ev::dir_t *, std::size_t)> callback, ctoken cancellation) {
    struct data_t { ev::dir_t *dir; decltype(callback) *cb; };
    data_t data ( dir, &callback );

    co_return co_await manapi__fs_operation (+[] (std::shared_ptr<ev::fs> w, void *source)
        -> int {
            return w->readdir(static_cast<data_t *> (source)->dir);
        }, +[](std::shared_ptr<ev::fs> w, manapi__fs_resolve resolve, void *source) mutable
        -> bool {
            auto const result = w->result();
            if (manapi__fs_operation_result_error (w, resolve)) return true;
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            if ( (*static_cast<data_t *> (source)->cb) (ptr, static_cast<std::size_t>(result))
                    || static_cast<std::size_t>(result) != static_cast<data_t *> (source)->dir->nentries) {
                resolve(manapi::ev::status_ok());
                return true;
            }
            return false;
        }, &data, std::move(cancellation));
}