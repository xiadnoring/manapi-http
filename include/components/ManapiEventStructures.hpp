#pragma once

#include <type_traits>
#include <memory>

#include <uv.h>

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"
#include "../ManapiDebug.hpp"

#define MANAPHTTP_UV_SINCE_AT(major,minor,patch) UV_VERSION_MAJOR > major || (UV_VERSION_MAJOR==major&&(UV_VERSION_MINOR > minor || (UV_VERSION_MINOR==minor && UV_VERSION_PATCH>=patch)))
#define MANAPI_EV_CAST_STREAM(x) reinterpret_cast<uv_stream_t *> (x)
#define MANAPI_EV_CAST_HANDLE(x) reinterpret_cast <uv_handle_t *> (x)
#define MANAPI_EV_DEFAULT_PRIVATE_VAR(name_class, name_struct)
#define MANAPI_EV_DEFAULT(name_class, name_struct) \
        void unbind (uv_close_cb cb) MANAPIHTTP_NOEXCEPT;\
        void unbind () MANAPIHTTP_NOEXCEPT;\
        void data (void *data) MANAPIHTTP_NOEXCEPT;\
        void *data () MANAPIHTTP_NOEXCEPT; \
        loop_ref loop () MANAPIHTTP_NOEXCEPT; \
        name_struct* custom () MANAPIHTTP_NOEXCEPT; \
        bool is_active () MANAPIHTTP_NOEXCEPT; \
        ~name_class ();
#define MANAPI_EV_STREAM(name_class, name_struct) \
        int listen (int tcp_backlog, uv_connection_cb cb) MANAPIHTTP_NOEXCEPT; \
        static int ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPIHTTP_NOEXCEPT; \
        static int ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPIHTTP_NOEXCEPT;
#define MANAPI_EV_CHECK(expr) if (expr) { THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_INTERNAL, #expr); }

namespace manapi {
    /**
     * Cross platform representation of a fd object
     */
    typedef uv_os_fd_t fd_t;
    /**
     * Cross platform representation of a socket object
     */
    typedef uv_os_sock_t socket_t;
}

namespace manapi::ev {
    /**
     * Cross platform representation of a buffer object
     */
    typedef uv_buf_t buff_t;
    /**
     *Cross platform representation of a stream object
     */
    typedef uv_stream_t stream_t;

    enum pf_ip_types {
        /* equals PF_INET */
        IPv4 = PF_INET,
        /* equals PF_INET6 */
        IPv6 = PF_INET6
    };

    enum udp_flags {
        UDP_IPV6ONLY = UV_UDP_IPV6ONLY,
        UDP_REUSEADDR = UV_UDP_REUSEADDR,
#if MANAPHTTP_UV_SINCE_AT(1, 49, 0)
        UDP_REUSEPORT = UV_UDP_REUSEPORT,
#else
        UDP_REUSEPORT = 0,
#endif
        UDP_MMSG_CHUNK = UV_UDP_MMSG_CHUNK,
        UDP_MMSG_FREE = UV_UDP_MMSG_FREE,
        UDP_PARTIAL = UV_UDP_PARTIAL,
        UDP_LINUX_RECVERR = UV_UDP_LINUX_RECVERR,
        UDP_RECVMMSG = UV_UDP_RECVMMSG
    };

    enum tcp_flags {
        TCP_IPV6ONLY = UV_TCP_IPV6ONLY,
#if MANAPHTTP_UV_SINCE_AT(1,49,0)
        TCP_REUSEPORT = UV_TCP_REUSEPORT,
#else
        TCP_REUSEPORT = 0,
#endif
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
        /* read by owner */
        IRUSR = S_IREAD,
        /* write by owner */
        IWUSR = S_IWRITE,
        /* execute by owner */
        IXUSR = S_IEXEC,
        /* read, write, execute by owner */
        IRWXU = S_IREAD|S_IWRITE|S_IEXEC,

        /* read by owner */
        IRGRP = S_IREAD,
        /* write by owner */
        IWGRP = S_IWRITE,
        /* execute by owner  */
        IXGRP = S_IEXEC,
        /* read, write, execute by owner */
        IRWXG = S_IREAD|S_IWRITE|S_IEXEC,

        /* read by owner */
        IROTH = S_IREAD,
        /* write by owner */
        IWOTH = S_IWRITE,
        /* execute by owner */
        IXOTH = S_IEXEC,
        /* read, write, execute by owner */
        IRWXO = S_IREAD|S_IWRITE|S_IEXEC,

