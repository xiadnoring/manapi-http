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

#define XX(code, msg) static const char *EV_FS_## code ##_MSG = msg;
XX(E2BIG, "argument list too long")
XX(EACCES, "permission denied")
XX(EADDRINUSE, "address already in use")
XX(EADDRNOTAVAIL, "address not available")
XX(EAFNOSUPPORT, "address family not supported")
XX(EAGAIN, "resource temporarily unavailable")
XX(EAI_ADDRFAMILY, "address family not supported")
XX(EAI_AGAIN, "temporary failure")
XX(EAI_BADFLAGS, "bad ai_flags value")
XX(EAI_BADHINTS, "invalid value for hints")
XX(EAI_CANCELED, "request canceled")
XX(EAI_FAIL, "permanent failure")
XX(EAI_FAMILY, "ai_family not supported")
XX(EAI_MEMORY, "out of memory")
XX(EAI_NODATA, "no address")
XX(EAI_NONAME, "unknown node or service")
XX(EAI_OVERFLOW, "argument buffer overflow")
XX(EAI_PROTOCOL, "resolved protocol is unknown")
XX(EAI_SERVICE, "service not available for socket type")
XX(EAI_SOCKTYPE, "socket type not supported")
XX(EALREADY, "connection already in progress")
XX(EBADF, "bad file descriptor")
XX(EBUSY, "resource busy or locked")
XX(ECANCELED, "operation canceled")
XX(ECHARSET, "invalid Unicode character")
XX(ECONNABORTED, "software caused connection abort")
XX(ECONNREFUSED, "connection refused")
XX(ECONNRESET, "connection reset by peer")
XX(EDESTADDRREQ, "destination address required")
XX(EEXIST, "file already exists")
XX(EFAULT, "bad address in system call argument")
XX(EFBIG, "file too large")
XX(EHOSTUNREACH, "host is unreachable")
XX(EINTR, "interrupted system call")
XX(EINVAL, "invalid argument")
XX(EIO, "i/ o error")
XX(EISCONN, "socket is already connected")
XX(EISDIR, "illegal operation on a directory")
XX(ELOOP, "too many symbolic links encountered")
XX(EMFILE, "too many open files")
XX(EMSGSIZE, "message too long")
XX(ENAMETOOLONG, "name too long")
XX(ENETDOWN, "network is down")
XX(ENETUNREACH, "network is unreachable")
XX(ENFILE, "file table overflow")
XX(ENOBUFS, "no buffer space available")
XX(ENODEV, "no such device")
XX(ENOENT, "no such file or directory")
XX(ENOMEM, "not enough memory")
XX(ENONET, "machine is not on the network")
XX(ENOPROTOOPT, "protocol not available")
XX(ENOSPC, "no space left on device")
XX(ENOSYS, "function not implemented")
XX(ENOTCONN, "socket is not connected")
XX(ENOTDIR, "not a directory")
XX(ENOTEMPTY, "directory not empty")
XX(ENOTSOCK, "socket operation on non-socket")
XX(ENOTSUP, "operation not supported on socket")
XX(EOVERFLOW, "value too large for defined data type")
XX(EPERM, "operation not permitted")
XX(EPIPE, "broken pipe")
XX(EPROTO, "protocol error")
XX(EPROTONOSUPPORT, "protocol not supported")
XX(EPROTOTYPE, "protocol wrong type for socket")
XX(ERANGE, "result too large")
XX(EROFS, "read-only file system")
XX(ESHUTDOWN, "cannot send after transport endpoint shutdown")
XX(ESPIPE, "invalid seek")
XX(ESRCH, "no such process")
XX(ETIMEDOUT, "connection timed out")
XX(ETXTBSY, "text file is busy")
XX(EXDEV, "cross-device link not permitted")
XX(UNKNOWN, "unknown error")
XX(EOF, "end of file")
XX(ENXIO, "no such device or address")
XX(EMLINK, "too many links")
XX(EHOSTDOWN, "host is down")
XX(EREMOTEIO, "remote I/ O error")
XX(ENOTTY, "inappropriate ioctl for device")
XX(EFTYPE, "inappropriate file type or format")
XX(EILSEQ, "illegal byte sequence")
XX(ESOCKTNOSUPPORT, "socket type not supported")
XX(ENODATA, "no data available")
XX(EUNATCH, "protocol driver not attached")
#undef XX

#define MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE 4096LL

