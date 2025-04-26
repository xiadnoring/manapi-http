#pragma once

#include "uv.h"

#include "ManapiDebug.hpp"

#define MANAPI_EV_NODISCARD [[nodiscard]]
#define MANAPI_EV_NOEXPECT noexcept(true)
#define MANAPI_EV_CAST_STREAM(x) reinterpret_cast<uv_stream_t *> (x)
#define MANAPI_EV_CAST_HANDLE(x) reinterpret_cast <uv_handle_t *> (x)
#define MANAPI_EV_DEFAULT_PRIVATE_VAR(name_class, name_struct)
#define MANAPI_EV_DEFAULT(name_class, name_struct) \
        void unbind (uv_close_cb cb) MANAPI_EV_NOEXPECT;\
        void unbind () MANAPI_EV_NOEXPECT;\
        void data (void *data) MANAPI_EV_NOEXPECT;\
        void *data () MANAPI_EV_NOEXPECT; \
        loop_ref loop () MANAPI_EV_NOEXPECT; \
        name_struct* custom () MANAPI_EV_NOEXPECT; \
        bool is_active () MANAPI_EV_NOEXPECT; \
        ~name_class ();
#define MANAPI_EV_STREAM(name_class, name_struct) \
        int listen (int backlog, uv_connection_cb cb) MANAPI_EV_NOEXPECT; \
        static int ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPI_EV_NOEXPECT; \
        static int ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPI_EV_NOEXPECT;
#define MANAPI_EV_CHECK(expr) if (expr) { THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_EXTERNAL_LIB_CRASH, #expr); }

namespace manapi {
    typedef uv_os_fd_t fd_t;
    typedef uv_os_sock_t socket_t;
}

namespace manapi::ev {
    enum types {
        EV_TIMER = 0,
        EV_WRITE,
        EV_IO,
        EV_TCP,
        EV_UDP,
        EV_PREPARE,
        EV_CHECK,
        EV_IDLE,
        EV_ASYNC
    };
    enum flags {
        READ = UV_READABLE,
        WRITE = UV_WRITABLE,
        DISCONNECT = UV_DISCONNECT,
        PRIORITIZED = UV_PRIORITIZED
    };
    enum fs_o_flags {
        FS_O_APPEND = UV_FS_O_APPEND,
        FS_O_CREAT = UV_FS_O_CREAT,
        FS_O_DIRECT = UV_FS_O_DIRECT,
        FS_O_DIRECTORY = UV_FS_O_DIRECTORY,
        FS_O_DSYNC = UV_FS_O_DSYNC,
        FS_O_EXCL = UV_FS_O_EXCL,
        FS_O_EXLOCK = UV_FS_O_EXLOCK,
        FS_O_FILEMAP = UV_FS_O_FILEMAP,
        FS_O_NOATIME = UV_FS_O_NOATIME,
        FS_O_NOCTTY = UV_FS_O_NOCTTY,
        FS_O_NOFOLLOW = UV_FS_O_NOFOLLOW,
        FS_O_NONBLOCK = UV_FS_O_NONBLOCK,
        FS_O_RANDOM = UV_FS_O_RANDOM,
        FS_O_RDONLY = UV_FS_O_RDONLY,
        FS_O_RDWR = UV_FS_O_RDWR,
        FS_O_SEQUENTIAL = UV_FS_O_SEQUENTIAL,
        FS_O_SHORT_LIVED = UV_FS_O_SHORT_LIVED,
        FS_O_SYMLINK = UV_FS_O_SYMLINK,
        FS_O_SYNC = UV_FS_O_SYNC,
        FS_O_TEMPORARY = UV_FS_O_TEMPORARY,
        FS_O_TRUNC = UV_FS_O_TRUNC,
        FS_O_WRONLY = UV_FS_O_WRONLY
    };

#if defined(_WIN32)
    enum fs_o_modes {
        IRUSR = 0,
        IWUSR = 0,
        IXUSR = 0,
        IRWXU = 0,

        IRGRP = 0,
        IWGRP = 0,
        IXGRP = 0,
        IRWXG = 0,

        IROTH = 0,
        IWOTH = 0,
        IXOTH = 0,
        IRWXO = 0
    };
#else
    enum fs_o_modes {
        IRUSR = S_IRUSR,
        IWUSR = S_IWUSR,
        IXUSR = S_IXUSR,
        IRWXU = S_IRWXU,

        IRGRP = S_IRGRP,
        IWGRP = S_IWGRP,
        IXGRP = S_IXGRP,
        IRWXG = S_IRWXG,