        /* regular file */
        IFREG = S_IFREG,
        /* socket */
        IFSOCK = 0,
        /* symbolic link */
        IFLNK = 0,
        /* block device */
        IFBLK = 0,
        /* directory */
        IFDIR = S_IFDIR,
        /* character device */
        IFCHR = S_IFCHR,
        /* FIFO */
        IFIFO = 0,
        /* bit mask for the file type bit field */
        IFMT = S_IFMT
    };
#else
    enum fs_o_stat {
        /* read by owner */
        IRUSR = S_IRUSR,
        /* write by owner */
        IWUSR = S_IWUSR,
        /* execute by owner */
        IXUSR = S_IXUSR,
        /* read, write, execute by owner */
        IRWXU = S_IRWXU,

        /* read by group */
        IRGRP = S_IRGRP,
        /* write by group */
        IWGRP = S_IWGRP,
        /* execute by group  */
        IXGRP = S_IXGRP,
        /* read, write, execute by group */
        IRWXG = S_IRWXG,

        /* read by others */
        IROTH = S_IROTH,
        /* write by others */
        IWOTH = S_IWOTH,
        /* execute by others */
        IXOTH = S_IXOTH,
        /* read, write, execute by others */
        IRWXO = S_IRWXO,

        /* regular file */
        IFREG = S_IFREG,
        /* socket */
        IFSOCK = S_IFSOCK,
        /* symbolic link */
        IFLNK = S_IFLNK,
        /* block device */
        IFBLK = S_IFBLK,
        /* directory */
        IFDIR = S_IFDIR,
        /* character device */
        IFCHR = S_IFCHR,
        /* FIFO */
        IFIFO = S_IFIFO,
        /* bit mask for the file type bit field */
        IFMT = S_IFMT
    };
#endif

    enum sys_errors {

