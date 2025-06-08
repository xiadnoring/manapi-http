#include "components/ManapiEventStructures.hpp"

#define MANAPI_EV_NOEXPECT noexcept(true)
#define MANAPI_EV_CAST_STREAM(x) reinterpret_cast<uv_stream_t *> (x)
#define MANAPI_EV_CAST_HANDLE(x) reinterpret_cast <uv_handle_t *> (x)
#define MANAPI_EV_DEFAULT(name_class, name_struct) \
void manapi::ev::name_class::unbind (uv_close_cb cb) MANAPI_EV_NOEXPECT {  uv_close(MANAPI_EV_CAST_HANDLE(&this->s_), cb); }\
void manapi::ev::name_class::unbind () MANAPI_EV_NOEXPECT { uv_close(MANAPI_EV_CAST_HANDLE (&this->s_), callback_close_cb); }\
void manapi::ev::name_class::data (void *data) MANAPI_EV_NOEXPECT { uv_handle_set_data(MANAPI_EV_CAST_HANDLE (&this->s_), data);}\
void *manapi::ev::name_class::data () MANAPI_EV_NOEXPECT {return uv_handle_get_data(MANAPI_EV_CAST_HANDLE (&this->s_)); } \
manapi::ev::loop_ref manapi::ev::name_class::loop () MANAPI_EV_NOEXPECT { return uv_handle_get_loop(MANAPI_EV_CAST_HANDLE(&this->s_)); } \
name_struct* manapi::ev::name_class::custom () MANAPI_EV_NOEXPECT { return &this->s_; } \
bool manapi::ev::name_class::is_active() MANAPI_EV_NOEXPECT { return uv_is_active(MANAPI_EV_CAST_HANDLE(&this->s_)); } \
manapi::ev::name_class::~name_class () = default;
#define MANAPI_EV_STREAM(name_class, name_struct) \
int manapi::ev::name_class::listen (int backlog, uv_connection_cb cb) MANAPI_EV_NOEXPECT {  return uv_listen(MANAPI_EV_CAST_STREAM(&this->s_), backlog, cb); } \
int manapi::ev::name_class::ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPI_EV_NOEXPECT { return uv_ip4_addr(ip, port, addr); } \
int manapi::ev::name_class::ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPI_EV_NOEXPECT {  return uv_ip6_addr(ip, port, addr); }
#define MANAPI_EV_CHECK(expr) { auto rhs = expr; if (rhs) { std::cout << rhs << "\n"; THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_INTERNAL, #expr); } }

MANAPI_EV_DEFAULT(async, uv_async_t)
MANAPI_EV_DEFAULT(timer, uv_timer_t)
MANAPI_EV_DEFAULT(io, uv_poll_t)
MANAPI_EV_DEFAULT(check, uv_check_t)
MANAPI_EV_DEFAULT(prepare, uv_prepare_t)
MANAPI_EV_DEFAULT(idle, uv_idle_t)
MANAPI_EV_DEFAULT(connect, uv_connect_t)

manapi::ev::connect::connect() : s_() {
}

int manapi::ev::connect::bind(uv_tcp_t *p, const sockaddr *addr, uv_connect_cb cb) MANAPI_EV_NOEXPECT {
    return uv_tcp_connect(&this->s_, p, addr, cb);
}

MANAPI_EV_DEFAULT(tcp, uv_tcp_t)

MANAPI_EV_DEFAULT(udp, uv_udp_t)
MANAPI_EV_DEFAULT(write, uv_write_t)
MANAPI_EV_DEFAULT(random, uv_random_t)
MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)
MANAPI_EV_DEFAULT(fs, uv_fs_t)

MANAPI_EV_STREAM(udp, uv_udp_t)
MANAPI_EV_STREAM(tcp, uv_tcp_t)

void manapi::ev::buffer_deleter::operator()(manapi::ev::buff_t *data) {
    delete[] data;
}

manapi::ev::async::async() : s_() {}

int manapi::ev::async::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return this->bind(loop, callback_watcher_async);
}

int manapi::ev::async::bind(loop_ref loop, uv_async_cb cb) MANAPI_EV_NOEXPECT {
    return uv_async_init(loop, &this->s_, cb);
}

int manapi::ev::async::send() MANAPI_EV_NOEXPECT {
    return uv_async_send(&this->s_);
}

