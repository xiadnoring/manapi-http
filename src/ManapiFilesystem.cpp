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
#include "include/ManapiUtils.hpp"
#include "ManapiBeforeDelete.hpp"
#include "ManapiString.hpp"
#include "include/ManapiDefaultErrors.hpp"

// #define XX(code, msg) static const char *EV_FS_## code ##_MSG = msg;
// XX(E2BIG, "argument list too long")
// XX(EACCES, "permission denied")
// XX(EADDRINUSE, "address already in use")
// XX(EADDRNOTAVAIL, "address not available")
// XX(EAFNOSUPPORT, "address family not supported")
// XX(EAGAIN, "resource temporarily unavailable")
// XX(EAI_ADDRFAMILY, "address family not supported")
// XX(EAI_AGAIN, "temporary failure")
// XX(EAI_BADFLAGS, "bad ai_flags value")
// XX(EAI_BADHINTS, "invalid value for hints")
// XX(EAI_CANCELED, "request canceled")
// XX(EAI_FAIL, "permanent failure")
// XX(EAI_FAMILY, "ai_family not supported")
// XX(EAI_MEMORY, "out of memory")
// XX(EAI_NODATA, "no address")
// XX(EAI_NONAME, "unknown node or service")
// XX(EAI_OVERFLOW, "argument buffer overflow")
// XX(EAI_PROTOCOL, "resolved protocol is unknown")
// XX(EAI_SERVICE, "service not available for socket type")
// XX(EAI_SOCKTYPE, "socket type not supported")
// XX(EALREADY, "connection already in progress")
// XX(EBADF, "bad file descriptor")
// XX(EBUSY, "resource busy or locked")
// XX(ECANCELED, "operation canceled")
// XX(ECHARSET, "invalid Unicode character")
// XX(ECONNABORTED, "software caused connection abort")
// XX(ECONNREFUSED, "connection refused")
// XX(ECONNRESET, "connection reset by peer")
// XX(EDESTADDRREQ, "destination address required")
// XX(EEXIST, "file already exists")
// XX(EFAULT, "bad address in system call argument")
// XX(EFBIG, "file too large")
// XX(EHOSTUNREACH, "host is unreachable")
// XX(EINTR, "interrupted system call")
// XX(EINVAL, "invalid argument")
// XX(EIO, "i/ o error")
// XX(EISCONN, "socket is already connected")
// XX(EISDIR, "illegal operation on a directory")
// XX(ELOOP, "too many symbolic links encountered")
// XX(EMFILE, "too many open files")
// XX(EMSGSIZE, "message too long")
// XX(ENAMETOOLONG, "name too long")
// XX(ENETDOWN, "network is down")
// XX(ENETUNREACH, "network is unreachable")
// XX(ENFILE, "file table overflow")
// XX(ENOBUFS, "no buffer space available")
// XX(ENODEV, "no such device")
// XX(ENOENT, "no such file or directory")
// XX(ENOMEM, "not enough memory")
// XX(ENONET, "machine is not on the network")
// XX(ENOPROTOOPT, "protocol not available")
// XX(ENOSPC, "no space left on device")
// XX(ENOSYS, "function not implemented")
// XX(ENOTCONN, "socket is not connected")
// XX(ENOTDIR, "not a directory")
// XX(ENOTEMPTY, "directory not empty")
// XX(ENOTSOCK, "socket operation on non-socket")
// XX(ENOTSUP, "operation not supported on socket")
// XX(EOVERFLOW, "value too large for defined data type")
// XX(EPERM, "operation not permitted")
// XX(EPIPE, "broken pipe")
// XX(EPROTO, "protocol error")
// XX(EPROTONOSUPPORT, "protocol not supported")
// XX(EPROTOTYPE, "protocol wrong type for socket")
// XX(ERANGE, "result too large")
// XX(EROFS, "read-only file system")
// XX(ESHUTDOWN, "cannot send after transport endpoint shutdown")
// XX(ESPIPE, "invalid seek")
// XX(ESRCH, "no such process")
// XX(ETIMEDOUT, "connection timed out")
// XX(ETXTBSY, "text file is busy")
// XX(EXDEV, "cross-device link not permitted")
// XX(UNKNOWN, "unknown error")
// XX(EOF, "end of file")
// XX(ENXIO, "no such device or address")
// XX(EMLINK, "too many links")
// XX(EHOSTDOWN, "host is down")
// XX(EREMOTEIO, "remote I/ O error")
// XX(ENOTTY, "inappropriate ioctl for device")
// XX(EFTYPE, "inappropriate file type or format")
// XX(EILSEQ, "illegal byte sequence")
// XX(ESOCKTNOSUPPORT, "socket type not supported")
// XX(ENODATA, "no data available")
// XX(EUNATCH, "protocol driver not attached")
// #undef XX
//
// #define MANAPIHTTP_FILESYSTEM_COPY_BUFFER_SIZE 4096LL
//
// const char *fserr2msg (int num) {
//     switch (num) {
//         case manapi::ev::ERR_2BIG:
//             return EV_FS_E2BIG_MSG;
//
//         case manapi::ev::ERR_ACCES:
//             return EV_FS_EACCES_MSG;
//
//         case manapi::ev::ERR_ADDRINUSE:
//             return EV_FS_EADDRINUSE_MSG;
//
//         case manapi::ev::ERR_ADDRNOTAVAIL:
//             return EV_FS_EADDRNOTAVAIL_MSG;
//
//         case manapi::ev::ERR_AFNOSUPPORT:
//             return EV_FS_EAFNOSUPPORT_MSG;
//
//         case manapi::ev::ERR_AGAIN:
//             return EV_FS_EAGAIN_MSG;
//
//         case manapi::ev::ERR_AI_ADDRFAMILY:
//             return EV_FS_EAI_ADDRFAMILY_MSG;
//
//         case manapi::ev::ERR_AI_AGAIN:
//             return EV_FS_EAI_AGAIN_MSG;
//
//         case manapi::ev::ERR_AI_BADFLAGS:
//             return EV_FS_EAI_BADFLAGS_MSG;
//
//         case manapi::ev::ERR_AI_BADHINTS:
//             return EV_FS_EAI_BADHINTS_MSG;
//
//         case manapi::ev::ERR_AI_CANCELED:
//             return EV_FS_EAI_CANCELED_MSG;
//
//         case manapi::ev::ERR_AI_FAIL:
//             return EV_FS_EAI_FAIL_MSG;
//
//         case manapi::ev::ERR_AI_FAMILY:
//             return EV_FS_EAI_FAMILY_MSG;
//
//         case manapi::ev::ERR_AI_MEMORY:
//             return EV_FS_EAI_MEMORY_MSG;
//
//         case manapi::ev::ERR_AI_NODATA:
//             return EV_FS_EAI_NODATA_MSG;
//
//         case manapi::ev::ERR_AI_NONAME:
//             return EV_FS_EAI_NONAME_MSG;
//
//         case manapi::ev::ERR_AI_OVERFLOW:
//             return EV_FS_EAI_OVERFLOW_MSG;
//
//         case manapi::ev::ERR_AI_PROTOCOL:
//             return EV_FS_EAI_PROTOCOL_MSG;
//
//         case manapi::ev::ERR_AI_SERVICE:
//             return EV_FS_EAI_SERVICE_MSG;
//
//         case manapi::ev::ERR_AI_SOCKTYPE:
//             return EV_FS_EAI_SOCKTYPE_MSG;
//
//         case manapi::ev::ERR_ALREADY:
//             return EV_FS_EALREADY_MSG;
//
//         case manapi::ev::ERR_BADF:
//             return EV_FS_EBADF_MSG;
//
//         case manapi::ev::ERR_BUSY:
//             return EV_FS_EBUSY_MSG;
//
//         case manapi::ev::ERR_CANCELED:
//             return EV_FS_ECANCELED_MSG;
//
//         case manapi::ev::ERR_CHARSET:
//             return EV_FS_ECHARSET_MSG;
//
//         case manapi::ev::ERR_CONNABORTED:
//             return EV_FS_ECONNABORTED_MSG;
//
//         case manapi::ev::ERR_CONNREFUSED:
//             return EV_FS_ECONNREFUSED_MSG;
//
//         case manapi::ev::ERR_CONNRESET:
//             return EV_FS_ECONNRESET_MSG;
//
//         case manapi::ev::ERR_DESTADDRREQ:
//             return EV_FS_EDESTADDRREQ_MSG;
//
//         case manapi::ev::ERR_EXIST:
//             return EV_FS_EEXIST_MSG;
//
//         case manapi::ev::ERR_FAULT:
//             return EV_FS_EFAULT_MSG;
//
//         case manapi::ev::ERR_FBIG:
//             return EV_FS_EFBIG_MSG;
//
//         case manapi::ev::ERR_HOSTUNREACH:
//             return EV_FS_EHOSTUNREACH_MSG;
//
//         case manapi::ev::ERR_INTR:
//             return EV_FS_EINTR_MSG;
//
//         case manapi::ev::ERR_INVAL:
//             return EV_FS_EINVAL_MSG;
//
//         case manapi::ev::ERR_IO:
//             return EV_FS_EIO_MSG;
//
//         case manapi::ev::ERR_ISCONN:
//             return EV_FS_EISCONN_MSG;
//
//         case manapi::ev::ERR_ISDIR:
//             return EV_FS_EISDIR_MSG;
//
//         case manapi::ev::ERR_LOOP:
//             return EV_FS_ELOOP_MSG;
//
//         case manapi::ev::ERR_MFILE:
//             return EV_FS_EMFILE_MSG;
//
//         case manapi::ev::ERR_MSGSIZE:
//             return EV_FS_EMSGSIZE_MSG;
//
//         case manapi::ev::ERR_NAMETOOLONG:
//             return EV_FS_ENAMETOOLONG_MSG;
//
//         case manapi::ev::ERR_NETDOWN:
//             return EV_FS_ENETDOWN_MSG;
//
//         case manapi::ev::ERR_NETUNREACH:
//             return EV_FS_ENETUNREACH_MSG;
//
//         case manapi::ev::ERR_NFILE:
//             return EV_FS_ENFILE_MSG;
//
//         case manapi::ev::ERR_NOBUFS:
//             return EV_FS_ENOBUFS_MSG;
//
//         case manapi::ev::ERR_NODEV:
//             return EV_FS_ENODEV_MSG;
//
//         case manapi::ev::ERR_NOENT:
//             return EV_FS_ENOENT_MSG;
//
//         case manapi::ev::ERR_NOMEM:
//             return EV_FS_ENOMEM_MSG;
//
//         case manapi::ev::ERR_NONET:
//             return EV_FS_ENONET_MSG;
//
//         case manapi::ev::ERR_NOPROTOOPT:
//             return EV_FS_ENOPROTOOPT_MSG;
//
//         case manapi::ev::ERR_NOSPC:
//             return EV_FS_ENOSPC_MSG;
//
//         case manapi::ev::ERR_NOSYS:
//             return EV_FS_ENOSYS_MSG;
//
//         case manapi::ev::ERR_NOTCONN:
//             return EV_FS_ENOTCONN_MSG;
//
//         case manapi::ev::ERR_NOTDIR:
//             return EV_FS_ENOTDIR_MSG;
//
//         case manapi::ev::ERR_NOTEMPTY:
//             return EV_FS_ENOTEMPTY_MSG;
//
//         case manapi::ev::ERR_NOTSOCK:
//             return EV_FS_ENOTSOCK_MSG;
//
//         case manapi::ev::ERR_NOTSUP:
//             return EV_FS_ENOTSUP_MSG;
//
//         case manapi::ev::ERR_OVERFLOW:
//             return EV_FS_EOVERFLOW_MSG;
//
//         case manapi::ev::ERR_PERM:
//             return EV_FS_EPERM_MSG;
//
//         case manapi::ev::ERR_PIPE:
//             return EV_FS_EPIPE_MSG;
//
//         case manapi::ev::ERR_PROTO:
//             return EV_FS_EPROTO_MSG;
//
//         case manapi::ev::ERR_PROTONOSUPPORT:
//             return EV_FS_EPROTONOSUPPORT_MSG;
//
//         case manapi::ev::ERR_PROTOTYPE:
//             return EV_FS_EPROTOTYPE_MSG;
//
//         case manapi::ev::ERR_RANGE:
//             return EV_FS_ERANGE_MSG;
//
//         case manapi::ev::ERR_ROFS:
//             return EV_FS_EROFS_MSG;
//
//         case manapi::ev::ERR_SHUTDOWN:
//             return EV_FS_ESHUTDOWN_MSG;
//
//         case manapi::ev::ERR_SPIPE:
//             return EV_FS_ESPIPE_MSG;
//
//         case manapi::ev::ERR_SRCH:
//             return EV_FS_ESRCH_MSG;
//
//         case manapi::ev::ERR_TIMEDOUT:
//             return EV_FS_ETIMEDOUT_MSG;
//
//         case manapi::ev::ERR_TXTBSY:
//             return EV_FS_ETXTBSY_MSG;
//
//         case manapi::ev::ERR_XDEV:
//             return EV_FS_EXDEV_MSG;
//
//         case manapi::ev::ERR_UNKNOWN:
//             return EV_ERR_UNKNOWN_MSG;
//
//         case manapi::ev::ERR_OF:
//             return EV_FS_EOF_MSG;
//
//         case manapi::ev::ERR_NXIO:
//             return EV_FS_ENXIO_MSG;
//
//         case manapi::ev::ERR_MLINK:
//             return EV_FS_EMLINK_MSG;
//
//         case manapi::ev::ERR_HOSTDOWN:
//             return EV_FS_EHOSTDOWN_MSG;
//
//         case manapi::ev::ERR_REMOTEIO:
//             return EV_FS_EREMOTEIO_MSG;
//
//         case manapi::ev::ERR_NOTTY:
//             return EV_FS_ENOTTY_MSG;
//
//         case manapi::ev::ERR_FTYPE:
//             return EV_FS_EFTYPE_MSG;
//
//         case manapi::ev::ERR_ILSEQ:
//             return EV_FS_EILSEQ_MSG;
//
//         case manapi::ev::ERR_SOCKTNOSUPPORT:
//             return EV_FS_ESOCKTNOSUPPORT_MSG;
//
//         case manapi::ev::ERR_NODATA:
//             return EV_FS_ENODATA_MSG;
//
//         case manapi::ev::ERR_UNATCH:
//             return EV_FS_EUNATCH_MSG;
//         default:
//             return manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT];
//     }
// }