        /* operation not permitted (-1) */
        ERR_PERM = UV_EPERM ,
        /* no such file or directory (-2) */
        ERR_NOENT = UV_ENOENT ,
        /* no such process (-3) */
        ERR_SRCH = UV_ESRCH ,
        /* interrupted system call (-4) */
        ERR_INTR = UV_EINTR ,
        /* i/o error (-5) */
        ERR_IO = UV_EIO ,
        /* no such device or address (-6) */
        ERR_NXIO = UV_ENXIO ,
        /* argument list too long (-7) */
        ERR_2BIG = UV_E2BIG,
        /* bad file descriptor (-9) */
        ERR_BADF = UV_EBADF ,
        /* resource temporarily unavailable (-11) */
        ERR_AGAIN = UV_EAGAIN ,
        /* not enough memory (-12) */
        ERR_NOMEM = UV_ENOMEM ,
        /* permission denied (-13) */
        ERR_ACCES = UV_EACCES,
        /* bad address in system call argument (-14) */
        ERR_FAULT = UV_EFAULT ,
        /* resource busy or locked (-16) */
        ERR_BUSY = UV_EBUSY ,
        /* file already exists (-17) */
        ERR_EXIST = UV_EEXIST ,
        /* cross-device link not permitted (-18) */
        ERR_XDEV = UV_EXDEV ,
        /* no such device (-19) */
        ERR_NODEV = UV_ENODEV ,
        /* not a directory (-20) */
        ERR_NOTDIR = UV_ENOTDIR ,
        /* illegal operation on a directory (-21) */
        ERR_ISDIR = UV_EISDIR ,
        /* invalid argument (-22) */
        ERR_INVAL = UV_EINVAL ,
        /* file table overflow (-23) */
        ERR_NFILE = UV_ENFILE ,
        /* too many open files (-24) */
        ERR_MFILE = UV_EMFILE ,
        /* inappropriate ioctl for device (-25) */
        ERR_NOTTY = UV_ENOTTY ,
        /* text file is busy (-26) */
        ERR_TXTBSY = UV_ETXTBSY ,
        /* file too large (-27) */
        ERR_FBIG = UV_EFBIG ,
        /* no space left on device (-28) */
        ERR_NOSPC = UV_ENOSPC ,
        /* invalid seek (-29) */
        ERR_SPIPE = UV_ESPIPE ,
        /* read-only file system (-30) */
        ERR_ROFS = UV_EROFS ,
        /* too many links (-31) */
        ERR_MLINK = UV_EMLINK ,
        /* broken pipe (-32) */
        ERR_PIPE = UV_EPIPE ,
        /* result too large (-34 ) */
        ERR_RANGE = UV_ERANGE ,
        /* name too long (-36) */
        ERR_NAMETOOLONG = UV_ENAMETOOLONG ,
        /* function not implemented (-38) */
        ERR_NOSYS = UV_ENOSYS ,
        /* directory not empty (-39) */
        ERR_NOTEMPTY = UV_ENOTEMPTY ,
        /* too many symbolic links encountered (-40) */
        ERR_LOOP = UV_ELOOP ,
#if MANAPHTTP_UV_SINCE_AT(1,45,0)
        /* protocol driver not attached (-49) */
        ERR_UNATCH = UV_EUNATCH,
        /* (-61) */
        ERR_NODATA = UV_ENODATA ,
#else
        ERR_UNATCH = UV_ERRNO_MAX,
        ERR_NODATA = UV_ERRNO_MAX ,
#endif
        /* machine is not on the network (-64) */
        ERR_NONET = UV_ENONET ,
        /* protocol error (-71) */
        ERR_PROTO = UV_EPROTO ,
        /* value too large for defined data type (-75) */
        ERR_OVERFLOW = UV_EOVERFLOW ,
        /* illegal byte sequence (-84) */
        ERR_ILSEQ = UV_EILSEQ ,
        /* socket operation on non-socket (-88) */
        ERR_NOTSOCK = UV_ENOTSOCK ,
        /* destination address required (-89) */
        ERR_DESTADDRREQ = UV_EDESTADDRREQ ,
        /* message too long (-90) */
        ERR_MSGSIZE = UV_EMSGSIZE ,
        /* protocol wrong type for socket (-91) */
        ERR_PROTOTYPE = UV_EPROTOTYPE ,
        /* protocol not available (-92) */
        ERR_NOPROTOOPT = UV_ENOPROTOOPT ,
        /* protocol not supported (-93) */
        ERR_PROTONOSUPPORT = UV_EPROTONOSUPPORT ,
        /* socket type not supported (-94) */
        ERR_SOCKTNOSUPPORT = UV_ESOCKTNOSUPPORT ,
        /* operation not supported on socket (-95) */
        ERR_NOTSUP = UV_ENOTSUP ,
        /* address family not supported (-97) */
        ERR_AFNOSUPPORT = UV_EAFNOSUPPORT,
        /* address already in use (-98)*/
        ERR_ADDRINUSE = UV_EADDRINUSE,
        /* address not available (-99) */
        ERR_ADDRNOTAVAIL = UV_EADDRNOTAVAIL,
        /* network is down (-100) */
        ERR_NETDOWN = UV_ENETDOWN ,
        /* network is unreachable (-101) */
        ERR_NETUNREACH = UV_ENETUNREACH ,
        /* software caused connection abort (-103) */
        ERR_CONNABORTED = UV_ECONNABORTED ,
        /* connection reset by peer (-104) */
        ERR_CONNRESET = UV_ECONNRESET ,
        /* no buffer space available (-105) */
        ERR_NOBUFS = UV_ENOBUFS ,
        /* socket is already connected (-106) */
        ERR_ISCONN = UV_EISCONN ,
        /* socket is not connected (-107) */
        ERR_NOTCONN = UV_ENOTCONN ,
        /* cannot send after transport endpoint shutdown (-108) */
        ERR_SHUTDOWN = UV_ESHUTDOWN ,
        /* connection timed out (-110) */
        ERR_TIMEDOUT = UV_ETIMEDOUT ,
        /* connection refused (-111) */
        ERR_CONNREFUSED = UV_ECONNREFUSED ,
        /* (-112) */
        ERR_HOSTDOWN = UV_EHOSTDOWN ,
        /* host is unreachable (-113) */
        ERR_HOSTUNREACH = UV_EHOSTUNREACH ,
        /* connection already in progress (-114) */
        ERR_ALREADY = UV_EALREADY ,
        /* (-121) */
        ERR_REMOTEIO = UV_EREMOTEIO ,
        /* operation canceled (-125) */
        ERR_CANCELED = UV_ECANCELED ,
        /* address family not supported (-3000) */
        ERR_AI_ADDRFAMILY = UV_EAI_ADDRFAMILY ,
        /* temporary failure (-3001) */
        ERR_AI_AGAIN = UV_EAI_AGAIN ,
        /* bad ai_flags value (-3002) */
        ERR_AI_BADFLAGS = UV_EAI_BADFLAGS ,
        /* request canceled (-3003) */
        ERR_AI_CANCELED = UV_EAI_CANCELED ,
        /* permanent failure (-3004) */
        ERR_AI_FAIL = UV_EAI_FAIL ,
        /* ai_family not supported (-3005) */
        ERR_AI_FAMILY = UV_EAI_FAMILY ,
        /* out of memory (-3006) */
        ERR_AI_MEMORY = UV_EAI_MEMORY ,
        /* no address (-3007) */
        ERR_AI_NODATA = UV_EAI_NODATA ,
        /* unknown node or service (-3008) */
        ERR_AI_NONAME = UV_EAI_NONAME ,
        /* argument buffer overflow (-3009) */
        ERR_AI_OVERFLOW = UV_EAI_OVERFLOW ,
        /* service not available for socket type (-3010) */
        ERR_AI_SERVICE = UV_EAI_SERVICE ,
        /* socket type not supported (-3011) */
        ERR_AI_SOCKTYPE = UV_EAI_SOCKTYPE ,
        /* invalid value for hints (-3013) */
        ERR_AI_BADHINTS = UV_EAI_BADHINTS ,
        /* resolved protocol is unknown (-3014) */
        ERR_AI_PROTOCOL = UV_EAI_PROTOCOL ,
        /* inappropriate file type or format (-4028) */
        ERR_FTYPE = UV_EFTYPE ,
        /* invalid Unicode character (-4080) */
        ERR_CHARSET = UV_ECHARSET ,
        /* unknown error (-4094) */
        ERR_UNKNOWN = UV_UNKNOWN ,
        /* end of file (-4095) */
        ERR_OF = UV_EOF ,
        /* (-4096) */
        ERR_RRNO_MAX = UV_ERRNO_MAX
    };