int manapi::ev::async::set(uv_async_cb cb) MANAPI_EV_NOEXPECT {
    auto data = this->data();
    auto loop = this->loop();

    this->unbind();

    if (auto rhs = uv_async_init(loop, &this->s_, cb)) {
        return rhs;
    }

    this->data(data);
    return 0;
}

int manapi::ev::idle::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return uv_idle_init(loop, &this->s_);
}

int manapi::ev::idle::start() MANAPI_EV_NOEXPECT {
    return this->start(callback_watcher_idle);
}

int manapi::ev::idle::start(uv_idle_cb cb) MANAPI_EV_NOEXPECT {
    return uv_idle_start(&this->s_, cb);
}

int manapi::ev::idle::stop() MANAPI_EV_NOEXPECT {
    return uv_idle_stop(&this->s_);
}

manapi::ev::idle::idle() : s_() {

}

manapi::ev::check::check() : s_() {
}

int manapi::ev::check::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return uv_check_init(loop, &this->s_);
}

int manapi::ev::check::start() MANAPI_EV_NOEXPECT {
    return this->start(callback_watcher_check);
}

int manapi::ev::check::start(uv_check_cb cb) MANAPI_EV_NOEXPECT {
    return uv_check_start(&this->s_, cb);
}

int manapi::ev::check::stop() MANAPI_EV_NOEXPECT {
    return uv_check_stop(&this->s_);
}

manapi::ev::io::io () : s_ () {
    
}

int manapi::ev::io::start(int revents, uv_poll_cb cb) MANAPI_EV_NOEXPECT {
    return uv_poll_start(&this->s_, revents, cb);
}

int manapi::ev::io::start(int revents) MANAPI_EV_NOEXPECT {
    return this->start(revents, ev::callback_watcher_io);
}

int manapi::ev::io::start() MANAPI_EV_NOEXPECT {
    return this->start(this->events());
}

int manapi::ev::io::restart(int revents) MANAPI_EV_NOEXPECT {
    if (this->stop()) return -1;
    return this->start(revents);
}

int manapi::ev::io::stop() MANAPI_EV_NOEXPECT {
    return uv_poll_stop(&this->s_);
}

int manapi::ev::io::events() MANAPI_EV_NOEXPECT {
#ifdef _WIN32
    return 0;
#else
    return this->custom()->io_watcher.pevents & (ev::WRITE|ev::READ);
#endif
}

manapi::ev::write::write() : s_() {

}

int manapi::ev::write::bind(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb) MANAPI_EV_NOEXPECT {
    return uv_write(&this->s_, stream, buf, nbufs, cb);
}

int manapi::ev::write::bind(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs) MANAPI_EV_NOEXPECT {
    return this->bind(stream, buf, nbufs, callback_watcher_write);
}

manapi::ev::tcp::tcp() : s_() {

}

int manapi::ev::tcp::listen(int backlog) MANAPI_EV_NOEXPECT {
    return this->listen(backlog, reinterpret_cast<uv_connection_cb>(ev::callback_watcher_tcp_accept));
}

int manapi::ev::tcp::bind(loop_ref loop) noexcept(true) {
    return uv_tcp_init(loop, &this->s_);
}

int manapi::ev::tcp::connect(uv_connect_t *connect, const sockaddr *addr, uv_connect_cb cb) MANAPI_EV_NOEXPECT {
    return uv_tcp_connect(connect, &this->s_, addr, cb);
}

int manapi::ev::tcp::accept(tcp *parent) MANAPI_EV_NOEXPECT {
    return uv_accept(MANAPI_EV_CAST_STREAM(&parent->s_), MANAPI_EV_CAST_STREAM(&this->s_));
}

int manapi::ev::tcp::read_start() MANAPI_EV_NOEXPECT {
    return this->read_start(ev::callback_watcher_tcp_connection_alloc, ev::callback_watcher_tcp_read);
}

int manapi::ev::tcp::read_start(uv_alloc_cb alloc, uv_read_cb cb) MANAPI_EV_NOEXPECT {
    return uv_read_start(MANAPI_EV_CAST_STREAM(&this->s_), alloc, cb);
}

int manapi::ev::tcp::read_stop() MANAPI_EV_NOEXPECT {
    return uv_read_stop(MANAPI_EV_CAST_STREAM(&this->s_));
}