std::string manapi::filesystem::path::basename(std::string_view path) {
    size_t pos = path.find_last_of(std::filesystem::path::preferred_separator);

    if (pos != std::string::npos)
    {
        return std::string{path.substr(pos + 1)};
    }

    return std::string{path};
}

std::string manapi::filesystem::path::extension(std::string_view path) {
    size_t pos = path.find_last_of('.');

    if (pos != std::string::npos)
    {
        return std::string{path.substr(pos + 1)};
    }

    return std::string{};
}


void manapi::filesystem::path::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
    {
        path.push_back(std::filesystem::path::preferred_separator);
    }
}

template<typename T>
bool async_fs_operation_result_error (std::shared_ptr<manapi::ev::fs> &w, typename manapi::async::promise<T>::reject_t &reject, manapi::async::cancellation_action &cancellation) {
    if (w->result() < 0) {
        cancellation.disable();
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_FS_IO_OPERATIONS], manapi::ev::namerror(w->result()))));

        return true;
    }

    return false;
}

template<typename T>
using async_fs_operation_event_cb =  std::move_only_function<void(std::shared_ptr<manapi::ev::fs>, typename manapi::async::promise<T>::resolve_t&, typename manapi::async::promise<T>::reject_t&, manapi::async::cancellation_action &cancellation)>;