    struct buffer_deleter {
        void operator()(ev::buff_t *data);
    };

    struct chars_deleter {
        void operator()(char *data);
    };

    /**
     * Data type used for streaming directory iteration.
     * Used by fs::opendir(), fs::readdir(), and fs::closedir().
     * dirents represents a user provided array of uv_dirent_t`s used to hold results.
     * `nentries is the user provided maximum array size of dirents.
     */
    typedef uv_dir_t dir_t;

    /* Cross platform representation of a file handle. */
    typedef uv_file file;

    /* Loop data reference */
    typedef uv_loop_t *loop_ref;

    typedef uv_handle_t handle;

    typedef uv_uid_t uid_t;

    typedef uv_gid_t gid_t;

    /* Cross platform (reduced) equivalent of struct dirent. Used in fs::scandir_next(). */
    typedef uv_dirent_t dirent_t;

    /* Reduced cross platform equivalent of struct statfs. Used in fs::statfs(). */
    typedef uv_statfs_t statfs_t;

    /* Stores the result of fs::stat() and other stat requests. */
    typedef uv_stat_t stat_t;

    class async {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(async, uv_async_t)
    public:
        MANAPI_EV_DEFAULT(async, uv_async_t)

        /**
         * initialize the async watcher
         */
        async ();

        /**
         * bind the watcher
         * @param loop the loop reference
         * @return the status code
         */
        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;

        /**
         * bind the watcher
         * @param loop the loop reference
         * @param cb the callback
         * @return the status code
         */
        int bind (loop_ref loop, uv_async_cb cb) MANAPIHTTP_NOEXCEPT;

        /**
         * send a request
         * @return the status code
         */
        int send () MANAPIHTTP_NOEXCEPT;

        /**
         * set the event loop callback
         * @return the status code
         */
        int set () MANAPIHTTP_NOEXCEPT;

        /**
         * set the custom callback
         * @param cb the custom callback
         * @return the status code
         */
        int set (uv_async_cb cb) MANAPIHTTP_NOEXCEPT;
    private:

        uv_async_t s_;
    };

    class idle {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(idle, uv_idle_t)
    public:
        MANAPI_EV_DEFAULT(idle, uv_idle_t)

        /**
         * initialize the idle watcher
         */
        idle ();

        /**
         * bind the watcher
         * @param loop the loop reference
         * @return the status code
         */
        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;

        /**
         * start listening to the watcher with the event loop callback
         * @return the status code
         */
        int start () MANAPIHTTP_NOEXCEPT;

        /**
         * start listening to the watcher with the custom callback
         * @param cb the custom callback
         * @return the status code
         */
        int start (uv_idle_cb cb) MANAPIHTTP_NOEXCEPT;

        /**
         * stop listening to the watcher
         * @return the status code
         */
        int stop () MANAPIHTTP_NOEXCEPT;
    private:
        uv_idle_t s_;
    };