ssize_t manapi::ev::tcp::try_write(const void *buff, ssize_t len) MANAPI_EV_NOEXPECT {
    ev::buff_t buffs;
    buffs.base = (char*)buff;
    buffs.len = static_cast<std::size_t>(len);
    return uv_try_write(MANAPI_EV_CAST_STREAM(&this->s_), &buffs, 1);
}

int manapi::ev::tcp::s_bind(const sockaddr *addr, int flags) MANAPI_EV_NOEXPECT {
    return uv_tcp_bind(&this->s_, addr, flags);
}

int manapi::ev::tcp::getpeername(sockaddr *name, int *namelen) MANAPI_EV_NOEXPECT {
    return uv_tcp_getpeername(&this->s_, name, namelen);
}

int manapi::ev::tcp::getsockname(sockaddr *name, int *namelen) MANAPI_EV_NOEXPECT {
    return uv_tcp_getsockname(&this->s_, name, namelen);
}

int manapi::ev::tcp::close_reset(uv_close_cb close_cb) MANAPI_EV_NOEXPECT {
    return uv_tcp_close_reset(&this->s_, close_cb);
}

int manapi::ev::tcp::close_reset() MANAPI_EV_NOEXPECT {
    return this->close_reset(callback_close_cb);
}

int manapi::ev::tcp::keepalive(int enable, unsigned int delay) MANAPI_EV_NOEXPECT {
    return uv_tcp_keepalive(&this->s_, enable, delay);
}

int manapi::ev::tcp::nodelay(int enable) MANAPI_EV_NOEXPECT {
    return uv_tcp_nodelay(&this->s_, enable);
}

int manapi::ev::tcp::simultaneous_accepts(int enable) MANAPI_EV_NOEXPECT {
    return uv_tcp_simultaneous_accepts(&this->s_, enable);
}

manapi::ev::udp::udp() : s_() {

}

int manapi::ev::udp::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return uv_udp_init(loop, &this->s_);
}

int manapi::ev::udp::s_bind(const sockaddr *addr, int flags) MANAPI_EV_NOEXPECT{
    return uv_udp_bind(&this->s_, addr, flags);
}

int manapi::ev::udp::recv_start() MANAPI_EV_NOEXPECT {
    return this->recv_start(ev::callback_watcher_udp_alloc, ev::callback_watcher_udp_recv);
}