template<typename T>
void async_fs_operation_event_handler (std::shared_ptr<manapi::ev::fs> w, typename manapi::async::promise<T>::resolve_t resolve, typename manapi::async::promise<T>::reject_t reject,
    manapi::async::cancellation_action cancellation, async_fs_operation_event_cb<T> &event_cb) {
    try {
        event_cb(w, resolve,reject, cancellation);
    }
    catch (std::exception const &e) {
        reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(manapi::ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_CALLBACK], e.what())));
    }
}

struct async_fs_operation_deleter {
    manapi::async::cancellation_action c;
    ~async_fs_operation_deleter() {
        this->c.cancel();
    }
};

template<typename T>
manapi::future<T> async_fs_operation (std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb,
      async_fs_operation_event_cb<T> event_cb,  manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<T, std::false_type>;
    async_fs_operation_deleter t {cancellation};

    co_return co_await promise([&] (typename promise::resolve_t resolve, typename promise::reject_t reject)
        -> void {
        auto watcher = manapi::async::current()->eventloop()->create_watcher_fs([resolve = std::move(resolve), reject, event_cb = std::move(event_cb), cancellation] (std::shared_ptr<manapi::ev::fs> &w) mutable
            -> void {
            async_fs_operation_event_handler<T>(w, std::move(resolve), std::move(reject), std::move(cancellation), event_cb);
        });

        if (!start_cb(watcher)) {
            reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
        }

        if (cancellation.contains_cancel_callback()) {
            cancellation.cancel_callback([watcher = std::move(watcher), reject = std::move(reject)] () mutable
                -> void {
                manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(watcher));
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_CANCELLED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_CANCELLED])));
            });
        }
    });
}