const char *fserr2msg (int num) {
    switch (num) {
        case manapi::ev::FS_E2BIG:
            return EV_FS_E2BIG_MSG;

        case manapi::ev::FS_EACCES:
            return EV_FS_EACCES_MSG;

        case manapi::ev::FS_EADDRINUSE:
            return EV_FS_EADDRINUSE_MSG;

        case manapi::ev::FS_EADDRNOTAVAIL:
            return EV_FS_EADDRNOTAVAIL_MSG;

        case manapi::ev::FS_EAFNOSUPPORT:
            return EV_FS_EAFNOSUPPORT_MSG;

        case manapi::ev::FS_EAGAIN:
            return EV_FS_EAGAIN_MSG;

        case manapi::ev::FS_EAI_ADDRFAMILY:
            return EV_FS_EAI_ADDRFAMILY_MSG;

        case manapi::ev::FS_EAI_AGAIN:
            return EV_FS_EAI_AGAIN_MSG;

        case manapi::ev::FS_EAI_BADFLAGS:
            return EV_FS_EAI_BADFLAGS_MSG;

        case manapi::ev::FS_EAI_BADHINTS:
            return EV_FS_EAI_BADHINTS_MSG;

        case manapi::ev::FS_EAI_CANCELED:
            return EV_FS_EAI_CANCELED_MSG;

        case manapi::ev::FS_EAI_FAIL:
            return EV_FS_EAI_FAIL_MSG;

        case manapi::ev::FS_EAI_FAMILY:
            return EV_FS_EAI_FAMILY_MSG;

        case manapi::ev::FS_EAI_MEMORY:
            return EV_FS_EAI_MEMORY_MSG;

        case manapi::ev::FS_EAI_NODATA:
            return EV_FS_EAI_NODATA_MSG;

        case manapi::ev::FS_EAI_NONAME:
            return EV_FS_EAI_NONAME_MSG;

        case manapi::ev::FS_EAI_OVERFLOW:
            return EV_FS_EAI_OVERFLOW_MSG;

        case manapi::ev::FS_EAI_PROTOCOL:
            return EV_FS_EAI_PROTOCOL_MSG;

        case manapi::ev::FS_EAI_SERVICE:
            return EV_FS_EAI_SERVICE_MSG;

        case manapi::ev::FS_EAI_SOCKTYPE:
            return EV_FS_EAI_SOCKTYPE_MSG;

        case manapi::ev::FS_EALREADY:
            return EV_FS_EALREADY_MSG;

        case manapi::ev::FS_EBADF:
            return EV_FS_EBADF_MSG;

        case manapi::ev::FS_EBUSY:
            return EV_FS_EBUSY_MSG;

        case manapi::ev::FS_ECANCELED:
            return EV_FS_ECANCELED_MSG;

        case manapi::ev::FS_ECHARSET:
            return EV_FS_ECHARSET_MSG;

        case manapi::ev::FS_ECONNABORTED:
            return EV_FS_ECONNABORTED_MSG;

        case manapi::ev::FS_ECONNREFUSED:
            return EV_FS_ECONNREFUSED_MSG;

        case manapi::ev::FS_ECONNRESET:
            return EV_FS_ECONNRESET_MSG;

        case manapi::ev::FS_EDESTADDRREQ:
            return EV_FS_EDESTADDRREQ_MSG;

        case manapi::ev::FS_EEXIST:
            return EV_FS_EEXIST_MSG;

        case manapi::ev::FS_EFAULT:
            return EV_FS_EFAULT_MSG;

        case manapi::ev::FS_EFBIG:
            return EV_FS_EFBIG_MSG;

        case manapi::ev::FS_EHOSTUNREACH:
            return EV_FS_EHOSTUNREACH_MSG;

        case manapi::ev::FS_EINTR:
            return EV_FS_EINTR_MSG;

        case manapi::ev::FS_EINVAL:
            return EV_FS_EINVAL_MSG;

        case manapi::ev::FS_EIO:
            return EV_FS_EIO_MSG;

        case manapi::ev::FS_EISCONN:
            return EV_FS_EISCONN_MSG;

        case manapi::ev::FS_EISDIR:
            return EV_FS_EISDIR_MSG;

        case manapi::ev::FS_ELOOP:
            return EV_FS_ELOOP_MSG;

        case manapi::ev::FS_EMFILE:
            return EV_FS_EMFILE_MSG;

        case manapi::ev::FS_EMSGSIZE:
            return EV_FS_EMSGSIZE_MSG;

        case manapi::ev::FS_ENAMETOOLONG:
            return EV_FS_ENAMETOOLONG_MSG;

        case manapi::ev::FS_ENETDOWN:
            return EV_FS_ENETDOWN_MSG;

        case manapi::ev::FS_ENETUNREACH:
            return EV_FS_ENETUNREACH_MSG;

        case manapi::ev::FS_ENFILE:
            return EV_FS_ENFILE_MSG;

        case manapi::ev::FS_ENOBUFS:
            return EV_FS_ENOBUFS_MSG;

        case manapi::ev::FS_ENODEV:
            return EV_FS_ENODEV_MSG;

        case manapi::ev::FS_ENOENT:
            return EV_FS_ENOENT_MSG;

        case manapi::ev::FS_ENOMEM:
            return EV_FS_ENOMEM_MSG;

        case manapi::ev::FS_ENONET:
            return EV_FS_ENONET_MSG;

        case manapi::ev::FS_ENOPROTOOPT:
            return EV_FS_ENOPROTOOPT_MSG;

        case manapi::ev::FS_ENOSPC:
            return EV_FS_ENOSPC_MSG;

        case manapi::ev::FS_ENOSYS:
            return EV_FS_ENOSYS_MSG;

        case manapi::ev::FS_ENOTCONN:
            return EV_FS_ENOTCONN_MSG;

        case manapi::ev::FS_ENOTDIR:
            return EV_FS_ENOTDIR_MSG;

        case manapi::ev::FS_ENOTEMPTY:
            return EV_FS_ENOTEMPTY_MSG;

        case manapi::ev::FS_ENOTSOCK:
            return EV_FS_ENOTSOCK_MSG;

        case manapi::ev::FS_ENOTSUP:
            return EV_FS_ENOTSUP_MSG;

        case manapi::ev::FS_EOVERFLOW:
            return EV_FS_EOVERFLOW_MSG;

        case manapi::ev::FS_EPERM:
            return EV_FS_EPERM_MSG;

        case manapi::ev::FS_EPIPE:
            return EV_FS_EPIPE_MSG;

        case manapi::ev::FS_EPROTO:
            return EV_FS_EPROTO_MSG;

        case manapi::ev::FS_EPROTONOSUPPORT:
            return EV_FS_EPROTONOSUPPORT_MSG;

        case manapi::ev::FS_EPROTOTYPE:
            return EV_FS_EPROTOTYPE_MSG;

        case manapi::ev::FS_ERANGE:
            return EV_FS_ERANGE_MSG;

        case manapi::ev::FS_EROFS:
            return EV_FS_EROFS_MSG;

        case manapi::ev::FS_ESHUTDOWN:
            return EV_FS_ESHUTDOWN_MSG;

        case manapi::ev::FS_ESPIPE:
            return EV_FS_ESPIPE_MSG;

        case manapi::ev::FS_ESRCH:
            return EV_FS_ESRCH_MSG;

        case manapi::ev::FS_ETIMEDOUT:
            return EV_FS_ETIMEDOUT_MSG;

        case manapi::ev::FS_ETXTBSY:
            return EV_FS_ETXTBSY_MSG;

        case manapi::ev::FS_EXDEV:
            return EV_FS_EXDEV_MSG;

        case manapi::ev::FS_UNKNOWN:
            return EV_FS_UNKNOWN_MSG;

        case manapi::ev::FS_EOF:
            return EV_FS_EOF_MSG;

        case manapi::ev::FS_ENXIO:
            return EV_FS_ENXIO_MSG;

        case manapi::ev::FS_EMLINK:
            return EV_FS_EMLINK_MSG;

        case manapi::ev::FS_EHOSTDOWN:
            return EV_FS_EHOSTDOWN_MSG;

        case manapi::ev::FS_EREMOTEIO:
            return EV_FS_EREMOTEIO_MSG;

        case manapi::ev::FS_ENOTTY:
            return EV_FS_ENOTTY_MSG;

        case manapi::ev::FS_EFTYPE:
            return EV_FS_EFTYPE_MSG;

        case manapi::ev::FS_EILSEQ:
            return EV_FS_EILSEQ_MSG;

        case manapi::ev::FS_ESOCKTNOSUPPORT:
            return EV_FS_ESOCKTNOSUPPORT_MSG;

        case manapi::ev::FS_ENODATA:
            return EV_FS_ENODATA_MSG;

        case manapi::ev::FS_EUNATCH:
            return EV_FS_EUNATCH_MSG;
        default:
            return manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT];
    }
}

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
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FS_IO_RESULT, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_FS_IO_OPERATIONS], fserr2msg(w->result()))));

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
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_CALLBACK], e.what())));
    }
}