        IROTH = S_IROTH,
        IWOTH = S_IWOTH,
        IXOTH = S_IXOTH,
        IRWXO = S_IRWXO
    };
#endif

    enum fs_errors {
        FS_E2BIG = UV_E2BIG,
        FS_EACCES = UV_EACCES,
        FS_EADDRINUSE = UV_EADDRINUSE,
        FS_EADDRNOTAVAIL = UV_EADDRNOTAVAIL,
        FS_EAFNOSUPPORT = UV_EAFNOSUPPORT,
        FS_EAGAIN = UV_EAGAIN ,
        FS_EAI_ADDRFAMILY = UV_EAI_ADDRFAMILY ,
        FS_EAI_AGAIN = UV_EAI_AGAIN ,
        FS_EAI_BADFLAGS = UV_EAI_BADFLAGS ,
        FS_EAI_BADHINTS = UV_EAI_BADHINTS ,
        FS_EAI_CANCELED = UV_EAI_CANCELED ,
        FS_EAI_FAIL = UV_EAI_FAIL ,
        FS_EAI_FAMILY = UV_EAI_FAMILY ,
        FS_EAI_MEMORY = UV_EAI_MEMORY ,
        FS_EAI_NODATA = UV_EAI_NODATA ,
        FS_EAI_NONAME = UV_EAI_NONAME ,
        FS_EAI_OVERFLOW = UV_EAI_OVERFLOW ,
        FS_EAI_PROTOCOL = UV_EAI_PROTOCOL ,
        FS_EAI_SERVICE = UV_EAI_SERVICE ,
        FS_EAI_SOCKTYPE = UV_EAI_SOCKTYPE ,
        FS_EALREADY = UV_EALREADY ,
        FS_EBADF = UV_EBADF ,
        FS_EBUSY = UV_EBUSY ,
        FS_ECANCELED = UV_ECANCELED ,
        FS_ECHARSET = UV_ECHARSET ,
        FS_ECONNABORTED = UV_ECONNABORTED ,
        FS_ECONNREFUSED = UV_ECONNREFUSED ,
        FS_ECONNRESET = UV_ECONNRESET ,
        FS_EDESTADDRREQ = UV_EDESTADDRREQ ,
        FS_EEXIST = UV_EEXIST ,
        FS_EFAULT = UV_EFAULT ,
        FS_EFBIG = UV_EFBIG ,
        FS_EHOSTUNREACH = UV_EHOSTUNREACH ,
        FS_EINTR = UV_EINTR ,
        FS_EINVAL = UV_EINVAL ,
        FS_EIO = UV_EIO ,
        FS_EISCONN = UV_EISCONN ,
        FS_EISDIR = UV_EISDIR ,
        FS_ELOOP = UV_ELOOP ,
        FS_EMFILE = UV_EMFILE ,
        FS_EMSGSIZE = UV_EMSGSIZE ,
        FS_ENAMETOOLONG = UV_ENAMETOOLONG ,
        FS_ENETDOWN = UV_ENETDOWN ,
        FS_ENETUNREACH = UV_ENETUNREACH ,
        FS_ENFILE = UV_ENFILE ,
        FS_ENOBUFS = UV_ENOBUFS ,
        FS_ENODEV = UV_ENODEV ,
        FS_ENOENT = UV_ENOENT ,
        FS_ENOMEM = UV_ENOMEM ,
        FS_ENONET = UV_ENONET ,
        FS_ENOPROTOOPT = UV_ENOPROTOOPT ,
        FS_ENOSPC = UV_ENOSPC ,
        FS_ENOSYS = UV_ENOSYS ,
        FS_ENOTCONN = UV_ENOTCONN ,
        FS_ENOTDIR = UV_ENOTDIR ,
        FS_ENOTEMPTY = UV_ENOTEMPTY ,
        FS_ENOTSOCK = UV_ENOTSOCK ,
        FS_ENOTSUP = UV_ENOTSUP ,
        FS_EOVERFLOW = UV_EOVERFLOW ,
        FS_EPERM = UV_EPERM ,
        FS_EPIPE = UV_EPIPE ,
        FS_EPROTO = UV_EPROTO ,
        FS_EPROTONOSUPPORT = UV_EPROTONOSUPPORT ,
        FS_EPROTOTYPE = UV_EPROTOTYPE ,
        FS_ERANGE = UV_ERANGE ,
        FS_EROFS = UV_EROFS ,
        FS_ESHUTDOWN = UV_ESHUTDOWN ,
        FS_ESPIPE = UV_ESPIPE ,
        FS_ESRCH = UV_ESRCH ,
        FS_ETIMEDOUT = UV_ETIMEDOUT ,
        FS_ETXTBSY = UV_ETXTBSY ,
        FS_EXDEV = UV_EXDEV ,
        FS_UNKNOWN = UV_UNKNOWN ,
        FS_EOF = UV_EOF ,
        FS_ENXIO = UV_ENXIO ,
        FS_EMLINK = UV_EMLINK ,
        FS_EHOSTDOWN = UV_EHOSTDOWN ,
        FS_EREMOTEIO = UV_EREMOTEIO ,
        FS_ENOTTY = UV_ENOTTY ,
        FS_EFTYPE = UV_EFTYPE ,
        FS_EILSEQ = UV_EILSEQ ,
        FS_ESOCKTNOSUPPORT = UV_ESOCKTNOSUPPORT ,
        FS_ENODATA = UV_ENODATA ,
        FS_EUNATCH = UV_EUNATCH,
        FS_ERRNO_MAX = UV_ERRNO_MAX
    };