manapi::future<> async_fs_simple_operation (std::move_only_function<bool(std::shared_ptr<manapi::ev::fs> w)> start_cb, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_return co_await async_fs_operation<void>(std::move(start_cb),
        [](std::shared_ptr<manapi::ev::fs> w,
            promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();

            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }

            resolve();
        }, std::move(cancellation));
}

manapi::future<bool> manapi::filesystem::async_exists(std::string path, manapi::async::cancellation_action cancellation) {
    co_return co_await filesystem::async_stat(std::move(path), nullptr, std::move(cancellation));
}

manapi::future<std::chrono::system_clock::time_point> manapi::filesystem::async_last_time_write(std::string path, manapi::async::cancellation_action cancellation) {
    uv_timespec64_t mtime;
    bool exists = co_await filesystem::async_stat(std::move(path), [&mtime, &exists] (ev::stat_t *stat)
        -> void { mtime.tv_nsec = stat->st_mtim.tv_nsec; mtime.tv_sec = stat->st_mtim.tv_sec; }, std::move(cancellation));
    if (!exists) { THROW_MANAPIHTTP_EXCEPTION2(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_FILE_NOT_FOUND]); }
    /**
     * Windows doesn't accept std::nano in the time_point,
     * so we need to remove it.
     * 
     * also we can continue using it in the Linux system
     * to saving accuracy
     */
    co_return std::chrono::system_clock::time_point (std::chrono::seconds{mtime.tv_sec}/* + std::chrono::nanoseconds{mtime.tv_nsec} */);
}