struct async_fs_operation_deleter {
    manapi::async::cancellation_action c;
    ~async_fs_operation_deleter() {
        this->c.cancel();
    }
};

template<typename T>
manapi::future<T> async_fs_operation (manapi::async::shared_ctx ctx, std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb,
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
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
            }

            if (cancellation.contains_cancel_callback()) {
                cancellation.cancel_callback([watcher = std::move(watcher), reject = std::move(reject)] (manapi::async::shared_ctx ctx) mutable
                    -> void {
                    ctx->eventloop()->stop_watcher<manapi::ev::fs>(std::move(watcher));
                    reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_CANCELLED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_CANCELLED])));
                });
            }
        });
    });
}

manapi::future<> async_fs_simple_operation (manapi::async::shared_ctx ctx, std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_return co_await async_fs_operation<void>(ctx, std::move(start_cb),
        [](manapi::async::context *ctx, std::shared_ptr<manapi::ev::fs> w,
            promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }

            resolve();
        }, std::move(cancellation));
}

manapi::future<bool> manapi::filesystem::async_exists(async::shared_ctx ctx, std::string path, manapi::async::cancellation_action cancellation) {
    co_return co_await filesystem::async_stat(std::move(ctx), std::move(path), nullptr, std::move(cancellation));
}

manapi::future<std::filesystem::file_time_type> manapi::filesystem::async_last_time_write(async::shared_ctx ctx, std::string path, manapi::async::cancellation_action cancellation) {
    uv_timespec_t mtime;
    bool exists = co_await filesystem::async_stat(std::move(ctx), std::move(path), [&mtime, &exists] (ev::stat_t *stat)
        -> void { mtime = stat->st_mtim; }, std::move(cancellation));
    if (!exists) { THROW_MANAPIHTTP_EXCEPTION2(ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_FILE_NOT_FOUND]); }
    co_return std::filesystem::file_time_type (std::chrono::seconds(mtime.tv_sec) + std::chrono::milliseconds(mtime.tv_nsec));
}