    class check {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(check, uv_check_t)
    public:
        MANAPI_EV_DEFAULT(check, uv_check_t)

        /**
         * initalize the watcher
         */
        check ();

        /**
         * bind the watcher
         * @param loop the loop reference
         * @return the status code
         */
        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;

        /**
         * start listening to the watcher with the event loop callback
         * @return the status code
         */
        int start () MANAPIHTTP_NOEXCEPT;

        /**
         * start listening to the watcher with the custom callback
         * @param cb the custom callback
         * @return the status code
         */
        int start (uv_check_cb cb) MANAPIHTTP_NOEXCEPT;

        /**
         * stop listening to the watcher
         * @return the status code
         */
        int stop () MANAPIHTTP_NOEXCEPT;
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

        int start (int revents, uv_poll_cb cb) MANAPIHTTP_NOEXCEPT;

        int start (int revents) MANAPIHTTP_NOEXCEPT;

        int start () MANAPIHTTP_NOEXCEPT;

        int restart (int revents) MANAPIHTTP_NOEXCEPT;

        int stop () MANAPIHTTP_NOEXCEPT;

        int events () MANAPIHTTP_NOEXCEPT;
    private:
        uv_poll_t s_;
    };

    class write {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(write, uv_write_t)
    public:
        MANAPI_EV_DEFAULT(write, uv_write_t)

        write ();

        int bind (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb) MANAPIHTTP_NOEXCEPT;

        int bind (uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs) MANAPIHTTP_NOEXCEPT;
    private:
        uv_write_t s_{};
    };

    class connect {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(write, uv_write_t)
    public:
        MANAPI_EV_DEFAULT(connect, uv_connect_t)

        connect ();

        int bind (uv_tcp_t *p, const struct sockaddr *addr, uv_connect_cb cb) MANAPIHTTP_NOEXCEPT;
    private:
        uv_connect_t s_{};
    };

    class tcp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(tcp, uv_tcp_t)
    public:
        MANAPI_EV_DEFAULT(tcp, uv_tcp_t)
        MANAPI_EV_STREAM(tcp, uv_tcp_t)

        tcp ();

        int listen (int tcp_backlog) MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;

        int connect (uv_connect_t *connect, const struct sockaddr *addr, uv_connect_cb cb) MANAPIHTTP_NOEXCEPT;

        int accept (tcp *parent) MANAPIHTTP_NOEXCEPT;

        int read_start () MANAPIHTTP_NOEXCEPT;

        int read_start (uv_alloc_cb alloc, uv_read_cb cb) MANAPIHTTP_NOEXCEPT;

        int read_stop () MANAPIHTTP_NOEXCEPT;

        ssize_t try_write (const void *buff, ssize_t len) MANAPIHTTP_NOEXCEPT;

        ssize_t try_write (const ev::buff_t *buff, uint32_t nbuff) MANAPIHTTP_NOEXCEPT;

        int s_bind (const sockaddr *addr, int flags) MANAPIHTTP_NOEXCEPT;

        int getpeername (sockaddr *name, int *namelen) MANAPIHTTP_NOEXCEPT;

        int getsockname (sockaddr *name, int *namelen) MANAPIHTTP_NOEXCEPT;

        int close_reset (uv_close_cb close_cb) MANAPIHTTP_NOEXCEPT;

        int close_reset () MANAPIHTTP_NOEXCEPT;

        int keepalive (int enable, unsigned int delay) MANAPIHTTP_NOEXCEPT;

        int nodelay (int enable) MANAPIHTTP_NOEXCEPT;