manapi::future<void> manapi::filesystem::async_mkdir(std::string path, int mode, bool recursive, manapi::async::cancellation_action cancellation) {
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

            async::cancellation_action cancellation2 = async::cancellation_action::unit (cancellation);

            co_await async_fs_operation<void>([path = path.substr(0, size), mode, cancellation2, cancellation](std::shared_ptr<ev::fs> w) mutable
                -> bool {
                    if (w->mkdir(path.data(), mode)) {
                        return false;
                    }

                    return true;
                },
                +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
                -> void {
                    cancel.disable();
                    auto rhs = w->result();
                    if ((rhs != ev::ERR_EXIST&&rhs != ev::ERR_PERM) && async_fs_operation_result_error<void>(w, reject, cancel)) {
                        return;
                    }
                    resolve();
                }, cancellation2);
        }
    }
    else {
        co_await async_fs_operation<void>([path = std::move(path), mode](std::shared_ptr<ev::fs> w)
            -> bool {
                return !(w->mkdir(path.data(), mode));
            },
            +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
            -> void {
                cancel.disable();
                auto rhs = w->result();
                if ((rhs != ev::ERR_EXIST&&rhs != ev::ERR_PERM) && async_fs_operation_result_error<void>(w,  reject, cancel)) {
                    return;
                }
                resolve();
            }, cancellation);
    }
}