int manapi::ev::udp::recv_start(uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPI_EV_NOEXPECT {
    return uv_udp_recv_start(&this->s_, alloc, cb);
}

int manapi::ev::udp::recv_stop() MANAPI_EV_NOEXPECT{
    return uv_udp_recv_stop(&this->s_);
}

int manapi::ev::udp::connect(const sockaddr *addr) MANAPI_EV_NOEXPECT {
    return uv_udp_connect(&this->s_, addr);
}

int manapi::ev::udp::try_send(const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPI_EV_NOEXPECT {
    return uv_udp_try_send(&this->s_, buf, nbuf, addr);
}

manapi::ev::udp_send::udp_send() : s_() {

}

int manapi::ev::udp_send::bind(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb,
    const sockaddr *addr) MANAPI_EV_NOEXPECT {
    return uv_udp_send(&this->s_, stream, buf, nbufs, addr, cb);
}

int manapi::ev::udp_send::bind(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr) MANAPI_EV_NOEXPECT {
    return this->bind(stream, buf, nbufs, callback_watcher_udp_send, addr);
}

manapi::ev::prepare::prepare() : s_() {

}

int manapi::ev::prepare::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return uv_prepare_init(loop, &this->s_);
}

int manapi::ev::prepare::start() MANAPI_EV_NOEXPECT {
    return this->start(ev::callback_watcher_prepare);
}

int manapi::ev::prepare::start(uv_prepare_cb cb) MANAPI_EV_NOEXPECT {
    return uv_prepare_start(&this->s_, cb);
}

int manapi::ev::prepare::stop() MANAPI_EV_NOEXPECT {
    return uv_prepare_stop(&this->s_);
}

manapi::ev::timer::timer() : s_() {

}

int manapi::ev::timer::bind(loop_ref loop) MANAPI_EV_NOEXPECT {
    return uv_timer_init(loop, &this->s_);
}

int manapi::ev::timer::start(uint64_t timeout, uint64_t repeat) MANAPI_EV_NOEXPECT {
    return this->start(timeout, repeat, ev::callback_watcher_timer);
}

int manapi::ev::timer::start(uint64_t timeout, uint64_t repeat, uv_timer_cb cb) MANAPI_EV_NOEXPECT {
    return uv_timer_start(&this->s_, cb, timeout, repeat);
}

int manapi::ev::timer::stop() MANAPI_EV_NOEXPECT {
    return uv_timer_stop(&this->s_);
}

int manapi::ev::timer::again() MANAPI_EV_NOEXPECT {
    return uv_timer_again(&this->s_);
}

void manapi::ev::timer::repeat(uint64_t repeat) MANAPI_EV_NOEXPECT {
    uv_timer_set_repeat(&this->s_, repeat);
}

uint64_t manapi::ev::timer::repeat() const MANAPI_EV_NOEXPECT {
    return uv_timer_get_repeat(&this->s_);
}

uint64_t manapi::ev::timer::due_in() const MANAPI_EV_NOEXPECT {
    return uv_timer_get_due_in(&this->s_);
}

manapi::ev::fs::fs(loop_ref loop) : s_(), loop_(loop) {}

int manapi::ev::fs::cancel() noexcept(true) {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::fs::open(const char *path, int flags, int mode, uv_fs_cb open_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_open(this->loop_, &this->s_, path, flags, mode, open_cb);
}

int manapi::ev::fs::open(const char *path, int flags, int mode) MANAPI_EV_NOEXPECT {
    return this->open(path, flags, mode, callback_watcher_fs);
}

int manapi::ev::fs::read(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb read_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_read(this->loop_, &this->s_, fileno, buff, nbuff, offset, read_cb);
}

int manapi::ev::fs::read(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT {
    return this->read(fileno, buff, nbuff, offset, callback_watcher_fs);
}

int manapi::ev::fs::write(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb write_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_write(this->loop_, &this->s_, fileno, buff, nbuff, offset, write_cb);
}

int manapi::ev::fs::write(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT {
    return this->write(fileno, buff, nbuff, offset, callback_watcher_fs);
}

ssize_t manapi::ev::fs::try_write(ev::file fileno, const void *buff, ssize_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT {
    ssize_t r;

#if defined(_WIN32)
    return 0;
    HANDLE handle;
    OVERLAPPED overlapped, *overlapped_ptr;
    LARGE_INTEGER offset_;
    DWORD bytes;
    DWORD error;
    int result;
    unsigned int index;
    LARGE_INTEGER original_position;
    LARGE_INTEGER zero_offset;
    int restore_position;

    handle = (HANDLE) ::_get_osfhandle(fileno);
    if (handle == INVALID_HANDLE_VALUE) {
        return FS_EBADF;
    }

    if (offset != -1) {
        memset(&overlapped, 0, sizeof overlapped);
        overlapped_ptr = &overlapped;
        if (SetFilePointerEx(handle, zero_offset, &original_position, FILE_CURRENT)) {
            restore_position = 1;
        }
    }
    else {
        overlapped_ptr = NULL;
    }

    index = 0;
    bytes = 0;

    do {
        DWORD incremental_bytes;

        if (offset != -1) {
            offset_.QuadPart = offset + bytes;
            overlapped.Offset = offset_.LowPart;
            overlapped.OffsetHigh = offset_.HighPart;
        }

        result = WriteFile(handle,
                           buff,
                           nbuff,
                           &incremental_bytes,
                           overlapped_ptr);
        bytes += incremental_bytes;
        ++index;
    }
    while (0);

    if (restore_position) {
        SetFilePointerEx(handle, original_position, NULL, FILE_BEGIN);
    }

    if (result || bytes > 0) {
        r = bytes;
    }
    else {
        error = GetLastError();
        /* error */
        if (error == ERROR_ACCESS_DENIED) {
            error = ERROR_INVALID_FLAGS;
        }
        r = uv_translate_sys_error(error);
    }
#else
    if (offset < 0) {
        r = ::write(fileno, buff, nbuff);
    }
    else {
        r = ::pwrite(fileno, buff, nbuff, offset);
    }
#endif
    return r;
}

ssize_t manapi::ev::fs::try_read(ev::file fileno, void *buff, ssize_t nbuff, int64_t offset) MANAPI_EV_NOEXPECT {
    ssize_t r;
#if defined(_WIN32)
    HANDLE handle;
    OVERLAPPED overlapped, *overlapped_ptr;
    LARGE_INTEGER offset_;
    DWORD bytes;
    DWORD error;
    int result;
    unsigned int index;
    LARGE_INTEGER original_position;
    LARGE_INTEGER zero_offset;
    int restore_position;

    zero_offset.QuadPart = 0;
    restore_position = 0;
    handle = (HANDLE) ::_get_osfhandle(fileno);

    if (handle == INVALID_HANDLE_VALUE) {
        return FS_EBADF;
    }

    if (offset != -1) {
        memset(&overlapped, 0, sizeof overlapped);
        overlapped_ptr = &overlapped;
        if (SetFilePointerEx(handle, zero_offset, &original_position, FILE_CURRENT)) {
            restore_position = 1;
        }
    }
    else {
        overlapped_ptr = NULL;
    }

    index = 0;
    bytes = 0;
    do {
        DWORD incremental_bytes;

        if (offset != -1) {
            offset_.QuadPart = offset + bytes;
            overlapped.Offset = offset_.LowPart;
            overlapped.OffsetHigh = offset_.HighPart;
        }

        result = ReadFile(handle,
                          buff,
                          nbuff,
                          &incremental_bytes,
                          overlapped_ptr);
        bytes += incremental_bytes;
        ++index;
    }
    while (0);

    if (restore_position) {
        SetFilePointerEx(handle, original_position, NULL, FILE_BEGIN);
    }

    if (result || bytes > 0) {
        r = bytes;
    }
    else {
        error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            error = ERROR_INVALID_FLAGS;
        }

        if (error == ERROR_HANDLE_EOF || error == ERROR_BROKEN_PIPE) {
            r = bytes;
        }
        else {
            r = uv_translate_sys_error(error);
        }
    }
#else
    if (offset < 0) {
        r = ::read(fileno, buff, nbuff);
    }
    else {
        r = ::pread(fileno, buff, nbuff, offset);
    }
#endif
    return r;
}

int manapi::ev::fs::close(ev::file fileno, uv_fs_cb close_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_close(this->loop_, &this->s_, fileno, close_cb);
}

int manapi::ev::fs::close(ev::file fileno) MANAPI_EV_NOEXPECT {
    return this->close(fileno, callback_watcher_fs);
}

int manapi::ev::fs::unlink(const char *path, uv_fs_cb unlink_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_unlink(this->loop_, &this->s_, path, unlink_cb);
}

int manapi::ev::fs::unlink(const char *path) MANAPI_EV_NOEXPECT {
    return this->unlink(path, callback_watcher_fs);
}

int manapi::ev::fs::mkdir(const char *path, int mode, uv_fs_cb mkdir_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_mkdir(this->loop_, &this->s_, path, mode, mkdir_cb);
}

int manapi::ev::fs::mkdir(const char *path, int mode) MANAPI_EV_NOEXPECT {
    return this->mkdir(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::mkdtemp(const char *path, uv_fs_cb mkdtemp_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_mkdtemp(this->loop_, &this->s_, path, mkdtemp_cb);
}

int manapi::ev::fs::mkdtemp(const char *path) MANAPI_EV_NOEXPECT {
    return this->mkdtemp(path, callback_watcher_fs);
}

int manapi::ev::fs::mkstemp(const char *path, uv_fs_cb mkstemp_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_mkstemp(this->loop_, &this->s_, path, mkstemp_cb);
}

int manapi::ev::fs::mkstemp(const char *path) MANAPI_EV_NOEXPECT {
    return this->mkstemp(path, callback_watcher_fs);
}

int manapi::ev::fs::rmdir(const char *path, uv_fs_cb rmdir_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_rmdir(this->loop_, &this->s_, path, rmdir_cb);
}

int manapi::ev::fs::rmdir(const char *path) MANAPI_EV_NOEXPECT {
    return this->rmdir(path, callback_watcher_fs);
}

int manapi::ev::fs::opendir(const char *path, uv_fs_cb opendir) MANAPI_EV_NOEXPECT {
    return uv_fs_opendir(this->loop_, &this->s_, path, opendir);
}

int manapi::ev::fs::opendir(const char *path) MANAPI_EV_NOEXPECT {
    return this->opendir(path, callback_watcher_fs);
}

int manapi::ev::fs::closedir(ev::dir_t * dir, uv_fs_cb closedir_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_closedir(this->loop_, &this->s_, dir, closedir_cb);
}

int manapi::ev::fs::closedir(ev::dir_t * dir) MANAPI_EV_NOEXPECT {
    return this->closedir(dir, callback_watcher_fs);
}

int manapi::ev::fs::readdir(ev::dir_t * dir, uv_fs_cb readdir_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_readdir(this->loop_, &this->s_, dir, readdir_cb);
}

int manapi::ev::fs::readdir(ev::dir_t * dir) MANAPI_EV_NOEXPECT {
    return this->readdir(dir, callback_watcher_fs);
}

int manapi::ev::fs::scandir(const char *path, int flags, uv_fs_cb scandir_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_scandir(this->loop_, &this->s_, path, flags, scandir_cb);
}

int manapi::ev::fs::scandir(const char *path, int flags) MANAPI_EV_NOEXPECT {
    return this->scandir(path, flags, callback_watcher_fs);
}

int manapi::ev::fs::scandir_next(ev::dirent_t *dir) MANAPI_EV_NOEXPECT {
    return uv_fs_scandir_next(&this->s_, dir);
}

int manapi::ev::fs::stat(const char *path, uv_fs_cb stat_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_stat(this->loop_, &this->s_, path, stat_cb);
}

int manapi::ev::fs::stat(const char *path) MANAPI_EV_NOEXPECT {
    return this->stat(path, callback_watcher_fs);
}

int manapi::ev::fs::fstat(ev::file file, uv_fs_cb fstat_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_fstat(this->loop_, &this->s_, file, fstat_cb);
}

int manapi::ev::fs::fstat(ev::file file) MANAPI_EV_NOEXPECT {
    return this->fstat(file, callback_watcher_fs);
}

int manapi::ev::fs::lstat(const char *path, uv_fs_cb lstat_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_lstat(this->loop_, &this->s_, path, lstat_cb);
}

int manapi::ev::fs::lstat(const char *path) MANAPI_EV_NOEXPECT {
    return this->lstat(path, callback_watcher_fs);
}

int manapi::ev::fs::statfs(const char *path, uv_fs_cb statfs_cb) MANAPI_EV_NOEXPECT {
    return uv_fs_statfs(this->loop_, &this->s_, path, statfs_cb);
}

int manapi::ev::fs::statfs(const char *path) MANAPI_EV_NOEXPECT {
    return this->statfs(path, callback_watcher_fs);
}

int manapi::ev::fs::rename(const char *path, const char *new_path, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_rename(this->loop_, &this->s_, path, new_path, cb);
}

int manapi::ev::fs::rename(const char *path, const char *new_path) MANAPI_EV_NOEXPECT {
    return this->rename(path, new_path, callback_watcher_fs);
}

int manapi::ev::fs::fsync(ev::file file, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_fsync(this->loop_, &this->s_, file, cb);
}

int manapi::ev::fs::fsync(ev::file file) MANAPI_EV_NOEXPECT {
    return this->fsync(file, callback_watcher_fs);
}

int manapi::ev::fs::fdatasync(ev::file file, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_fdatasync(this->loop_, &this->s_, file, cb);
}

int manapi::ev::fs::fdatasync(ev::file file) MANAPI_EV_NOEXPECT {
    return this->fdatasync(file, callback_watcher_fs);
}

int manapi::ev::fs::ftruncate(ev::file file, int64_t off, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_ftruncate(this->loop_, &this->s_, file, off, cb);
}

int manapi::ev::fs::ftruncate(ev::file file, int64_t off) MANAPI_EV_NOEXPECT {
    return this->ftruncate(file, off, callback_watcher_fs);
}

int manapi::ev::fs::copyfile(const char *path1, const char *path2, int flags, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_copyfile(this->loop_, &this->s_, path1, path2, flags, cb);
}

int manapi::ev::fs::copyfile(const char *path1, const char *path2, int flags) MANAPI_EV_NOEXPECT {
    return this->copyfile(path1, path2, flags, callback_watcher_fs);
}

int manapi::ev::fs::sendfile(ev::file outfd, ev::file infd, int64_t off, size_t length, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_sendfile(this->loop_, &this->s_, outfd, infd, off, length, cb);
}

int manapi::ev::fs::sendfile(ev::file outfd, ev::file infd, int64_t off, size_t length) MANAPI_EV_NOEXPECT {
    return this->sendfile(outfd, infd, off, length, callback_watcher_fs);
}

int manapi::ev::fs::access(const char *path, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_access(this->loop_, &this->s_, path, mode, cb);
}

int manapi::ev::fs::access(const char *path, int mode) MANAPI_EV_NOEXPECT {
    return this->access(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::chmod(const char *path, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_chmod(this->loop_, &this->s_, path, mode, cb);
}

int manapi::ev::fs::chmod(const char *path, int mode) MANAPI_EV_NOEXPECT {
    return this->chmod(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::fchmod(ev::file file, int mode, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_fchmod(this->loop_, &this->s_, file, mode, cb);
}

int manapi::ev::fs::fchmod(ev::file file, int mode) MANAPI_EV_NOEXPECT {
    return this->fchmod(file, mode, callback_watcher_fs);
}

int manapi::ev::fs::utime(const char *path, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_utime(this->loop_, &this->s_, path, atime, mtime, cb);
}

int manapi::ev::fs::utime(const char *path, double atime, double mtime) MANAPI_EV_NOEXPECT {
    return this->utime(path, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::futime(ev::file file, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_futime(this->loop_, &this->s_, file, atime, mtime, cb);
}

int manapi::ev::fs::futime(ev::file file, double atime, double mtime) MANAPI_EV_NOEXPECT {
    return this->futime(file, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::lutime(const char *path, double atime, double mtime, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_lutime(this->loop_, &this->s_, path, atime, mtime, cb);
}

int manapi::ev::fs::lutime(const char *path, double atime, double mtime) MANAPI_EV_NOEXPECT {
    return this->lutime(path, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::link(const char *path, const char *new_path, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_link(this->loop_, &this->s_, path, new_path, cb);
}

int manapi::ev::fs::link(const char *path, const char *new_path) MANAPI_EV_NOEXPECT {
    return this->link(path, new_path, callback_watcher_fs);
}

int manapi::ev::fs::symlink(const char *path, const char *new_path, int flags, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_symlink(this->loop_, &this->s_, path, new_path, flags, cb);
}

int manapi::ev::fs::symlink(const char *path, const char *new_path, int flags) MANAPI_EV_NOEXPECT {
    return this->symlink(path, new_path, flags, callback_watcher_fs);
}

int manapi::ev::fs::readlink(const char *path, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_readlink(this->loop_, &this->s_, path, cb);
}

int manapi::ev::fs::readlink(const char *path) MANAPI_EV_NOEXPECT {
    return this->readlink(path, callback_watcher_fs);
}

int manapi::ev::fs::realpath(const char *path, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_realpath(this->loop_, &this->s_, path, cb);
}

int manapi::ev::fs::realpath(const char *path) MANAPI_EV_NOEXPECT {
    return this->realpath(path, callback_watcher_fs);
}

int manapi::ev::fs::chown(const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_chown(this->loop_, &this->s_, path, uid, gid, cb);
}

int manapi::ev::fs::chown(const char *path, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT {
    return this->chown(path, uid, gid, callback_watcher_fs);
}

int manapi::ev::fs::fchown(ev::file file, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_fchown(this->loop_, &this->s_, file, uid, gid, cb);
}

int manapi::ev::fs::fchown(ev::file file, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT {
    return this->fchown(file, uid, gid, callback_watcher_fs);
}

int manapi::ev::fs::lchown(const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPI_EV_NOEXPECT {
    return uv_fs_lchown(this->loop_, &this->s_, path, uid, gid, cb);
}

int manapi::ev::fs::lchown(const char *path, uid_t uid, gid_t gid) MANAPI_EV_NOEXPECT {
    return this->lchown(path, uid, gid, callback_watcher_fs);
}

ssize_t manapi::ev::fs::result() const MANAPI_EV_NOEXPECT {
    return this->s_.result;
}

manapi::ev::random::random() : s_() {}

int manapi::ev::random::cancel() noexcept(true) {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::random::bind(loop_ref loop, char *buff, std::size_t size, uv_random_cb cb) MANAPI_EV_NOEXPECT {
    return uv_random(loop, &this->s_, buff, size, /* flags */ 0, cb);
}

int manapi::ev::random::bind (loop_ref loop, char *buff, std::size_t size) MANAPI_EV_NOEXPECT {
    return this->bind(loop, buff, size, callback_watcher_random);
}