manapi::future<void> manapi::filesystem::async_mkdir(async::shared_ctx ctx, std::string path, int mode, bool recursive, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    if (recursive) {
        auto parts = manapi::string::split(path, path::delimiter);
        std::size_t size = 0;
        for (const auto &part : parts) {
            size += 1;

            if (part.empty()) {
                continue;
            }

            size += part.size();

            async::cancellation_action cancellation2 (ctx, cancellation);

            co_await async_fs_operation<void>(ctx,
                [path = path.substr(0, size), mode, cancellation2, cancellation](std::shared_ptr<ev::fs> w) mutable
                -> bool {
                    if (w->mkdir(path.data(), mode)) {
                        return false;
                    }

                    return true;
                },
                +[](manapi::async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
                -> void {
                    cancel.disable_cancellation();
                    auto rhs = w->result();
                    if ((rhs != ev::FS_EEXIST) && async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
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

manapi::future<manapi::ev::file> manapi::filesystem::async_open(async::shared_ctx ctx, std::string path, int flags, int mode, manapi::async::cancellation_action cancellation) {
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
        resolve (w->result());
    }, cancellation);

    co_return fileno;
}

manapi::future<void> manapi::filesystem::async_close(async::shared_ctx ctx, ev::file file, async::cancellation_action cancellation) {
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

manapi::future<ssize_t> manapi::filesystem::async_write(async::shared_ctx ctx, ev::file file, const void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    if (!size) {
        co_return size;
    }

    ssize_t rhs = ev::fs::try_write(file, data, size, offset);
    if (rhs > 0) {
        co_return rhs;
    }
    rhs = 0;

    struct async_write_data_t {
        ssize_t result;
        uv_buf_t buff[1];
        ev::file file;
        int64_t offset;
        async::context *ctx;
        async_fs_operation_event_cb<ssize_t> event_cb;
    } dd {
        rhs,
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
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO,manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w)] (async::shared_ctx ctx) mutable
                    -> void { ctx->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
            }
        }
        else {
            cancel.disable_cancellation();
            resolve(dd.result);
        }
    };

    rhs = co_await async_fs_operation<ssize_t>(ctx,
        [&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->write(dd.file, dd.buff, 1, dd.offset);
        }, [&dd] (async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) -> void {
            dd.event_cb(ctx, std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return rhs;
}

manapi::future<ssize_t> manapi::filesystem::async_read(async::shared_ctx ctx, ev::file file, void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    if (!size) {
        co_return size;
    }

    ssize_t rhs = ev::fs::try_read(file, data, size, offset);
    if (rhs > 0) {
        co_return rhs;
    }

    rhs = 0;

    struct async_read_data_t {
        ssize_t result;
        uv_buf_t buff[1];
        ev::file file;
        int64_t offset;
        async::context *ctx;
        async_fs_operation_event_cb<ssize_t> event_cb;
    } dd {
            rhs,
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

        if (rhs && rhs < dd.buff->len) {
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
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w)] (async::shared_ctx ctx) mutable
                    -> void { ctx->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
            }
        }
        else {
            cancel.disable_cancellation();
            resolve(dd.result);
        }
    };

    rhs = co_await async_fs_operation<ssize_t>(ctx,
        [&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->read(dd.file, dd.buff, 1, dd.offset);
        }, [&dd](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
            dd.event_cb(ctx, std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return rhs;
}

manapi::future<> manapi::filesystem::async_write(async::shared_ctx ctx, std::string path, std::string data, int mode, int64_t offset, manapi::async::cancellation_action cancellation) {
    std::exception_ptr err;
    ev::file fileno = 0;

    try {
        fileno = co_await async_open(ctx, path, ev::FS_O_WRONLY|ev::FS_O_CREAT|ev::FS_O_APPEND, mode, manapi::async::cancellation_action(ctx, cancellation));
        auto rhs = co_await async_write(ctx, fileno, data.data(), data.size(), offset, manapi::async::cancellation_action(ctx, cancellation));
        if (rhs != data.size()) {
            err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_SIZE_NOT_SAME]));
        }
    }
    catch (std::exception const &e) {
        err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_BY_ERROR], e.what()));
    }

    if (fileno > 0) {
        async::run(ctx,
                filesystem::async_close(ctx, fileno));
    }

    if (err) {
        std::rethrow_exception(err);
    }
}

manapi::future<std::string> manapi::filesystem::async_read(async::shared_ctx ctx, std::string path, int64_t offset, manapi::async::cancellation_action cancellation) {
    std::exception_ptr err;
    ev::file fileno = 0;
    std::string data;
    ssize_t len = 0;

    try {
        fileno = co_await async_open(ctx, path, ev::FS_O_RDONLY, 0, manapi::async::cancellation_action(ctx, cancellation));
        co_await async_fstat(ctx, fileno, [&len] (ev::stat_t *stat)
            -> void {
            len = stat ? static_cast<ssize_t>(stat->st_size) : -1;
        }, manapi::async::cancellation_action(ctx, cancellation));
        if (len == -1) { err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2 (ERR_FILE_NOT_FOUND, "file not exists")); }
        data.resize(len);
        auto rhs = co_await async_read(ctx, fileno, data.data(), len, offset, manapi::async::cancellation_action(ctx, cancellation));
        if (rhs != len) {
            err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_SIZE_NOT_SAME]));
        }
    }
    catch (std::exception const &e) {
        err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_FS_IO, manapi::error::default_msgs[manapi::error::ERRMSG_BY_ERROR], e.what()));
    }

    if (fileno > 0) {
        async::run(ctx,
                filesystem::async_close(ctx, fileno));
    }

    if (err) {
        std::rethrow_exception(err);
    }

    co_return std::move(data);
}