    typedef uv_dir_t dir_t;
    typedef uv_file file;
    typedef uv_loop_t *loop_ref;
    typedef uv_uid_t uid_t;
    typedef uv_gid_t gid_t;
    typedef uv_dirent_t dirent_t;
    typedef uv_statfs_t statfs_t;
    typedef uv_stat_t stat_t;

    void callback_watcher_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    void callback_watcher_async (uv_async_t *s);
    void callback_watcher_timer (uv_timer_t *s);
    void callback_watcher_io (uv_poll_t *s, int status, int revents);
    void callback_watcher_idle (uv_idle_t *s);
    void callback_watcher_check (uv_check_t *s);
    void callback_watcher_prepare (uv_prepare_t *s);
    void callback_watcher_tcp_accept (uv_tcp_t *s, int status);
    void callback_watcher_tcp_read (uv_stream_t *s,  ssize_t nread, const uv_buf_t *buf);
    void callback_watcher_udp_recv (uv_udp_t *s, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags);
    void callback_watcher_udp_send (uv_udp_send_t *s, int status);
    void callback_watcher_write (uv_write_t *s, int status);
    void callback_watcher_fs (uv_fs_t *req);

    class async {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(async, uv_async_t)
    public:
        MANAPI_EV_DEFAULT(async, uv_async_t)

        async (loop_ref loop);
        async (loop_ref loop, uv_async_cb cb);

        int send () MANAPI_EV_NOEXPECT;

        void set (uv_async_cb cb = callback_watcher_async) MANAPI_EV_NOEXPECT;
    private:

        uv_async_t s_;
    };

    class idle {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(idle, uv_idle_t)
    public:
        MANAPI_EV_DEFAULT(idle, uv_idle_t)
        idle (loop_ref loop);

        int init (loop_ref loop) MANAPI_EV_NOEXPECT;

        int start () MANAPI_EV_NOEXPECT;
        int start (uv_idle_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_idle_t s_;
    };

    class check {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(check, uv_check_t)
    public:
        MANAPI_EV_DEFAULT(check, uv_check_t)
        check (loop_ref loop);

        int init (loop_ref loop) MANAPI_EV_NOEXPECT;
        int start () MANAPI_EV_NOEXPECT;
        int start (uv_check_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_check_t s_;
    };

    class io {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(io, uv_poll_t)
    public:
        MANAPI_EV_DEFAULT(io, uv_poll_t)

        template<typename T1 = manapi::fd_t>
        requires(sizeof (manapi::fd_t) != sizeof (manapi::socket_t))
        io (loop_ref loop, manapi::socket_t fd) : s_() {
            MANAPI_EV_CHECK(uv_poll_init_socket(loop, &this->s_, fd));
        }

        io (loop_ref loop, manapi::fd_t fd) : s_() {
            MANAPI_EV_CHECK(uv_poll_init(loop, &this->s_, fd));
        }

        int start (int revents, uv_poll_cb cb) MANAPI_EV_NOEXPECT;
        int start (int revents) MANAPI_EV_NOEXPECT;
        int start () MANAPI_EV_NOEXPECT;
        int restart (int revents) MANAPI_EV_NOEXPECT;
        int stop () MANAPI_EV_NOEXPECT;
        int events () MANAPI_EV_NOEXPECT;
    private:
        uv_poll_t s_;
    };

