#pragma once

#include <type_traits>
#include <memory>

#include "uv.h"
#include "../ManapiUtils.hpp"
#include "../ManapiDebug.hpp"

#ifdef _WIN32
#   include <io.h>
#endif

#define MANAPI_EV_NODISCARD [[nodiscard]]
#define MANAPI_EV_NOEXPECT MANAPIHTTP_NOEXPECT
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
        int listen (int tcp_backlog, uv_connection_cb cb) MANAPI_EV_NOEXPECT; \
        static int ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPI_EV_NOEXPECT; \
        static int ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPI_EV_NOEXPECT;
#define MANAPI_EV_CHECK(expr) if (expr) { THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_INTERNAL, #expr); }

namespace manapi {
    typedef uv_os_fd_t fd_t;
    typedef uv_os_sock_t socket_t;
}

namespace manapi::ev {
    typedef uv_buf_t buff_t;
    typedef uv_stream_t stream_t;

    enum pf_ip_types {
        IPv4 = PF_INET,
        IPv6 = PF_INET6
    };

    enum udp_flags {
        UDP_IPV6ONLY = UV_UDP_IPV6ONLY,
        UDP_REUSEADDR = UV_UDP_REUSEADDR,
        UDP_REUSEPORT = UV_UDP_REUSEPORT,
        UDP_MMSG_CHUNK = UV_UDP_MMSG_CHUNK,
        UDP_MMSG_FREE = UV_UDP_MMSG_FREE,
        UDP_PARTIAL = UV_UDP_PARTIAL,
        UDP_LINUX_RECVERR = UV_UDP_LINUX_RECVERR,
        UDP_RECVMMSG = UV_UDP_RECVMMSG
    };

    enum tcp_flags {
        TCP_IPV6ONLY = UV_TCP_IPV6ONLY,
        TCP_REUSEPORT = UV_TCP_REUSEPORT
    };

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
        IRUSR = S_IREAD,
        IWUSR = S_IWRITE,
        IXUSR = S_IEXEC,
        IRWXU = S_IREAD|S_IWRITE|S_IEXEC,

        IRGRP = S_IREAD,
        IWGRP = S_IWRITE,
        IXGRP = S_IEXEC,
        IRWXG = S_IREAD|S_IWRITE|S_IEXEC,

        IROTH = S_IREAD,
        IWOTH = S_IWRITE,
        IXOTH = S_IEXEC,
        IRWXO = S_IREAD|S_IWRITE|S_IEXEC,

        IFREG = S_IFREG,
        IFSOCK = 0,
        IFLNK = 0,
        IFBLK = 0,
        IFDIR = S_IFDIR,
        IFCHR = S_IFCHR,
        IFIFO = 0,
        IFMT = S_IFMT
    };
#else
    enum fs_o_stat {
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
        IRWXO = S_IRWXO,

        IFREG = S_IFREG,
        IFSOCK = S_IFSOCK,
        IFLNK = S_IFLNK,
        IFBLK = S_IFBLK,
        IFDIR = S_IFDIR,
        IFCHR = S_IFCHR,
        IFIFO = S_IFIFO
    };