manapi::future<ssize_t> manapi::filesystem::async_file_size(async::shared_ctx ctx, std::string path, manapi::async::cancellation_action cancellation) {
    ssize_t size;

    co_await filesystem::async_stat(std::move(ctx), std::move(path), [&size] (ev::stat_t *stat)
        -> void {
        if (stat) {
            size = static_cast<ssize_t>(stat->st_size);
        }
        else {
            size = -1;
        }
    }, std::move(cancellation));

    if (size == -1) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, manapi::error::default_msgs[manapi::error::ERRMSG_WHEN_RECV_ADDITIONAL]);
    }

    co_return size;
}

manapi::future<bool> manapi::filesystem::async_stat(async::shared_ctx ctx, std::string path, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->stat(path.data());
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (w->result()) {
                resolve(false);
                return;
            }

            if (callback) {
                callback(&w->custom()->statbuf);
            }
            resolve(true);
        }, cancellation);
}

manapi::future<bool> manapi::filesystem::async_fstat(async::shared_ctx ctx, ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>(ctx,
        [file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->fstat(file);
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (w->result()) {
                resolve(false);
                return;
            }

            if (callback) {
                callback(&w->custom()->statbuf);
            }
            resolve(true);
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

manapi::future<void> manapi::filesystem::async_unlink (async::shared_ctx ctx, std::string path, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->unlink(path.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_rmdir (async::shared_ctx ctx, std::string path, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rmdir(path.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_closedir (manapi::async::shared_ctx ctx, manapi::ev::dir_t *directory, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [directory] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->closedir(directory); }, std::move(cancellation));
}

manapi::future<bool> manapi::filesystem::async_statfs (manapi::async::shared_ctx ctx, std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->statfs(path.data());
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();

            if (w->result()) {
                resolve(false);
                return;
            }

            if (callback) {
                callback(static_cast<ev::statfs_t*>(w->custom()->ptr));
            }
            resolve(true);
        }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_rename (manapi::async::shared_ctx ctx, std::string oldpath, std::string newpath, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [oldpath = std::move(oldpath), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rename(oldpath.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_copyfile (manapi::async::shared_ctx ctx, std::string src, std::string dest, int flags, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [src = std::move(src), dest = std::move(dest), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->copyfile(src.data(), dest.data(), flags); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_chmod (manapi::async::shared_ctx ctx, std::string path, int mode, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chmod(path.data(), mode); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fchmod (manapi::async::shared_ctx ctx, ev::file file, int mode, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [file, mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchmod(file, mode); }, std::move(cancellation));
}

manapi::future<int> manapi::filesystem::async_access (async::shared_ctx ctx, std::string path, int mode, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>(ctx,
        [path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->access(path.data(), mode);
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            resolve(w->result());
        }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_utime (async::shared_ctx ctx, std::string path, double atime, double mtime, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path), atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->utime(path.data(), atime, mtime); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_futime (async::shared_ctx ctx, ev::file file, double atime, double mtime, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [file, atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->futime(file, atime, mtime); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_link (manapi::async::shared_ctx ctx, std::string path, std::string newpath, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->link(path.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_symlink (manapi::async::shared_ctx ctx, std::string path, std::string newpath, int flags, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path = std::move(path), newpath = std::move(newpath), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->symlink(path.data(), newpath.data(), flags); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fsync (manapi::async::shared_ctx ctx, ev::file file, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fsync(file); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fdatasync(manapi::async::shared_ctx ctx, ev::file file, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fdatasync(file); }, std::move(cancellation));
}

manapi::future<manapi::ev::dir_t *> manapi::filesystem::async_opendir(manapi::async::shared_ctx ctx, std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<manapi::ev::dir_t *>;

    co_return co_await async_fs_operation<manapi::ev::dir_t *>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->opendir(path.data());
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve(static_cast<ev::dir_t *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_readlink (async::shared_ctx ctx, std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readlink(path.data());
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve(static_cast<const char *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_realpath (async::shared_ctx ctx, std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>(ctx,
        [path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->realpath(path.data());
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve(static_cast<const char *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_chown(manapi::async::shared_ctx ctx, std::string path, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [path=std::move(path), uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chown(path.data(), uid, gid); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fchown(manapi::async::shared_ctx ctx, ev::file file, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation (std::move(ctx), [file, uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchown(file, uid, gid); }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_mkdtemp(manapi::async::shared_ctx ctx, std::string tpl, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>(ctx,
        [tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkdtemp(tpl.data());
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve(std::string{w->custom()->path});
        }, std::move(cancellation));
}

manapi::future<std::pair<std::string, manapi::ev::file>> manapi::filesystem::async_mkstemp(manapi::async::shared_ctx ctx, std::string tpl, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::pair<std::string, manapi::ev::file>>;

    co_return co_await async_fs_operation<std::pair<std::string, manapi::ev::file>>(ctx,
        [tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkstemp(tpl.data());
        }, +[](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            resolve(std::make_pair(std::string{w->custom()->path}, static_cast<ev::file>(w->custom()->result)));
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_scandir (async::shared_ctx ctx, std::string path, int flags, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    co_return co_await async_fs_operation<ssize_t>(ctx,
        [path = std::move(path), flags] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->scandir(path.data(), flags);
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr);
            resolve(ptr->nentries);
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_readdir (async::shared_ctx ctx, ev::dir_t *dir, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    co_return co_await async_fs_operation<ssize_t>(ctx,
        [dir] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readdir(dir);
        }, [callback = std::move(callback)](async::context *ctx, std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable_cancellation();
            if (async_fs_operation_result_error<void>(w, ctx, reject, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr);
            resolve(ptr->nentries);
        }, std::move(cancellation));
}