    class write {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(write, uv_write_t)
    public:
        MANAPI_EV_DEFAULT(write, uv_write_t)

        write (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb);
        write (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs);
    private:
        uv_write_t s_{};
    };

    class tcp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(tcp, uv_tcp_t)
    public:
        MANAPI_EV_DEFAULT(tcp, uv_tcp_t)
        MANAPI_EV_STREAM(tcp, uv_tcp_t)

        tcp (loop_ref loop);

        int accept (tcp *parent) MANAPI_EV_NOEXPECT;

        int read_start () MANAPI_EV_NOEXPECT;
        int read_start (uv_alloc_cb alloc, uv_read_cb cb) MANAPI_EV_NOEXPECT;

        int read_stop () MANAPI_EV_NOEXPECT;

        int bind (sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;
    private:
        uv_tcp_t s_;
    };

    class udp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp, uv_udp_t)
    public:
        MANAPI_EV_DEFAULT(udp, uv_udp_t)
        MANAPI_EV_STREAM(udp, uv_udp_t)

        udp (loop_ref loop);

        int bind (sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;

        int recv_start () MANAPI_EV_NOEXPECT;
        int recv_start (uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPI_EV_NOEXPECT;

        int recv_stop () MANAPI_EV_NOEXPECT;

        int try_send (const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPI_EV_NOEXPECT;
    private:
        uv_udp_t s_;
    };

    class udp_send {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp_send, uv_udp_send_t)
    public:
        MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)

        udp_send (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb, const sockaddr *addr);
        udp_send (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr);
    private:
        uv_udp_send_t s_;
    };

    class prepare {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(prepare, uv_prepare_t)
    public:
        MANAPI_EV_DEFAULT(prepare, uv_prepare_t)

        prepare (loop_ref loop);

        int start () MANAPI_EV_NOEXPECT;
        int start (uv_prepare_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;
    private:
        uv_prepare_t s_;
    };

    class timer {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(timer, uv_timer_t)
    public:
        MANAPI_EV_DEFAULT(timer, uv_timer_t)

        timer (loop_ref loop);

        int start (uint64_t timeout, uint64_t repeat) MANAPI_EV_NOEXPECT;
        int start (uint64_t timeout, uint64_t repeat, uv_timer_cb cb) MANAPI_EV_NOEXPECT;

        int stop () MANAPI_EV_NOEXPECT;

        int again () MANAPI_EV_NOEXPECT;

        void repeat (uint64_t repeat)  MANAPI_EV_NOEXPECT;

        MANAPI_EV_NODISCARD uint64_t repeat () const MANAPI_EV_NOEXPECT;

        MANAPI_EV_NODISCARD uint64_t due_in () const MANAPI_EV_NOEXPECT;
    private:
        uv_timer_t s_;
    };

    class fs {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(fs, uv_fs_t)
    public:
        MANAPI_EV_DEFAULT(fs, uv_fs_t)

        fs (loop_ref loop);

        int open (const char *path, int flags, int mode, uv_fs_cb open_cb) MANAPI_EV_NOEXPECT;
        int open (const char *path, int flags, int mode) MANAPI_EV_NOEXPECT;

