#include "ManapiEventStructures.hpp"
#include "ManapiAsync.hpp"
#include "ManapiEventLoop.hpp"
#include "std/ManapiContext.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"
#include "./include/ManapiDebug.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <io.h>
#endif

#define MANAPIHTTP_EV_CAST_STREAM(x) (reinterpret_cast<uv_stream_t *> (x))
#define MANAPIHTTP_EV_CAST_HANDLE(x) (reinterpret_cast <uv_handle_t *> (x))
#define MANAPIHTTP_EV_DEFAULT(name_class, name_struct) \
name_struct *manapi::ev::name_class::custom () MANAPIHTTP_NOEXCEPT { return &this->s_; } \
void manapi::ev::name_class::unbind (uv_close_cb cb) MANAPIHTTP_NOEXCEPT {  uv_close(MANAPIHTTP_EV_CAST_HANDLE(&this->s_), cb); }\
void manapi::ev::name_class::unbind () MANAPIHTTP_NOEXCEPT { uv_close(MANAPIHTTP_EV_CAST_HANDLE (&this->s_), callback_close_cb); }\
void manapi::ev::name_class::data (void *data) MANAPIHTTP_NOEXCEPT { uv_handle_set_data(MANAPIHTTP_EV_CAST_HANDLE (&this->s_), data);}\
void *manapi::ev::name_class::data () MANAPIHTTP_NOEXCEPT {return uv_handle_get_data(MANAPIHTTP_EV_CAST_HANDLE (&this->s_)); } \
manapi::ev::loop_ref manapi::ev::name_class::loop () MANAPIHTTP_NOEXCEPT { return uv_handle_get_loop(MANAPIHTTP_EV_CAST_HANDLE(&this->s_)); } \
bool manapi::ev::name_class::is_active() MANAPIHTTP_NOEXCEPT { return uv_is_active(MANAPIHTTP_EV_CAST_HANDLE(&this->s_)); } \
void manapi::ev::name_class::unref () MANAPIHTTP_NOEXCEPT { return uv_unref(MANAPIHTTP_EV_CAST_HANDLE(&this->s_)); } \
void manapi::ev::name_class::ref () MANAPIHTTP_NOEXCEPT { return uv_ref(MANAPIHTTP_EV_CAST_HANDLE(&this->s_)); } \
manapi::ev::name_class::~name_class () = default;
#define MANAPIHTTP_EV_STREAM(name_class, name_struct) \
int manapi::ev::name_class::listen (int tcp_backlog, uv_connection_cb cb) MANAPIHTTP_NOEXCEPT {  return uv_listen(MANAPIHTTP_EV_CAST_STREAM(&this->s_), tcp_backlog, cb); } \
int manapi::ev::name_class::ip4_addr (const char *ip, int port, sockaddr_in *addr) MANAPIHTTP_NOEXCEPT { return uv_ip4_addr(ip, port, addr); } \
int manapi::ev::name_class::ip6_addr (const char *ip, int port, sockaddr_in6 *addr) MANAPIHTTP_NOEXCEPT {  return uv_ip6_addr(ip, port, addr); }
#define MANAPIHTTP_EV_CHECK(expr) { auto rhs = expr; if (rhs) { std::cout << rhs << "\n"; throw manapi::exception(manapi::ERR_INTERNAL, #expr); } }

MANAPIHTTP_EV_DEFAULT(async, uv_async_t)
MANAPIHTTP_EV_DEFAULT(timer, uv_timer_t)
MANAPIHTTP_EV_DEFAULT(io, uv_poll_t)
MANAPIHTTP_EV_DEFAULT(check, uv_check_t)
MANAPIHTTP_EV_DEFAULT(prepare, uv_prepare_t)
MANAPIHTTP_EV_DEFAULT(idle, uv_idle_t)
MANAPIHTTP_EV_DEFAULT(connect, uv_connect_t)
MANAPIHTTP_EV_DEFAULT(work, uv_work_t)
MANAPIHTTP_EV_DEFAULT(tcp, uv_tcp_t)

MANAPIHTTP_EV_DEFAULT(udp, uv_udp_t)
MANAPIHTTP_EV_DEFAULT(write, uv_write_t)
MANAPIHTTP_EV_DEFAULT(random, uv_random_t)
MANAPIHTTP_EV_DEFAULT(getaddrinfo, uv_getaddrinfo_t)
MANAPIHTTP_EV_DEFAULT(getnameinfo, uv_getnameinfo_t)
MANAPIHTTP_EV_DEFAULT(udp_send, uv_udp_send_t)
MANAPIHTTP_EV_DEFAULT(fs, uv_fs_t)