#endif

    enum sys_errors {
        ERR_SRCH = UV_ESRCH , /*3*/
        ERR_2BIG = UV_E2BIG,/*7*/
        ERR_BADF = UV_EBADF ,
        ERR_AGAIN = UV_EAGAIN , /*11*/
        ERR_ACCES = UV_EACCES, /*13*/
        ERR_AFNOSUPPORT = UV_EAFNOSUPPORT, /*97*/
        ERR_ADDRINUSE = UV_EADDRINUSE, /*98*/
        ERR_ADDRNOTAVAIL = UV_EADDRNOTAVAIL, /*99*/
        ERR_AI_ADDRFAMILY = UV_EAI_ADDRFAMILY ,/*3000*/
        ERR_AI_AGAIN = UV_EAI_AGAIN ,/*3001*/
        ERR_AI_BADFLAGS = UV_EAI_BADFLAGS ,/*3002*/
        ERR_AI_CANCELED = UV_EAI_CANCELED ,/*3003*/
        ERR_AI_FAIL = UV_EAI_FAIL ,/*3004*/
        ERR_AI_FAMILY = UV_EAI_FAMILY ,/*3005*/
        ERR_AI_MEMORY = UV_EAI_MEMORY ,/*3006*/
        ERR_AI_NODATA = UV_EAI_NODATA ,/*3007*/
        ERR_AI_NONAME = UV_EAI_NONAME ,/*3008*/
        ERR_AI_OVERFLOW = UV_EAI_OVERFLOW ,/*-3009*/
        ERR_AI_SERVICE = UV_EAI_SERVICE ,/*-3010*/
        ERR_AI_BADHINTS = UV_EAI_BADHINTS ,/*-3013*/
        ERR_AI_PROTOCOL = UV_EAI_PROTOCOL , /*-3014*/
        ERR_AI_SOCKTYPE = UV_EAI_SOCKTYPE ,/*-3011*/
        ERR_ALREADY = UV_EALREADY ,/*-114 */
        ERR_BUSY = UV_EBUSY ,/*-16 */
        ERR_CANCELED = UV_ECANCELED ,/*-125 */
        ERR_CHARSET = UV_ECHARSET ,/* -4080 */
        ERR_CONNABORTED = UV_ECONNABORTED ,/*-103 */
        ERR_CONNREFUSED = UV_ECONNREFUSED ,/*-111 */
        ERR_CONNRESET = UV_ECONNRESET ,/*-104 */
        ERR_DESTADDRREQ = UV_EDESTADDRREQ ,/*-89 */
        ERR_EXIST = UV_EEXIST ,/*-17 */
        ERR_FAULT = UV_EFAULT ,/* -14 */
        ERR_FBIG = UV_EFBIG ,/*-27 */
        ERR_HOSTUNREACH = UV_EHOSTUNREACH ,/*-113 */
        ERR_INTR = UV_EINTR ,/*-4 */
        ERR_INVAL = UV_EINVAL ,/*-22 */
        ERR_IO = UV_EIO ,/*-5 */
        ERR_ISCONN = UV_EISCONN ,/*-106 */
        ERR_ISDIR = UV_EISDIR ,/*-21 */
        ERR_LOOP = UV_ELOOP ,/*-40 */
        ERR_MFILE = UV_EMFILE ,/*-24 */
        ERR_MSGSIZE = UV_EMSGSIZE ,/*-90 */
        ERR_NAMETOOLONG = UV_ENAMETOOLONG ,/*-36 */
        ERR_NETDOWN = UV_ENETDOWN ,/*-100 */
        ERR_NETUNREACH = UV_ENETUNREACH ,/*-101 */
        ERR_NFILE = UV_ENFILE ,/*-23 */
        ERR_NOBUFS = UV_ENOBUFS ,/*-105 */
        ERR_NODEV = UV_ENODEV ,/*-19 */
        ERR_NOENT = UV_ENOENT ,/* -2 */
        ERR_NOMEM = UV_ENOMEM ,/*-12 */
        ERR_NONET = UV_ENONET ,/*-64 */
        ERR_NOPROTOOPT = UV_ENOPROTOOPT ,/*-92 */
        ERR_NOSPC = UV_ENOSPC ,/*-28 */
        ERR_NOSYS = UV_ENOSYS ,/* -38 */
        ERR_NOTCONN = UV_ENOTCONN ,/*-107 */
        ERR_NOTDIR = UV_ENOTDIR ,/*-20 */
        ERR_NOTEMPTY = UV_ENOTEMPTY ,/*-39 */
        ERR_NOTSOCK = UV_ENOTSOCK ,/* -88 */
        ERR_NOTSUP = UV_ENOTSUP ,/*-95 */
        ERR_OVERFLOW = UV_EOVERFLOW ,/* -75 */
        ERR_PERM = UV_EPERM ,/*-1 */
        ERR_PIPE = UV_EPIPE ,/*-32 */
        ERR_PROTO = UV_EPROTO ,/* -71 */
        ERR_PROTONOSUPPORT = UV_EPROTONOSUPPORT ,/* -93 */
        ERR_PROTOTYPE = UV_EPROTOTYPE ,/* -91 */
        ERR_RANGE = UV_ERANGE ,/*-34 */
        ERR_ROFS = UV_EROFS ,/* -30 */
        ERR_SHUTDOWN = UV_ESHUTDOWN ,/* -108 */
        ERR_SPIPE = UV_ESPIPE ,/*-29 */
        ERR_TIMEDOUT = UV_ETIMEDOUT ,/*-110 */
        ERR_TXTBSY = UV_ETXTBSY ,/*-26 */
        ERR_XDEV = UV_EXDEV ,/*-18 */
        ERR_UNKNOWN = UV_UNKNOWN ,/*-4094 */
        ERR_OF = UV_EOF ,/* -4095 */
        ERR_NXIO = UV_ENXIO ,/* -6 */
        ERR_MLINK = UV_EMLINK ,/*-31 */
        ERR_HOSTDOWN = UV_EHOSTDOWN ,/* -112 */
        ERR_REMOTEIO = UV_EREMOTEIO ,/*-121 */
        ERR_NOTTY = UV_ENOTTY ,/*-25 */
        ERR_FTYPE = UV_EFTYPE ,/*-4028 */
        ERR_ILSEQ = UV_EILSEQ ,/*-84 */
        ERR_SOCKTNOSUPPORT = UV_ESOCKTNOSUPPORT ,/*-94 */
        ERR_NODATA = UV_ENODATA ,/*-61 */
        ERR_UNATCH = UV_EUNATCH,/*-49 */
        ERR_RRNO_MAX = UV_ERRNO_MAX/* -4096 */
    };

    struct buffer_deleter {
        void operator()(ev::buff_t *data);
    };

    typedef uv_dir_t dir_t;
    typedef uv_file file;
    typedef uv_loop_t *loop_ref;
    typedef uv_uid_t uid_t;
    typedef uv_gid_t gid_t;
    typedef uv_dirent_t dirent_t;
    typedef uv_statfs_t statfs_t;
    typedef uv_stat_t stat_t;

    void callback_watcher_tcp_connection_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    void callback_watcher_udp_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
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
    void callback_watcher_connect_tcp (uv_connect_t *s, int status);
    void callback_watcher_write (uv_write_t *s, int status);
    void callback_watcher_fs (uv_fs_t *req);
    void callback_watcher_random (uv_random_t *s, int status, void *buf, size_t buflen);
    void callback_watcher_getaddrinfo (uv_getaddrinfo_t *req, int status, struct addrinfo *res);
    void callback_watcher_getnameinfo (uv_getnameinfo_t *req, int status, const char *hostname, const char *service);
    void callback_watcher_work (uv_work_t *req);
    void callback_watcher_after_work (uv_work_t *req, int status);
    void callback_close_cb (uv_handle_t *s);

    class async {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(async, uv_async_t)
    public:
        MANAPI_EV_DEFAULT(async, uv_async_t)

        async ();

        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, uv_async_cb cb) MANAPI_EV_NOEXPECT;

        int send () MANAPI_EV_NOEXPECT;

        int set (uv_async_cb cb = callback_watcher_async) MANAPI_EV_NOEXPECT;
    private:

        uv_async_t s_;
    };

    class idle {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(idle, uv_idle_t)
    public:
        MANAPI_EV_DEFAULT(idle, uv_idle_t)
        idle ();
        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;

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
        check ();

        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;
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

        io ();

        int bind (loop_ref loop, manapi::socket_t fd) {
            return (uv_poll_init_socket(loop, &this->s_, fd));
        }

        template<typename T1 = manapi::fd_t>
        requires(std::is_same_v<int, manapi::socket_t>)
        int bind (loop_ref loop, int fd) {
            return uv_poll_init(loop, &this->s_, fd);
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

        write ();

        int bind (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb) MANAPI_EV_NOEXPECT;
        int bind (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs) MANAPI_EV_NOEXPECT;
    private:
        uv_write_t s_{};
    };

    class connect {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(write, uv_write_t)
    public:
        MANAPI_EV_DEFAULT(connect, uv_connect_t)

        connect ();

        int bind (uv_tcp_t *p, const struct sockaddr *addr, uv_connect_cb cb) MANAPI_EV_NOEXPECT;
    private:
        uv_connect_t s_{};
    };

    class tcp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(tcp, uv_tcp_t)
    public:
        MANAPI_EV_DEFAULT(tcp, uv_tcp_t)
        MANAPI_EV_STREAM(tcp, uv_tcp_t)

        tcp ();

        int listen (int tcp_backlog) MANAPI_EV_NOEXPECT;

        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;

        int connect (uv_connect_t *connect, const struct sockaddr *addr, uv_connect_cb cb) MANAPI_EV_NOEXPECT;

        int accept (tcp *parent) MANAPI_EV_NOEXPECT;

        int read_start () MANAPI_EV_NOEXPECT;

        int read_start (uv_alloc_cb alloc, uv_read_cb cb) MANAPI_EV_NOEXPECT;

        int read_stop () MANAPI_EV_NOEXPECT;

        ssize_t try_write (const void *buff, ssize_t len) MANAPI_EV_NOEXPECT;

        ssize_t try_write (const ev::buff_t *buff, uint32_t nbuff) MANAPI_EV_NOEXPECT;

        int s_bind (const sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;

        int getpeername (sockaddr *name, int *namelen) MANAPI_EV_NOEXPECT;

        int getsockname (sockaddr *name, int *namelen) MANAPI_EV_NOEXPECT;

        int close_reset (uv_close_cb close_cb) MANAPI_EV_NOEXPECT;

        int close_reset () MANAPI_EV_NOEXPECT;

        int keepalive (int enable, unsigned int delay) MANAPI_EV_NOEXPECT;

        int nodelay (int enable) MANAPI_EV_NOEXPECT;

        int simultaneous_accepts (int enable) MANAPI_EV_NOEXPECT;
    private:
        uv_tcp_t s_;
    };

    class udp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp, uv_udp_t)
    public:
        MANAPI_EV_DEFAULT(udp, uv_udp_t)
        MANAPI_EV_STREAM(udp, uv_udp_t)

        udp ();

        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;
        int s_bind (const sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;

        int recv_start () MANAPI_EV_NOEXPECT;
        int recv_start (uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPI_EV_NOEXPECT;

        int recv_stop () MANAPI_EV_NOEXPECT;

        int connect (const struct sockaddr *addr) MANAPI_EV_NOEXPECT;

        int try_send (const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPI_EV_NOEXPECT;
    private:
        uv_udp_t s_;
    };

    class udp_send {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp_send, uv_udp_send_t)
    public:
        MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)

        udp_send ();

        int bind (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb, const sockaddr *addr) MANAPI_EV_NOEXPECT;
        int bind (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr) MANAPI_EV_NOEXPECT;
    private:
        uv_udp_send_t s_;
    };

    class prepare {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(prepare, uv_prepare_t)
    public:
        MANAPI_EV_DEFAULT(prepare, uv_prepare_t)

        prepare ();

        int bind(loop_ref loop) MANAPI_EV_NOEXPECT;

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

        timer ();

        int bind(loop_ref loop) MANAPI_EV_NOEXPECT;

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

        int cancel () MANAPI_EV_NOEXPECT;

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

    class random {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(random, uv_random_t)
    public:
        MANAPI_EV_DEFAULT(random, uv_random_t)
        random ();
        int cancel () MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, char *buff, std::size_t size, uv_random_cb cb) MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, char *buff, std::size_t size) MANAPI_EV_NOEXPECT;
    private:
        uv_random_t s_;
    };

    class getaddrinfo {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(getaddrinfo, uv_getaddrinfo_t)
    public:
        MANAPI_EV_DEFAULT(getaddrinfo, uv_getaddrinfo_t)
        getaddrinfo ();
        int cancel () MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, const char *node, const char *service, const struct addrinfo *hints, uv_getaddrinfo_cb getaddrinfo_cb) MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, const char *node, const char *service, const struct addrinfo *hints) MANAPI_EV_NOEXPECT;
        static void free (::addrinfo *n) MANAPI_EV_NOEXPECT;
    private:
        uv_getaddrinfo_t s_;
    };

    class getnameinfo {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(getnameinfo, uv_getnameinfo_t)
    public:
        MANAPI_EV_DEFAULT(getnameinfo, uv_getnameinfo_t)
        getnameinfo ();
        int cancel () MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, const sockaddr *addr, int flags, uv_getnameinfo_cb cb) MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, const sockaddr *addr, int flags) MANAPI_EV_NOEXPECT;
    private:
        uv_getnameinfo_t s_;
    };

    class work {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(work, uv_work_t)
    public:
        MANAPI_EV_DEFAULT(work, uv_work_t)
        work();
        int cancel () MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop, uv_work_cb cb, uv_after_work_cb after_cb) MANAPI_EV_NOEXPECT;
        int bind (loop_ref loop) MANAPI_EV_NOEXPECT;
    private:
        uv_work_t s_;
    };

    using shared_async = std::shared_ptr<async>;
    using shared_tcp = std::shared_ptr<tcp>;
    using shared_udp = std::shared_ptr<udp>;
    using shared_check = std::shared_ptr<check>;
    using shared_prepare = std::shared_ptr<prepare>;
    using shared_idle = std::shared_ptr<idle>;
    using shared_random = std::shared_ptr<random>;
    using shared_fs = std::shared_ptr<fs>;
    using shared_timer = std::shared_ptr<timer>;
    using shared_io = std::shared_ptr<io>;
    using shared_write = std::shared_ptr<write>;
    using shared_udp_send = std::shared_ptr<udp_send>;
    using shared_getaddrinfo = std::shared_ptr<getaddrinfo>;
    using shared_getnameinfo = std::shared_ptr<getnameinfo>;
    using shared_work = std::shared_ptr<work>;

    const char *strerror (int errnum) MANAPIHTTP_NOEXPECT;

    const char *namerror (int errnum) MANAPIHTTP_NOEXPECT;
}

#undef MANAPI_EV_CAST_HANDLE
#undef MANAPI_EV_DEFAULT_PRIVATE_VAR
#undef MANAPI_EV_STREAM
#undef MANAPI_EV_CHECK
#undef MANAPI_EV_DEFAULT
#undef MANAPI_EV_CAST_STREAM