manapi::future<manapi::ev::file> manapi::filesystem::async_open(std::string path, int flags, int mode, manapi::async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ev::file>;
    
    auto fileno = co_await async_fs_operation<ev::file>([path = std::move(path), flags, mode] (std::shared_ptr<ev::fs> w)
        -> bool {
        return !w->open(path.data(), flags, mode);
    }, +[] (std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
    -> void {
        cancel.disable();
        if (async_fs_operation_result_error<void>(w, reject, cancel)) {
            return;
        }
        resolve (w->result());
    }, cancellation);
    co_return fileno;
}

manapi::future<void> manapi::filesystem::async_close(ev::file file, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<void>;

    co_await async_fs_operation<void>([file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->close(file);
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve();
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_write(ev::file file, const void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
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
    async_fs_operation_event_cb<ssize_t> event_cb;
};

struct async_write_data_t {
    ssize_t result;
    manapi::ev::buff_t *buff;
    uint32_t nbuff;
    manapi::ev::file file;
    int64_t offset;
    async_fs_operation_event_cb<ssize_t> event_cb;
};


manapi::future<ssize_t> manapi::filesystem::async_read(ev::file file, void *data, ssize_t size, int64_t offset, manapi::async::cancellation_action cancellation) {
    ev::buff_t buff;
    buff.base = static_cast<char*>(data);
    buff.len = size;
    co_return co_await async_read (file, &buff, 1, offset, std::move(cancellation));
}

manapi::future<> manapi::filesystem::async_write(std::string path, std::string data, int mode, int flags, int64_t offset, manapi::async::cancellation_action cancellation) {
    std::exception_ptr err;
    ev::file fileno = 0;

    try {
        fileno = co_await async_open(path, flags, mode, manapi::async::cancellation_action::unit(cancellation));
        auto rhs = co_await async_write(fileno, data.data(), data.size(), offset, manapi::async::cancellation_action::unit(cancellation));
        if (rhs != data.size()) {
            err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_SIZE_NOT_SAME]));
        }
    }
    catch (std::exception const &e) {
        err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_BY_ERROR], e.what()));
    }

    if (fileno > 0) {
        async::run(filesystem::async_close(fileno));
    }

    if (err) {
        std::rethrow_exception(err);
    }
}

manapi::future<std::string> manapi::filesystem::async_read(std::string path, int flags, int64_t offset, manapi::async::cancellation_action cancellation) {
    std::exception_ptr err;
    ev::file fileno = 0;
    std::string data;
    ssize_t len = 0;

    try {
        fileno = co_await async_open(path, flags, 0, manapi::async::cancellation_action::unit(cancellation));
        co_await async_fstat(fileno, [&len] (ev::stat_t *stat)
            -> void {
            len = stat ? static_cast<ssize_t>(stat->st_size) : -1;
        }, manapi::async::cancellation_action::unit(cancellation));
        if (len == -1) { err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2 (ERR_NOT_FOUND, "file not exists")); }
        data.resize(len);
        auto rhs = co_await async_read(fileno, data.data(), len, offset, manapi::async::cancellation_action::unit(cancellation));
        if (rhs != len) {
            err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_SIZE_NOT_SAME]));
        }
    }
    catch (std::exception const &e) {
        err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_BY_ERROR], e.what()));
    }

    if (fileno > 0) {
        async::run(filesystem::async_close(fileno));
    }

    if (err) {
        std::rethrow_exception(err);
    }

    co_return std::move(data);
}