MANAPIHTTP_EV_STREAM(udp, uv_udp_t)
MANAPIHTTP_EV_STREAM(tcp, uv_tcp_t)

manapi::ev::connect::connect() : s_() {
}


int manapi::ev::connect::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel((reinterpret_cast<uv_req_t *>(&this->s_)));
}

int manapi::ev::connect::bind(uv_tcp_t *p, const sockaddr *addr, uv_connect_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_connect(&this->s_, p, addr, cb);
}


void manapi::ev::buffer_deleter::operator()(manapi::ev::buff_t *data) {
    delete[] data;
}

void manapi::ev::chars_deleter::operator()(const char *data) {
    delete[] data;
}

manapi::ev::async::async() : s_() {}

int manapi::ev::async::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return this->bind(loop, callback_watcher_async);
}

int manapi::ev::async::bind(loop_ref loop, uv_async_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_async_init(loop, &this->s_, cb);
}

int manapi::ev::async::send() MANAPIHTTP_NOEXCEPT {
    return uv_async_send(&this->s_);
}

int manapi::ev::async::set() MANAPIHTTP_NOEXCEPT {
    return this->set(callback_watcher_async);
}

int manapi::ev::async::set(uv_async_cb cb) MANAPIHTTP_NOEXCEPT {
    auto data = this->data();
    auto loop = this->loop();

    this->unbind();

    if (auto rhs = uv_async_init(loop, &this->s_, cb)) {
        return rhs;
    }

    this->data(data);
    return 0;
}

int manapi::ev::idle::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_idle_init(loop, &this->s_);
}

int manapi::ev::idle::start() MANAPIHTTP_NOEXCEPT {
    return this->start(callback_watcher_idle);
}

int manapi::ev::idle::start(uv_idle_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_idle_start(&this->s_, cb);
}

int manapi::ev::idle::stop() MANAPIHTTP_NOEXCEPT {
    return uv_idle_stop(&this->s_);
}

manapi::ev::idle::idle() : s_() {

}

manapi::ev::check::check() : s_() {
}

int manapi::ev::check::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_check_init(loop, &this->s_);
}

int manapi::ev::check::start() MANAPIHTTP_NOEXCEPT {
    return this->start(callback_watcher_check);
}

int manapi::ev::check::start(uv_check_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_check_start(&this->s_, cb);
}

int manapi::ev::check::stop() MANAPIHTTP_NOEXCEPT {
    return uv_check_stop(&this->s_);
}

manapi::ev::io::io () : s_ () {
    
}

int manapi::ev::io::start(int revents, uv_poll_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_poll_start(&this->s_, revents, cb);
}

int manapi::ev::io::start(int revents) MANAPIHTTP_NOEXCEPT {
    return this->start(revents, ev::callback_watcher_io);
}

int manapi::ev::io::start() MANAPIHTTP_NOEXCEPT {
    return this->start(this->events());
}

int manapi::ev::io::restart(int revents) MANAPIHTTP_NOEXCEPT {
    if (this->stop()) return -1;
    return this->start(revents);
}

int manapi::ev::io::stop() MANAPIHTTP_NOEXCEPT {
    return uv_poll_stop(&this->s_);
}

int manapi::ev::io::events() MANAPIHTTP_NOEXCEPT {
#ifdef _WIN32
    return 0;
#else
    return static_cast<int>(this->custom()->io_watcher.pevents & (ev::WRITE|ev::READ));
#endif
}

manapi::ev::write::write() : s_() {

}

int manapi::ev::write::bind(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_write(&this->s_, stream, buf, nbufs, cb);
}

int manapi::ev::write::bind(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs) MANAPIHTTP_NOEXCEPT {
    return this->bind(stream, buf, nbufs, callback_watcher_write);
}

manapi::ev::tcp::tcp() : s_() {

}

int manapi::ev::tcp::listen(int tcp_backlog) MANAPIHTTP_NOEXCEPT {
    return this->listen(tcp_backlog, reinterpret_cast<uv_connection_cb>(ev::callback_watcher_tcp_accept));
}

int manapi::ev::tcp::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_init(loop, &this->s_);
}

int manapi::ev::tcp::connect(uv_connect_t *connect, const sockaddr *addr, uv_connect_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_connect(connect, &this->s_, addr, cb);
}