        int read (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb read_cb) MANAPI_EV_NOEXPECT;
        int read (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT;

        int write (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb write_cb) MANAPI_EV_NOEXPECT;
        int write (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT;

        static ssize_t try_write (ev::file fileno, const void *buff, ssize_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT;
        static ssize_t try_read (ev::file fileno, void *buff, ssize_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT;

        int close (ev::file fileno, uv_fs_cb close_cb) MANAPI_EV_NOEXPECT;
        int close (ev::file fileno) MANAPI_EV_NOEXPECT;

        int unlink (const char *path, uv_fs_cb unlink_cb) MANAPI_EV_NOEXPECT;
        int unlink (const char *path) MANAPI_EV_NOEXPECT;

        int mkdir (const char *path, int mode, uv_fs_cb mkdir_cb) MANAPI_EV_NOEXPECT;
        int mkdir (const char *path, int mode) MANAPI_EV_NOEXPECT;

        int mkdtemp (const char *path, uv_fs_cb mkdtemp_cb) MANAPI_EV_NOEXPECT;
        int mkdtemp (const char *path) MANAPI_EV_NOEXPECT;

        int mkstemp (const char *path, uv_fs_cb mkstemp_cb) MANAPI_EV_NOEXPECT;
        int mkstemp (const char *path) MANAPI_EV_NOEXPECT;

        int rmdir (const char *path, uv_fs_cb rmdir_cb) MANAPI_EV_NOEXPECT;
        int rmdir (const char *path) MANAPI_EV_NOEXPECT;

        int opendir (const char *path, uv_fs_cb opendir) MANAPI_EV_NOEXPECT;
        int opendir (const char *path) MANAPI_EV_NOEXPECT;

        int closedir (ev::dir_t * dir, uv_fs_cb closedir_cb) MANAPI_EV_NOEXPECT;
        int closedir (ev::dir_t * dir) MANAPI_EV_NOEXPECT;

        int readdir (ev::dir_t * dir, uv_fs_cb readdir_cb) MANAPI_EV_NOEXPECT;
        int readdir (ev::dir_t * dir) MANAPI_EV_NOEXPECT;

        int scandir (const char *path, int flags, uv_fs_cb scandir_cb) MANAPI_EV_NOEXPECT;
        int scandir (const char *path, int flags) MANAPI_EV_NOEXPECT;

        int scandir_next (ev::dirent_t *dir) MANAPI_EV_NOEXPECT;

        int stat (const char *path, uv_fs_cb stat_cb) MANAPI_EV_NOEXPECT;
        int stat (const char *path) MANAPI_EV_NOEXPECT;

        int fstat (ev::file file, uv_fs_cb fstat_cb) MANAPI_EV_NOEXPECT;
        int fstat (ev::file file) MANAPI_EV_NOEXPECT;

        int lstat (const char *path, uv_fs_cb lstat_cb) MANAPI_EV_NOEXPECT;
        int lstat (const char *path) MANAPI_EV_NOEXPECT;

        int statfs (const char *path, uv_fs_cb statfs_cb) MANAPI_EV_NOEXPECT;
        int statfs (const char *path) MANAPI_EV_NOEXPECT;

        int rename (const char *path, const char *new_path, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int rename (const char *path, const char *new_path) MANAPI_EV_NOEXPECT;

        int fsync (ev::file file, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int fsync (ev::file file) MANAPI_EV_NOEXPECT;

        int fdatasync (ev::file file, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int fdatasync (ev::file file) MANAPI_EV_NOEXPECT;

        int ftruncate (ev::file file, int64_t off, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int ftruncate (ev::file file, int64_t off) MANAPI_EV_NOEXPECT;

        int copyfile (const char *path1, const char *path2, int flags, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int copyfile (const char *path1, const char *path2, int flags) MANAPI_EV_NOEXPECT;

        int sendfile (ev::file outfd, ev::file infd, int64_t off, size_t length, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int sendfile (ev::file outfd, ev::file infd, int64_t off, size_t length) MANAPI_EV_NOEXPECT;

        int access (const char *path, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int access (const char *path, int mode) MANAPI_EV_NOEXPECT;

        int chmod (const char *path, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int chmod (const char *path, int mode) MANAPI_EV_NOEXPECT;

        int fchmod (ev::file file, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int fchmod (ev::file file, int mode) MANAPI_EV_NOEXPECT;

        int utime (const char *path, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int utime (const char *path, double atime, double mtime) MANAPI_EV_NOEXPECT;

        int futime (ev::file file, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int futime (ev::file file, double atime, double mtime) MANAPI_EV_NOEXPECT;

        int lutime (const char *path, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int lutime (const char *path, double atime, double mtime) MANAPI_EV_NOEXPECT;

        int link (const char *path, const char *new_path, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int link (const char *path, const char *new_path) MANAPI_EV_NOEXPECT;

        int symlink (const char *path, const char *new_path, int flags, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int symlink (const char *path, const char *new_path, int flags) MANAPI_EV_NOEXPECT;

        int readlink (const char *path, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int readlink (const char *path) MANAPI_EV_NOEXPECT;

        int realpath (const char *path, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int realpath (const char *path) MANAPI_EV_NOEXPECT;

        int chown (const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int chown (const char *path, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT;

        int fchown (ev::file file, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int fchown (ev::file file, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT;

        int lchown (const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT;
        int lchown (const char *path, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT;
        
        MANAPI_EV_NODISCARD ssize_t result () const MANAPI_EV_NOEXPECT;
    private:
        loop_ref loop_;
        uv_fs_t s_;
    };
}

#undef MANAPI_EV_CAST_HANDLE
#undef MANAPI_EV_DEFAULT_PRIVATE_VAR
#undef MANAPI_EV_STREAM
#undef MANAPI_EV_CHECK
#undef MANAPI_EV_DEFAULT
#undef MANAPI_EV_CAST_STREAM