        int simultaneous_accepts (int enable) MANAPIHTTP_NOEXCEPT;
    private:
        uv_tcp_t s_;
    };

    class udp {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp, uv_udp_t)
    public:
        MANAPI_EV_DEFAULT(udp, uv_udp_t)
        MANAPI_EV_STREAM(udp, uv_udp_t)

        udp ();

        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;

        int s_bind (const sockaddr *addr, int flags) MANAPIHTTP_NOEXCEPT;

        int recv_start () MANAPIHTTP_NOEXCEPT;
        int recv_start (uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPIHTTP_NOEXCEPT;

        int recv_stop () MANAPIHTTP_NOEXCEPT;

        int connect (const struct sockaddr *addr) MANAPIHTTP_NOEXCEPT;

        int try_send (const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPIHTTP_NOEXCEPT;
    private:
        uv_udp_t s_;
    };

    class udp_send {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(udp_send, uv_udp_send_t)
    public:
        MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)

        udp_send ();

        int bind (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb, const sockaddr *addr) MANAPIHTTP_NOEXCEPT;
        int bind (uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr) MANAPIHTTP_NOEXCEPT;
    private:
        uv_udp_send_t s_;
    };

    class prepare {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(prepare, uv_prepare_t)
    public:
        MANAPI_EV_DEFAULT(prepare, uv_prepare_t)

        prepare ();

        int bind(loop_ref loop) MANAPIHTTP_NOEXCEPT;

        int start () MANAPIHTTP_NOEXCEPT;
        int start (uv_prepare_cb cb) MANAPIHTTP_NOEXCEPT;

        int stop () MANAPIHTTP_NOEXCEPT;
    private:
        uv_prepare_t s_;
    };

    class timer {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(timer, uv_timer_t)
    public:
        MANAPI_EV_DEFAULT(timer, uv_timer_t)

        timer ();

        int bind(loop_ref loop) MANAPIHTTP_NOEXCEPT;

        int start (uint64_t timeout, uint64_t repeat) MANAPIHTTP_NOEXCEPT;

        int start (uint64_t timeout, uint64_t repeat, uv_timer_cb cb) MANAPIHTTP_NOEXCEPT;

        int stop () MANAPIHTTP_NOEXCEPT;

        int again () MANAPIHTTP_NOEXCEPT;

        void repeat (uint64_t repeat)  MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD uint64_t repeat () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD uint64_t due_in () const MANAPIHTTP_NOEXCEPT;
    private:
        uv_timer_t s_;
    };

    class fs {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(fs, uv_fs_t)
    public:
        MANAPI_EV_DEFAULT(fs, uv_fs_t)

        fs (loop_ref loop);

        int cancel () MANAPIHTTP_NOEXCEPT;

        int open (const char *path, int flags, int mode, uv_fs_cb open_cb) MANAPIHTTP_NOEXCEPT;

        int open (const char *path, int flags, int mode) MANAPIHTTP_NOEXCEPT;

        int read (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb read_cb) MANAPIHTTP_NOEXCEPT;

        int read (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT;

        int write (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb write_cb) MANAPIHTTP_NOEXCEPT;

        int write (ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT;

        static ssize_t try_write (ev::file fileno, const void *buff, ssize_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT;

        static ssize_t try_read (ev::file fileno, void *buff, ssize_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT;

        int close (ev::file fileno, uv_fs_cb close_cb) MANAPIHTTP_NOEXCEPT;

        int close (ev::file fileno) MANAPIHTTP_NOEXCEPT;

        int unlink (const char *path, uv_fs_cb unlink_cb) MANAPIHTTP_NOEXCEPT;

        int unlink (const char *path) MANAPIHTTP_NOEXCEPT;

        int mkdir (const char *path, int mode, uv_fs_cb mkdir_cb) MANAPIHTTP_NOEXCEPT;

        int mkdir (const char *path, int mode) MANAPIHTTP_NOEXCEPT;

        int mkdtemp (const char *path, uv_fs_cb mkdtemp_cb) MANAPIHTTP_NOEXCEPT;

        int mkdtemp (const char *path) MANAPIHTTP_NOEXCEPT;

        int mkstemp (const char *path, uv_fs_cb mkstemp_cb) MANAPIHTTP_NOEXCEPT;

        int mkstemp (const char *path) MANAPIHTTP_NOEXCEPT;

        int rmdir (const char *path, uv_fs_cb rmdir_cb) MANAPIHTTP_NOEXCEPT;

        int rmdir (const char *path) MANAPIHTTP_NOEXCEPT;

        int opendir (const char *path, uv_fs_cb opendir) MANAPIHTTP_NOEXCEPT;

        int opendir (const char *path) MANAPIHTTP_NOEXCEPT;

        int closedir (ev::dir_t * dir, uv_fs_cb closedir_cb) MANAPIHTTP_NOEXCEPT;

        int closedir (ev::dir_t * dir) MANAPIHTTP_NOEXCEPT;

        int readdir (ev::dir_t * dir, uv_fs_cb readdir_cb) MANAPIHTTP_NOEXCEPT;

        int readdir (ev::dir_t * dir) MANAPIHTTP_NOEXCEPT;

        int scandir (const char *path, int flags, uv_fs_cb scandir_cb) MANAPIHTTP_NOEXCEPT;

        int scandir (const char *path, int flags) MANAPIHTTP_NOEXCEPT;

        int scandir_next (ev::dirent_t *dir) MANAPIHTTP_NOEXCEPT;

        int stat (const char *path, uv_fs_cb stat_cb) MANAPIHTTP_NOEXCEPT;

        int stat (const char *path) MANAPIHTTP_NOEXCEPT;

        int fstat (ev::file file, uv_fs_cb fstat_cb) MANAPIHTTP_NOEXCEPT;

        int fstat (ev::file file) MANAPIHTTP_NOEXCEPT;

        int lstat (const char *path, uv_fs_cb lstat_cb) MANAPIHTTP_NOEXCEPT;

        int lstat (const char *path) MANAPIHTTP_NOEXCEPT;

        int statfs (const char *path, uv_fs_cb statfs_cb) MANAPIHTTP_NOEXCEPT;

        int statfs (const char *path) MANAPIHTTP_NOEXCEPT;

        int rename (const char *path, const char *new_path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int rename (const char *path, const char *new_path) MANAPIHTTP_NOEXCEPT;

        int fsync (ev::file file, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int fsync (ev::file file) MANAPIHTTP_NOEXCEPT;

        int fdatasync (ev::file file, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int fdatasync (ev::file file) MANAPIHTTP_NOEXCEPT;

        int ftruncate (ev::file file, int64_t off, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int ftruncate (ev::file file, int64_t off) MANAPIHTTP_NOEXCEPT;

        int copyfile (const char *path1, const char *path2, int flags, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int copyfile (const char *path1, const char *path2, int flags) MANAPIHTTP_NOEXCEPT;

        int sendfile (ev::file outfd, ev::file infd, int64_t off, size_t length, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int sendfile (ev::file outfd, ev::file infd, int64_t off, size_t length) MANAPIHTTP_NOEXCEPT;

        int access (const char *path, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int access (const char *path, int mode) MANAPIHTTP_NOEXCEPT;

        int chmod (const char *path, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int chmod (const char *path, int mode) MANAPIHTTP_NOEXCEPT;

        int fchmod (ev::file file, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int fchmod (ev::file file, int mode) MANAPIHTTP_NOEXCEPT;

        int utime (const char *path, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int utime (const char *path, double atime, double mtime) MANAPIHTTP_NOEXCEPT;

        int futime (ev::file file, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int futime (ev::file file, double atime, double mtime) MANAPIHTTP_NOEXCEPT;

        int lutime (const char *path, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int lutime (const char *path, double atime, double mtime) MANAPIHTTP_NOEXCEPT;

        int link (const char *path, const char *new_path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int link (const char *path, const char *new_path) MANAPIHTTP_NOEXCEPT;

        int symlink (const char *path, const char *new_path, int flags, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int symlink (const char *path, const char *new_path, int flags) MANAPIHTTP_NOEXCEPT;

        int readlink (const char *path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int readlink (const char *path) MANAPIHTTP_NOEXCEPT;

        int realpath (const char *path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int realpath (const char *path) MANAPIHTTP_NOEXCEPT;

        int chown (const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int chown (const char *path, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT;

        int fchown (ev::file file, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int fchown (ev::file file, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT;

        int lchown (const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT;

        int lchown (const char *path, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT;
        
        MANAPIHTTP_NODISCARD ssize_t result () const MANAPIHTTP_NOEXCEPT;
    private:
        loop_ref loop_;
        uv_fs_t s_;
    };

    class random {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(random, uv_random_t)
    public:
        MANAPI_EV_DEFAULT(random, uv_random_t)

        random ();

        int cancel () MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, char *buff, std::size_t size, uv_random_cb cb) MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, char *buff, std::size_t size) MANAPIHTTP_NOEXCEPT;
    private:
        uv_random_t s_;
    };

    class getaddrinfo {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(getaddrinfo, uv_getaddrinfo_t)
    public:
        MANAPI_EV_DEFAULT(getaddrinfo, uv_getaddrinfo_t)

        getaddrinfo ();

        int cancel () MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, const char *node, const char *service, const struct addrinfo *hints, uv_getaddrinfo_cb getaddrinfo_cb) MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, const char *node, const char *service, const struct addrinfo *hints) MANAPIHTTP_NOEXCEPT;

        static void free (::addrinfo *n) MANAPIHTTP_NOEXCEPT;
    private:
        uv_getaddrinfo_t s_;
    };

    class getnameinfo {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(getnameinfo, uv_getnameinfo_t)
    public:
        MANAPI_EV_DEFAULT(getnameinfo, uv_getnameinfo_t)

        getnameinfo ();

        int cancel () MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, const sockaddr *addr, int flags, uv_getnameinfo_cb cb) MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, const sockaddr *addr, int flags) MANAPIHTTP_NOEXCEPT;
    private:
        uv_getnameinfo_t s_;
    };

    class work {
        MANAPI_EV_DEFAULT_PRIVATE_VAR(work, uv_work_t)
    public:
        MANAPI_EV_DEFAULT(work, uv_work_t)

        work();

        int cancel () MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop, uv_work_cb cb, uv_after_work_cb after_cb) MANAPIHTTP_NOEXCEPT;

        int bind (loop_ref loop) MANAPIHTTP_NOEXCEPT;
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

    const char *strerror (int errnum) MANAPIHTTP_NOEXCEPT;

    const char *namerror (int errnum) MANAPIHTTP_NOEXCEPT;
}

namespace manapi::sys_error {
    /**
     * error status for the OS event
     */
    class status final : public manapi::error::status {
    public:
        /**
         * initialize error status
         */
        status ();

        ~status () override;

        /**
         * initialize error status
         *
         * @param code the error code
         * @param msg the error msg
         * @param syserr the syserror code
         */
        status (manapi::err_num code, std::string_view msg, int syserr);

        status (status &&n) MANAPIHTTP_NOEXCEPT;

        status &operator=(status &&n) MANAPIHTTP_NOEXCEPT;

        status (error::status &&n) MANAPIHTTP_NOEXCEPT;

        status &operator=(error::status &&n) MANAPIHTTP_NOEXCEPT;

        /**
         * print log to the logger() if it exists,
         * otherwise it prints to the stdout
         */
        void log () const override;

        /**
         * throw a error if it exists, otherwise it does nothing
         */
        void unwrap() const override;

        /**
         * Get the system code error
         * @return the system code error
         */
        [[nodiscard]] int syserr () const;

        /**
         * Get the system name error
         * @return the system name error
         */
        [[nodiscard]] std::string_view sysname () const;

        /**
         * Get the system msg error
         * @return the system msg error
         */
        [[nodiscard]] std::string_view sysmsg () const;
    private:
        /* the system error code */
        int syserr_;
    };

    template<typename T, typename E = manapi::sys_error::status>
    class status_or final : public manapi::error::status_or<T, E> {
    public:
        /**
         * Initialize the status_or() instence
         * @param n the status error
         */
        status_or (sys_error::status n) : error::status_or<T, E>(std::move(n)) {}

        status_or (T &&n) : error::status_or<T, E>(std::forward<decltype(n)>(n)) {}

        status_or (const T &n) : error::status_or<T, E>(n) {}

        status_or(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        status_or&operator=(status_or &&n) MANAPIHTTP_NOEXCEPT = default;

        /**
         * Get the system code error
         * @return the system code error
         */
        [[nodiscard]] int syserr () const {
            return this->err_.syserr();
        }

        /**
         * Get the system name error
         * @return the system name error
         */
        [[nodiscard]] std::string_view sysname () const {
            return this->err_.sysname();
        }

        /**
         * Get the system msg error
         * @return the system msg error
         */
        [[nodiscard]] std::string_view sysmsg () const {
            return this->err_.sysmsg();
        }
    };

    /**
     * Generate an InvalidArgument error
     * @param msg the error msg
     * @param syserr the system error code
     * @return the generated error
     */
    sys_error::status status_invalid_argument (std::string_view msg, int syserr);

    /**
     * Generate an ResourceExhausted error
     * @return the generated error
     */
    sys_error::status status_resource_exhausted ();

    /**
     * Generate an ResourceExhausted error
     * @return the generated error
     */
    sys_error::status status_cancelled ();

    /**
     * Generate an ResourceExhausted error
     * @param msg the error msg
     * @return the generated error
     */
    sys_error::status status_cancelled (std::string_view msg);

    /**
     * Generate an InternalError error
     * @param msg the error msg
     * @param syserr the system error code
     * @return the generated error
     */
    sys_error::status status_internal (std::string_view msg, int syserr);

    /**
     * Generate an NotFound error
     * @param msg the error msg
     * @return the generated error
     */
    sys_error::status status_not_found (std::string_view msg);

    /**
     * Generate an Ok error
     * @return the generated Ok error
     */
    sys_error::status status_ok ();
}

#undef MANAPI_EV_CAST_HANDLE
#undef MANAPI_EV_DEFAULT_PRIVATE_VAR
#undef MANAPI_EV_STREAM
#undef MANAPI_EV_CHECK
#undef MANAPI_EV_DEFAULT
#undef MANAPI_EV_CAST_STREAM