int manapi::ev::tcp::accept(tcp *parent) MANAPIHTTP_NOEXCEPT {
    return uv_accept(MANAPIHTTP_EV_CAST_STREAM(&parent->s_), MANAPIHTTP_EV_CAST_STREAM(&this->s_));
}

int manapi::ev::tcp::read_start() MANAPIHTTP_NOEXCEPT {
    return this->read_start(ev::callback_watcher_tcp_connection_alloc, ev::callback_watcher_tcp_read);
}

int manapi::ev::tcp::read_start(uv_alloc_cb alloc, uv_read_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_read_start(MANAPIHTTP_EV_CAST_STREAM(&this->s_), alloc, cb);
}

int manapi::ev::tcp::read_stop() MANAPIHTTP_NOEXCEPT {
    return uv_read_stop(MANAPIHTTP_EV_CAST_STREAM(&this->s_));
}

ssize_t manapi::ev::tcp::try_write(const void *buff, std::size_t len) MANAPIHTTP_NOEXCEPT {
    ev::buff_t buffs;
    buffs.base = (char*)buff;
    buffs.len = static_cast<decltype(buffs.len)>(len);
    return uv_try_write(MANAPIHTTP_EV_CAST_STREAM(&this->s_), &buffs, 1);
}

ssize_t manapi::ev::tcp::try_write(const ev::buff_t *buff, uint32_t nbuff) MANAPIHTTP_NOEXCEPT {
    return uv_try_write(MANAPIHTTP_EV_CAST_STREAM(&this->s_), buff, nbuff);
}

int manapi::ev::tcp::s_bind(const sockaddr *addr, uint32_t flags) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_bind(&this->s_, addr, flags);
}

int manapi::ev::tcp::getpeername(sockaddr *name, int *namelen) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_getpeername(&this->s_, name, namelen);
}

int manapi::ev::tcp::getsockname(sockaddr *name, int *namelen) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_getsockname(&this->s_, name, namelen);
}

int manapi::ev::tcp::close_reset(uv_close_cb close_cb) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_close_reset(&this->s_, close_cb);
}

int manapi::ev::tcp::close_reset() MANAPIHTTP_NOEXCEPT {
    return this->close_reset(callback_close_cb);
}

int manapi::ev::tcp::keepalive(int enable, unsigned int delay) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_keepalive(&this->s_, enable, delay);
}

int manapi::ev::tcp::nodelay(int enable) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_nodelay(&this->s_, enable);
}

int manapi::ev::tcp::simultaneous_accepts(int enable) MANAPIHTTP_NOEXCEPT {
    return uv_tcp_simultaneous_accepts(&this->s_, enable);
}

manapi::ev::udp::udp() : s_() {

}

int manapi::ev::udp::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_udp_init(loop, &this->s_);
}

int manapi::ev::udp::s_bind(const sockaddr *addr, uint32_t flags) MANAPIHTTP_NOEXCEPT{
    return uv_udp_bind(&this->s_, addr, flags);
}

int manapi::ev::udp::recv_start() MANAPIHTTP_NOEXCEPT {
    return this->recv_start(ev::callback_watcher_udp_alloc, ev::callback_watcher_udp_recv);
}

int manapi::ev::udp::recv_start(uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_udp_recv_start(&this->s_, alloc, cb);
}

int manapi::ev::udp::recv_stop() MANAPIHTTP_NOEXCEPT{
    return uv_udp_recv_stop(&this->s_);
}

int manapi::ev::udp::connect(const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    return uv_udp_connect(&this->s_, addr);
}

int manapi::ev::udp::try_send(const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    return uv_udp_try_send(&this->s_, buf, nbuf, addr);
}

manapi::ev::udp_send::udp_send() : s_() {

}

int manapi::ev::udp_send::bind(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb,
    const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    return uv_udp_send(&this->s_, stream, buf, nbufs, addr, cb);
}

int manapi::ev::udp_send::bind(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    return this->bind(stream, buf, nbufs, callback_watcher_udp_send, addr);
}

manapi::ev::prepare::prepare() : s_() {

}

int manapi::ev::prepare::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_prepare_init(loop, &this->s_);
}

int manapi::ev::prepare::start() MANAPIHTTP_NOEXCEPT {
    return this->start(ev::callback_watcher_prepare);
}

int manapi::ev::prepare::start(uv_prepare_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_prepare_start(&this->s_, cb);
}

int manapi::ev::prepare::stop() MANAPIHTTP_NOEXCEPT {
    return uv_prepare_stop(&this->s_);
}