manapi::future<ssize_t> manapi::filesystem::async_write(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    if (!nbuff)
        co_return 0;

    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        auto rhs = ev::fs::try_write(file, buff->base + shift, buff->len - shift, offset);

        if (rhs > 0)
            co_return rhs;

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
        co_return res;

    async_write_data_t dd{};

    buff->base += shift;
    buff->len -= shift;

    dd.event_cb = nullptr;
    dd.buff = buff;
    dd.result = res;
    dd.nbuff = nbuff;
    dd.file = file;
    dd.offset = offset;
    dd.event_cb = [&dd](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
        if (async_fs_operation_result_error<ssize_t>(w, reject, cancel)) {
            return;
        }

        auto rhs = w->result();

        if (rhs < 0) {
            /* ignore ? */
            if (dd.result) {
                rhs = dd.result;
            }
            cancel.disable();
            resolve(rhs);
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
            auto w1 = manapi::async::current()->eventloop()->create_watcher_fs([&dd, resolve, reject, cancel] (std::shared_ptr<ev::fs> w) mutable
                -> void {
                async_fs_operation_event_handler<ssize_t>(std::move(w), std::move(resolve), std::move(reject), std::move(cancel), dd.event_cb);
            });

            dd.buff->base += rhs;
            dd.buff->len -= rhs;

            if (w1->write(dd.file, dd.buff, dd.nbuff, dd.offset)) {
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FILESYSTEM_FAILED,manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w1)] () mutable
                    -> void { manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
            }
        }
        else {
            cancel.disable();
            resolve(dd.result);
        }
    };

    res = co_await async_fs_operation<ssize_t>([&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->write(dd.file, dd.buff, dd.nbuff, dd.offset);
        }, [&dd] (std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) -> void {
            dd.event_cb(std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return res;
}

manapi::future<ssize_t> manapi::filesystem::async_read(ev::file file, ev::buff_t *buff, uint32_t nbuff, int64_t offset, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    if (!nbuff)
        co_return 0;

    ssize_t res = 0;
    std::size_t shift = 0;

    while (nbuff) {
        ssize_t rhs = ev::fs::try_read(file, buff->base + shift, buff->len - shift, offset);

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

    dd.event_cb = [&dd](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
        if (async_fs_operation_result_error<ssize_t>(w, reject, cancel)) {
            return;
        }

        auto rhs = w->result();

        if (rhs < 0) {
            if (dd.result) {
                rhs = dd.result;
            }
            cancel.disable();
            resolve(rhs);
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
            auto w  = manapi::async::current()->eventloop()->create_watcher_fs([&dd, resolve, reject, cancel] (std::shared_ptr<ev::fs> w) mutable
                -> void {
                async_fs_operation_event_handler<ssize_t>( std::move(w), std::move(resolve), std::move(reject), std::move(cancel), dd.event_cb);
            });

            dd.buff->base += rhs;
            dd.buff->len -= rhs;

            if (w->read(dd.file, dd.buff, dd.nbuff, dd.offset)) {
                reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_FS_FAILURE_INIT])));
                return;
            }

            if (cancel.contains_cancel_callback()) {
                cancel.cancel_callback([w = std::move(w)] () mutable
                    -> void { manapi::async::current()->eventloop()->stop_watcher<manapi::ev::fs>(std::move(w)); });
            }
        }
        else {
            cancel.disable();
            resolve(dd.result);
        }
    };

    res = co_await async_fs_operation<ssize_t>([&dd] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->read(dd.file, dd.buff, dd.nbuff, dd.offset);
        }, [&dd](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel)
        -> void {
            dd.event_cb(std::move(w), resolve, reject, cancel);
        }, cancellation);

    co_return res;
}