manapi::ev::timer::timer() : s_() {

}

int manapi::ev::timer::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return uv_timer_init(loop, &this->s_);
}

int manapi::ev::timer::start(uint64_t timeout, uint64_t repeat) MANAPIHTTP_NOEXCEPT {
    return this->start(timeout, repeat, ev::callback_watcher_timer);
}

int manapi::ev::timer::start(uint64_t timeout, uint64_t repeat, uv_timer_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_timer_start(&this->s_, cb, timeout, repeat);
}

int manapi::ev::timer::stop() MANAPIHTTP_NOEXCEPT {
    return uv_timer_stop(&this->s_);
}

int manapi::ev::timer::again() MANAPIHTTP_NOEXCEPT {
    return uv_timer_again(&this->s_);
}

void manapi::ev::timer::repeat(uint64_t repeat) MANAPIHTTP_NOEXCEPT {
    uv_timer_set_repeat(&this->s_, repeat);
}

uint64_t manapi::ev::timer::repeat() const MANAPIHTTP_NOEXCEPT {
    return uv_timer_get_repeat(&this->s_);
}

uint64_t manapi::ev::timer::due_in() const MANAPIHTTP_NOEXCEPT {
    return uv_timer_get_due_in(&this->s_);
}

manapi::ev::fs::fs(loop_ref loop) : s_(), loop_(loop) {}

int manapi::ev::fs::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::fs::open(const char *path, int flags, int mode, uv_fs_cb open_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_open(this->loop_, &this->s_, path, flags, mode, open_cb);
}

int manapi::ev::fs::open(const char *path, int flags, int mode) MANAPIHTTP_NOEXCEPT {
    return this->open(path, flags, mode, callback_watcher_fs);
}

int manapi::ev::fs::read(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb read_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_read(this->loop_, &this->s_, fileno, buff, nbuff, offset, read_cb);
}