manapi::future<ssize_t> manapi::filesystem::async_write(ev::file file, slice_view slice, int64_t offset, async::cancellation_action cancellation) {
    auto buffs = slice.slices_buffs();
    co_return co_await async_write(file, buffs.get(), slice.slices_size(), offset, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_read(ev::file file, slice_view slice, int64_t offset, async::cancellation_action cancellation) {
    auto buffs = slice.slices_buffs();
    co_return co_await async_read(file, buffs.get(), slice.slices_size(), offset, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_file_size(std::string path, manapi::async::cancellation_action cancellation) {
    ssize_t size;

    co_await filesystem::async_stat(std::move(path), [&size] (ev::stat_t *stat)
        -> void {
        if (stat) {
            size = static_cast<ssize_t>(stat->st_size);
        }
        else {
            size = -1;
        }
    }, std::move(cancellation));

    if (size == -1) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FILESYSTEM_FAILED, manapi::error::default_msgs[manapi::error::ERRMSG_WHEN_RECV_ADDITIONAL]);
    }

    co_return size;
}

manapi::future<bool> manapi::filesystem::async_stat(std::string path, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->stat(path.data());
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();

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

manapi::future<bool> manapi::filesystem::async_fstat(ev::file file, std::move_only_function<void(ev::stat_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>([file] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->fstat(file);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();

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

std::string manapi::filesystem::path::clean (std::string_view str) {
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

    return std::move(cleaned);
}

manapi::future<void> manapi::filesystem::async_unlink (std::string path, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->unlink(path.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_rmdir (std::string path, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rmdir(path.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_closedir (manapi::ev::dir_t *directory, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([directory] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->closedir(directory); }, std::move(cancellation));
}

manapi::future<bool> manapi::filesystem::async_statfs (std::string path, std::move_only_function<void(ev::statfs_t *data)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->statfs(path.data());
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();

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

manapi::future<void> manapi::filesystem::async_rename (std::string oldpath, std::string newpath, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([oldpath = std::move(oldpath), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->rename(oldpath.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_copyfile (std::string src, std::string dest, int flags, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([src = std::move(src), dest = std::move(dest), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->copyfile(src.data(), dest.data(), flags); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_chmod (std::string path, int mode, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chmod(path.data(), mode); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fchmod (ev::file file, int mode, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([file, mode] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchmod(file, mode); }, std::move(cancellation));
}

manapi::future<int> manapi::filesystem::async_access (std::string path, int mode, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<bool>;

    co_return co_await async_fs_operation<bool>([path = std::move(path), mode] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->access(path.data(), mode);
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            resolve(w->result());
        }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_utime (std::string path, double atime, double mtime, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path), atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->utime(path.data(), atime, mtime); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_futime (ev::file file, double atime, double mtime, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([file, atime, mtime] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->futime(file, atime, mtime); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_link (std::string path, std::string newpath, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path), newpath = std::move(newpath)] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->link(path.data(), newpath.data()); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_symlink (std::string path, std::string newpath, int flags, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path = std::move(path), newpath = std::move(newpath), flags] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->symlink(path.data(), newpath.data(), flags); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fsync (ev::file file, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fsync(file); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fdatasync(ev::file file, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([file] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fdatasync(file); }, std::move(cancellation));
}

manapi::future<manapi::ev::dir_t *> manapi::filesystem::async_opendir(std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<manapi::ev::dir_t *>;

    co_return co_await async_fs_operation<manapi::ev::dir_t *>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->opendir(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve(static_cast<ev::dir_t *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_readlink (std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readlink(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve(static_cast<const char *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_realpath (std::string path, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>([path = std::move(path)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->realpath(path.data());
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve(static_cast<const char *> (w->custom()->ptr));
        }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_chown(std::string path, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([path=std::move(path), uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->chown(path.data(), uid, gid); }, std::move(cancellation));
}

manapi::future<void> manapi::filesystem::async_fchown(ev::file file, ev::uid_t uid, ev::gid_t gid, async::cancellation_action cancellation) {
    co_await async_fs_simple_operation ([file, uid, gid] (std::shared_ptr<ev::fs> w)
        -> bool { return !w->fchown(file, uid, gid); }, std::move(cancellation));
}

manapi::future<std::string> manapi::filesystem::async_mkdtemp(std::string tpl, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::string>;

    co_return co_await async_fs_operation<std::string>([tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkdtemp(tpl.data());
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve(std::string{w->custom()->path});
        }, std::move(cancellation));
}

manapi::future<std::pair<std::string, manapi::ev::file>> manapi::filesystem::async_mkstemp(std::string tpl, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<std::pair<std::string, manapi::ev::file>>;

    co_return co_await async_fs_operation<std::pair<std::string, manapi::ev::file>>([tpl = std::move(tpl)] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->mkstemp(tpl.data());
        }, +[](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            resolve(std::make_pair(std::string{w->custom()->path}, static_cast<ev::file>(w->custom()->result)));
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_scandir (std::string path, int flags, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    co_return co_await async_fs_operation<ssize_t>([path = std::move(path), flags] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->scandir(path.data(), flags);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr);
            resolve(ptr->nentries);
        }, std::move(cancellation));
}

manapi::future<ssize_t> manapi::filesystem::async_readdir (ev::dir_t *dir, std::move_only_function<void(ev::dir_t *dir)> callback, async::cancellation_action cancellation) {
    using promise = manapi::async::promise<ssize_t>;

    co_return co_await async_fs_operation<ssize_t>([dir] (std::shared_ptr<ev::fs> w)
        -> bool {
            return !w->readdir(dir);
        }, [callback = std::move(callback)](std::shared_ptr<ev::fs> w, promise::resolve_t &resolve, promise::reject_t &reject, manapi::async::cancellation_action &cancel) mutable
        -> void {
            cancel.disable();
            if (async_fs_operation_result_error<void>(w, reject, cancel)) {
                return;
            }
            auto ptr = static_cast<ev::dir_t *> (w->custom()->ptr);
            callback(ptr);
            resolve(ptr->nentries);
        }, std::move(cancellation));
}