int manapi::ev::fs::read(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT {
    return this->read(fileno, buff, nbuff, offset, callback_watcher_fs);
}

int manapi::ev::fs::write(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset, uv_fs_cb write_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_write(this->loop_, &this->s_, fileno, buff, nbuff, offset, write_cb);
}

int manapi::ev::fs::write(ev::file fileno, const uv_buf_t *buff, uint32_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT {
    return this->write(fileno, buff, nbuff, offset, callback_watcher_fs);
}

ssize_t manapi::ev::fs::try_write(ev::file fileno, const void *buff, std::size_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT {
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
    handle = (HANDLE) _get_osfhandle(fileno);
    if (handle == INVALID_HANDLE_VALUE) {
        return ERR_BADF;
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
                           static_cast<DWORD>(nbuff),
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

ssize_t manapi::ev::fs::try_read(ev::file fileno, void *buff, std::size_t nbuff, int64_t offset) MANAPIHTTP_NOEXCEPT {
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
    handle = (HANDLE) _get_osfhandle(fileno);

    if (handle == INVALID_HANDLE_VALUE) {
        return ERR_BADF;
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
                          static_cast<DWORD>(nbuff),
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

int manapi::ev::fs::close(ev::file fileno, uv_fs_cb close_cb) MANAPIHTTP_NOEXCEPT {
    manapi_log_trace2("manapihttp::fs", manapi::debug::LOG_TRACE_LOW, "fs:fd %d close", fileno);
    return uv_fs_close(this->loop_, &this->s_, fileno, close_cb);
}

int manapi::ev::fs::close(ev::file fileno) MANAPIHTTP_NOEXCEPT {
    return this->close(fileno, callback_watcher_fs);
}

int manapi::ev::fs::unlink(const char *path, uv_fs_cb unlink_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_unlink(this->loop_, &this->s_, path, unlink_cb);
}

int manapi::ev::fs::unlink(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->unlink(path, callback_watcher_fs);
}

int manapi::ev::fs::mkdir(const char *path, int mode, uv_fs_cb mkdir_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_mkdir(this->loop_, &this->s_, path, mode, mkdir_cb);
}

int manapi::ev::fs::mkdir(const char *path, int mode) MANAPIHTTP_NOEXCEPT {
    return this->mkdir(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::mkdtemp(const char *path, uv_fs_cb mkdtemp_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_mkdtemp(this->loop_, &this->s_, path, mkdtemp_cb);
}

int manapi::ev::fs::mkdtemp(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->mkdtemp(path, callback_watcher_fs);
}

int manapi::ev::fs::mkstemp(const char *path, uv_fs_cb mkstemp_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_mkstemp(this->loop_, &this->s_, path, mkstemp_cb);
}

int manapi::ev::fs::mkstemp(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->mkstemp(path, callback_watcher_fs);
}

int manapi::ev::fs::rmdir(const char *path, uv_fs_cb rmdir_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_rmdir(this->loop_, &this->s_, path, rmdir_cb);
}

int manapi::ev::fs::rmdir(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->rmdir(path, callback_watcher_fs);
}

int manapi::ev::fs::opendir(const char *path, uv_fs_cb opendir) MANAPIHTTP_NOEXCEPT {
    return uv_fs_opendir(this->loop_, &this->s_, path, opendir);
}

int manapi::ev::fs::opendir(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->opendir(path, callback_watcher_fs);
}

int manapi::ev::fs::closedir(ev::dir_t * dir, uv_fs_cb closedir_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_closedir(this->loop_, &this->s_, dir, closedir_cb);
}

int manapi::ev::fs::closedir(ev::dir_t * dir) MANAPIHTTP_NOEXCEPT {
    return this->closedir(dir, callback_watcher_fs);
}

int manapi::ev::fs::readdir(ev::dir_t * dir, uv_fs_cb readdir_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_readdir(this->loop_, &this->s_, dir, readdir_cb);
}

int manapi::ev::fs::readdir(ev::dir_t * dir) MANAPIHTTP_NOEXCEPT {
    return this->readdir(dir, callback_watcher_fs);
}

int manapi::ev::fs::scandir(const char *path, int flags, uv_fs_cb scandir_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_scandir(this->loop_, &this->s_, path, flags, scandir_cb);
}

int manapi::ev::fs::scandir(const char *path, int flags) MANAPIHTTP_NOEXCEPT {
    return this->scandir(path, flags, callback_watcher_fs);
}

int manapi::ev::fs::scandir_next(ev::dirent_t *dir) MANAPIHTTP_NOEXCEPT {
    return uv_fs_scandir_next(&this->s_, dir);
}

int manapi::ev::fs::stat(const char *path, uv_fs_cb stat_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_stat(this->loop_, &this->s_, path, stat_cb);
}

int manapi::ev::fs::stat(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->stat(path, callback_watcher_fs);
}

int manapi::ev::fs::fstat(ev::file file, uv_fs_cb fstat_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_fstat(this->loop_, &this->s_, file, fstat_cb);
}

int manapi::ev::fs::fstat(ev::file file) MANAPIHTTP_NOEXCEPT {
    return this->fstat(file, callback_watcher_fs);
}

int manapi::ev::fs::lstat(const char *path, uv_fs_cb lstat_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_lstat(this->loop_, &this->s_, path, lstat_cb);
}

int manapi::ev::fs::lstat(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->lstat(path, callback_watcher_fs);
}

int manapi::ev::fs::statfs(const char *path, uv_fs_cb statfs_cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_statfs(this->loop_, &this->s_, path, statfs_cb);
}

int manapi::ev::fs::statfs(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->statfs(path, callback_watcher_fs);
}

int manapi::ev::fs::rename(const char *path, const char *new_path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_rename(this->loop_, &this->s_, path, new_path, cb);
}

int manapi::ev::fs::rename(const char *path, const char *new_path) MANAPIHTTP_NOEXCEPT {
    return this->rename(path, new_path, callback_watcher_fs);
}

int manapi::ev::fs::fsync(ev::file file, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_fsync(this->loop_, &this->s_, file, cb);
}

int manapi::ev::fs::fsync(ev::file file) MANAPIHTTP_NOEXCEPT {
    return this->fsync(file, callback_watcher_fs);
}

int manapi::ev::fs::fdatasync(ev::file file, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_fdatasync(this->loop_, &this->s_, file, cb);
}

int manapi::ev::fs::fdatasync(ev::file file) MANAPIHTTP_NOEXCEPT {
    return this->fdatasync(file, callback_watcher_fs);
}

int manapi::ev::fs::ftruncate(ev::file file, int64_t off, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_ftruncate(this->loop_, &this->s_, file, off, cb);
}

int manapi::ev::fs::ftruncate(ev::file file, int64_t off) MANAPIHTTP_NOEXCEPT {
    return this->ftruncate(file, off, callback_watcher_fs);
}

int manapi::ev::fs::copyfile(const char *path1, const char *path2, int flags, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_copyfile(this->loop_, &this->s_, path1, path2, flags, cb);
}

int manapi::ev::fs::copyfile(const char *path1, const char *path2, int flags) MANAPIHTTP_NOEXCEPT {
    return this->copyfile(path1, path2, flags, callback_watcher_fs);
}

int manapi::ev::fs::sendfile(ev::file outfd, ev::file infd, int64_t off, size_t length, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_sendfile(this->loop_, &this->s_, outfd, infd, off, length, cb);
}

int manapi::ev::fs::sendfile(ev::file outfd, ev::file infd, int64_t off, size_t length) MANAPIHTTP_NOEXCEPT {
    return this->sendfile(outfd, infd, off, length, callback_watcher_fs);
}

int manapi::ev::fs::access(const char *path, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_access(this->loop_, &this->s_, path, mode, cb);
}

int manapi::ev::fs::access(const char *path, int mode) MANAPIHTTP_NOEXCEPT {
    return this->access(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::chmod(const char *path, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_chmod(this->loop_, &this->s_, path, mode, cb);
}

int manapi::ev::fs::chmod(const char *path, int mode) MANAPIHTTP_NOEXCEPT {
    return this->chmod(path, mode, callback_watcher_fs);
}

int manapi::ev::fs::fchmod(ev::file file, int mode, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_fchmod(this->loop_, &this->s_, file, mode, cb);
}

int manapi::ev::fs::fchmod(ev::file file, int mode) MANAPIHTTP_NOEXCEPT {
    return this->fchmod(file, mode, callback_watcher_fs);
}

int manapi::ev::fs::utime(const char *path, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_utime(this->loop_, &this->s_, path, atime, mtime, cb);
}

int manapi::ev::fs::utime(const char *path, double atime, double mtime) MANAPIHTTP_NOEXCEPT {
    return this->utime(path, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::futime(ev::file file, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_futime(this->loop_, &this->s_, file, atime, mtime, cb);
}

int manapi::ev::fs::futime(ev::file file, double atime, double mtime) MANAPIHTTP_NOEXCEPT {
    return this->futime(file, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::lutime(const char *path, double atime, double mtime, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_lutime(this->loop_, &this->s_, path, atime, mtime, cb);
}

int manapi::ev::fs::lutime(const char *path, double atime, double mtime) MANAPIHTTP_NOEXCEPT {
    return this->lutime(path, atime, mtime, callback_watcher_fs);
}

int manapi::ev::fs::link(const char *path, const char *new_path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_link(this->loop_, &this->s_, path, new_path, cb);
}

int manapi::ev::fs::link(const char *path, const char *new_path) MANAPIHTTP_NOEXCEPT {
    return this->link(path, new_path, callback_watcher_fs);
}

int manapi::ev::fs::symlink(const char *path, const char *new_path, int flags, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_symlink(this->loop_, &this->s_, path, new_path, flags, cb);
}

int manapi::ev::fs::symlink(const char *path, const char *new_path, int flags) MANAPIHTTP_NOEXCEPT {
    return this->symlink(path, new_path, flags, callback_watcher_fs);
}

int manapi::ev::fs::readlink(const char *path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_readlink(this->loop_, &this->s_, path, cb);
}

int manapi::ev::fs::readlink(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->readlink(path, callback_watcher_fs);
}

int manapi::ev::fs::realpath(const char *path, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_realpath(this->loop_, &this->s_, path, cb);
}

int manapi::ev::fs::realpath(const char *path) MANAPIHTTP_NOEXCEPT {
    return this->realpath(path, callback_watcher_fs);
}

int manapi::ev::fs::chown(const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_chown(this->loop_, &this->s_, path, uid, gid, cb);
}

int manapi::ev::fs::chown(const char *path, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT {
    return this->chown(path, uid, gid, callback_watcher_fs);
}

int manapi::ev::fs::fchown(ev::file file, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_fchown(this->loop_, &this->s_, file, uid, gid, cb);
}

int manapi::ev::fs::fchown(ev::file file, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT {
    return this->fchown(file, uid, gid, callback_watcher_fs);
}

int manapi::ev::fs::lchown(const char *path, uid_t uid, gid_t gid, uv_fs_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_fs_lchown(this->loop_, &this->s_, path, uid, gid, cb);
}

int manapi::ev::fs::lchown(const char *path, uid_t uid, gid_t gid) MANAPIHTTP_NOEXCEPT {
    return this->lchown(path, uid, gid, callback_watcher_fs);
}

ssize_t manapi::ev::fs::result() const MANAPIHTTP_NOEXCEPT {
    return this->s_.result;
}

manapi::ev::random::random() : s_() {}

int manapi::ev::random::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::random::bind(loop_ref loop, char *buff, std::size_t size, uv_random_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_random(loop, &this->s_, buff, size, /* flags */ 0, cb);
}

int manapi::ev::random::bind (loop_ref loop, char *buff, std::size_t size) MANAPIHTTP_NOEXCEPT {
    return this->bind(loop, buff, size, callback_watcher_random);
}

manapi::ev::getaddrinfo::getaddrinfo() : s_() {

}

int manapi::ev::getaddrinfo::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::getaddrinfo::bind(loop_ref loop, const char *node, const char *service, const addrinfo *hints,
    uv_getaddrinfo_cb getaddrinfo_cb) MANAPIHTTP_NOEXCEPT {
        return uv_getaddrinfo(loop, &this->s_, getaddrinfo_cb, node, service, hints);
}

int manapi::ev::getaddrinfo::bind(loop_ref loop, const char *node, const char *service,
    const addrinfo *hints) MANAPIHTTP_NOEXCEPT {
        return this->bind(loop, node, service, hints, callback_watcher_getaddrinfo);
}

void manapi::ev::getaddrinfo::free(addrinfo *n) MANAPIHTTP_NOEXCEPT {
    uv_freeaddrinfo(n);
}

manapi::ev::getnameinfo::getnameinfo() : s_() {
}

int manapi::ev::getnameinfo::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel(reinterpret_cast<uv_req_t *> (&this->s_));
}

int manapi::ev::getnameinfo::bind(loop_ref loop, const sockaddr *addr, int flags, uv_getnameinfo_cb cb) MANAPIHTTP_NOEXCEPT {
    return uv_getnameinfo(loop, &this->s_, cb, addr, flags);
}

int manapi::ev::getnameinfo::bind(loop_ref loop, const sockaddr *addr, int flags) MANAPIHTTP_NOEXCEPT {
    return this->bind(loop, addr, flags, callback_watcher_getnameinfo);
}

manapi::ev::work::work() : s_() {}

int manapi::ev::work::cancel() MANAPIHTTP_NOEXCEPT {
    return uv_cancel(reinterpret_cast<uv_req_t *>(&this->s_));
}

int manapi::ev::work::bind(loop_ref loop, uv_work_cb cb, uv_after_work_cb after_cb) MANAPIHTTP_NOEXCEPT {
    return uv_queue_work(loop, &this->s_, cb, after_cb);
}

int manapi::ev::work::bind(loop_ref loop) MANAPIHTTP_NOEXCEPT {
    return this->bind(loop, callback_watcher_work, callback_watcher_after_work);
}

const char * manapi::ev::strerror(int errnum) MANAPIHTTP_NOEXCEPT {
    return uv_strerror(errnum);
}

const char * manapi::ev::namerror(int errnum) MANAPIHTTP_NOEXCEPT {
    return uv_err_name(errnum);
}

std::size_t manapi::ev::hrtime() MANAPIHTTP_NOEXCEPT {
    return ::uv_hrtime();
}

manapi::ev::status::status() {
    this->m_syserr = 0;
}

manapi::ev::status::~status() = default;

manapi::ev::status::status(manapi::err_num code, std::string_view msg, int syserr) : manapi::status(code, msg) {
    this->m_syserr = syserr;
}

manapi::ev::status::status(manapi::err_num code, std::string msg, int syserr) : manapi::status(code, std::move(msg)) {
    this->m_syserr = syserr;
}

manapi::ev::status::status(manapi::err_num code, const char *msg, int syserr) : manapi::status(code, msg) {
    this->m_syserr = syserr;
}

manapi::ev::status::status(status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_syserr = std::exchange(n.m_syserr, 0);
    manapi::status::operator=(std::forward<decltype(n)>(n));
}

manapi::ev::status & manapi::ev::status::operator=(status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_syserr = std::exchange(n.m_syserr, 0);
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

manapi::ev::status::status(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_syserr = 0;
    manapi::status::operator=(std::forward<decltype(n)>(n));
}

manapi::ev::status & manapi::ev::status::operator=(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_syserr = 0;
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

manapi::ev::status::status(const status &n) : manapi::status(n) {
    this->m_syserr = n.m_syserr;
}

manapi::ev::status & manapi::ev::status::operator=(const status &n) {
    this->m_syserr = n.m_syserr;
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

std::string manapi::ev::status::fullmsg() const {
    return std::format("{} syserr={} sysname={} sysmsg={}", manapi::status::fullmsg(), this->m_syserr, this->sysname(), this->sysmsg());
}

int manapi::ev::status::syserr() const {
    return this->m_syserr;
}

std::string_view manapi::ev::status::sysname() const {
    if (!this->m_syserr) return "OK";
    return ev::namerror(this->m_syserr);
}

std::string_view manapi::ev::status::sysmsg() const {
    if (!this->m_syserr) return "OK";
    return ev::strerror(this->m_syserr);
}

manapi::ev::status manapi::ev::status_invalid_argument(std::string_view msg, int syserr) {
    return ev::status{ERR_INVALID_ARGUMENT, msg, syserr};
}

manapi::ev::status manapi::ev::status_resource_exhausted() {
    return ev::status{ERR_RESOURCE_EXHAUSTED, "bad alloc", ev::ERR_NOMEM};
}

manapi::ev::status manapi::ev::status_cancelled() {
    return ev::status_cancelled("cancelled");
}

manapi::ev::status manapi::ev::status_cancelled(std::string_view msg) {
    return ev::status{ERR_CANCELLED, msg, ev::ERR_CANCELED};
}

manapi::ev::status manapi::ev::status_internal(std::string_view msg, int syserr) {
    return ev::status{ERR_INTERNAL, msg, syserr};
}

manapi::ev::status manapi::ev::status_unknown(std::string_view msg, int syserr) {
    return ev::status (manapi::ERR_UNKNOWN, msg, syserr );
}

manapi::ev::status manapi::ev::status_not_found(std::string_view msg) {
    return ev::status{ERR_NOT_FOUND, msg, ev::ERR_NOENT};
}

manapi::ev::status manapi::ev::status_ok() {
    return ev::status{ERR_OK, "OK", 0};
}

manapi::ev::unique_file::unique_file() {
}

manapi::ev::unique_file::unique_file(ev::file fd) : m_fd(fd) {

}

manapi::ev::unique_file::~unique_file() {
    this->reset();
}

manapi::ev::unique_file::unique_file(unique_file &&n) MANAPIHTTP_NOEXCEPT {
    this->m_fd = n.m_fd;
    n.m_fd.reset();
}

manapi::ev::unique_file & manapi::ev::unique_file::operator=(unique_file &&n) MANAPIHTTP_NOEXCEPT {
    this->m_fd = n.m_fd;
    n.m_fd.reset();
    return *this;
}

manapi::status_or<manapi::ev::file> manapi::ev::unique_file::release() MANAPIHTTP_NOEXCEPT {
    if (this->m_fd.has_value()) {
        auto val = this->m_fd.value();
        this->m_fd.reset();
        return manapi::ev::file( val );
    }
    return manapi::status_not_found("unique_file:empty");
}

manapi::ev::file manapi::ev::unique_file::get() const {
    if (this->m_fd.has_value())
        return this->m_fd.value();
    throw manapi::exception (manapi::ERR_NOT_FOUND, "unique_file:empty");
}
//
// manapi::ev::unique_file::operator bool() const MANAPIHTTP_NOEXCEPT {
//     return this->m_fd.has_value();
// }

void manapi::ev::unique_file::reset() MANAPIHTTP_NOEXCEPT {
    if (this->m_fd.has_value()) {
        auto fd = this->release().unwrap();
        ::uv_fs_t req;
        manapi_log_trace2("manapihttp::fs", manapi::debug::LOG_TRACE_LOW, "fs:fd %d close", fd);
        if (auto rhs = ::uv_fs_close(manapi::async::eventloop()->loop(), &req, fd, nullptr))
            manapi_log_error("%s failed due to %s", "uv_fs_close", ev::strerror(rhs));
        ::uv_fs_req_cleanup(&req);
    }
}

void manapi::ev::unique_file::reset(ev::file fd) MANAPIHTTP_NOEXCEPT {
    this->reset();
    this->m_fd = fd;
}

bool manapi::ev::unique_file::has_value() const MANAPIHTTP_NOEXCEPT {
    return this->m_fd.has_value();
}

void manapi::ev::dir_deleter_t::operator()(ev::dir_t *ptr) MANAPIHTTP_NOEXCEPT {
    ::uv_fs_t req;
    if (auto rhs = ::uv_fs_closedir(manapi::async::eventloop()->loop(), &req, ptr, nullptr))
        manapi_log_error("%s failed due to %s", "uv_fs_closedir", ev::strerror(rhs));
    ::uv_fs_req_cleanup(&req);
}
