#include "components/ManapiEventStructures.hpp"

#define MANAPI_EV_NOEXPECT noexcept(true)
#define MANAPI_EV_CAST_STREAM(x) reinterpret_cast<uv_stream_t *> (x)
#define MANAPI_EV_CAST_HANDLE(x) reinterpret_cast <uv_handle_t *> (x)
#define MANAPI_EV_DEFAULT(name_class, name_struct) \
void manapi::ev::name_class::unbind (uv_close_cb cb) MANAPI_EV_NOEXPECT {  uv_close(MANAPI_EV_CAST_HANDLE(&this->s_), cb); }\
void manapi::ev::name_class::unbind () MANAPI_EV_NOEXPECT { uv_close(MANAPI_EV_CAST_HANDLE (&this->s_), nullptr); }\
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
#define MANAPI_EV_CHECK(expr) { auto rhs = expr; if (rhs) { std::cout << rhs << "\n"; THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_EXTERNAL_LIB_CRASH, #expr); } }

MANAPI_EV_DEFAULT(async, uv_async_t)


MANAPI_EV_DEFAULT(timer, uv_timer_t)

MANAPI_EV_DEFAULT(io, uv_poll_t)
MANAPI_EV_DEFAULT(check, uv_check_t)
MANAPI_EV_DEFAULT(prepare, uv_prepare_t)
MANAPI_EV_DEFAULT(idle, uv_idle_t)
MANAPI_EV_DEFAULT(tcp, uv_tcp_t)
MANAPI_EV_DEFAULT(udp, uv_udp_t)
MANAPI_EV_DEFAULT(write, uv_write_t)
MANAPI_EV_DEFAULT(udp_send, uv_udp_send_t)

MANAPI_EV_STREAM(udp, uv_udp_t)
MANAPI_EV_STREAM(tcp, uv_tcp_t)

manapi::ev::async::async(loop_ref loop) : async(loop, manapi::ev::callback_watcher_async) {}

manapi::ev::async::async(loop_ref loop, uv_async_cb cb) : s_() {
    MANAPI_EV_CHECK(uv_async_init(loop, &this->s_, cb));
}

int manapi::ev::async::send() MANAPI_EV_NOEXPECT {
    return uv_async_send(&this->s_);
}

void manapi::ev::async::set(uv_async_cb cb) MANAPI_EV_NOEXPECT {
    auto data = this->data();
    auto loop = this->loop();

    this->unbind();

    MANAPI_EV_CHECK(uv_async_init(loop, &this->s_, cb));
    this->data(data);
}

manapi::ev::idle::idle(loop_ref loop) : s_() {
    MANAPI_EV_CHECK(this->init(loop));
}

int manapi::ev::idle::init(loop_ref loop) MANAPI_EV_NOEXPECT {
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

manapi::ev::check::check(loop_ref loop) : s_() {
    MANAPI_EV_CHECK(this->init(loop));
}

int manapi::ev::check::init(loop_ref loop) MANAPI_EV_NOEXPECT {
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
    return this->custom()->io_watcher.pevents;
}

manapi::ev::write::write(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_write_cb cb) : s_() {
    MANAPI_EV_CHECK(uv_write(&this->s_, stream, buf, nbufs, cb));
}

manapi::ev::write::write(uv_stream_t *stream, const uv_buf_t *buf, uint32_t nbufs)
    : write(stream, buf, nbufs, ev::callback_watcher_write) {}

manapi::ev::tcp::tcp(loop_ref loop) : s_() {
    MANAPI_EV_CHECK(uv_tcp_init(loop, &this->s_));
}

int manapi::ev::tcp::accept(tcp *parent) MANAPI_EV_NOEXPECT {
    return uv_accept(MANAPI_EV_CAST_STREAM(&parent->s_), MANAPI_EV_CAST_STREAM(&this->s_));
}

int manapi::ev::tcp::read_start() MANAPI_EV_NOEXPECT {
    return this->read_start(ev::callback_watcher_alloc, ev::callback_watcher_tcp_read);
}

int manapi::ev::tcp::read_start(uv_alloc_cb alloc, uv_read_cb cb) MANAPI_EV_NOEXPECT {
    return uv_read_start(MANAPI_EV_CAST_STREAM(&this->s_), alloc, cb);
}

int manapi::ev::tcp::read_stop() MANAPI_EV_NOEXPECT {
    return uv_read_stop(MANAPI_EV_CAST_STREAM(&this->s_));
}

int manapi::ev::tcp::bind(sockaddr *addr, int flags) MANAPI_EV_NOEXPECT {
    return uv_tcp_bind(&this->s_, addr, flags);
}

manapi::ev::udp::udp(loop_ref loop) : s_() {
    MANAPI_EV_CHECK(uv_udp_init(loop, &this->s_));
}

int manapi::ev::udp::bind(sockaddr *addr, int flags) MANAPI_EV_NOEXPECT{
    return uv_udp_bind(&this->s_, addr, flags);
}

int manapi::ev::udp::recv_start() MANAPI_EV_NOEXPECT {
    return this->recv_start(ev::callback_watcher_alloc, ev::callback_watcher_udp_recv);
}

int manapi::ev::udp::recv_start(uv_alloc_cb alloc, uv_udp_recv_cb cb) MANAPI_EV_NOEXPECT {
    return uv_udp_recv_start(&this->s_, alloc, cb);
}

int manapi::ev::udp::recv_stop() MANAPI_EV_NOEXPECT{
    return uv_udp_recv_stop(&this->s_);
}

int manapi::ev::udp::try_send(const uv_buf_t *buf, uint32_t nbuf, sockaddr *addr) MANAPI_EV_NOEXPECT {
    return uv_udp_try_send(&this->s_, buf, nbuf, addr);
}

int manapi::ev::udp::try_send(int count, uv_buf_t **buf, uint32_t *nbuf, sockaddr **addr, int flags) MANAPI_EV_NOEXPECT {
    return uv_udp_try_send2(&this->s_, count, buf, nbuf, addr, flags);
}

manapi::ev::udp_send::udp_send(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, uv_udp_send_cb cb, const sockaddr *addr) : s_() {
    MANAPI_EV_CHECK(uv_udp_send(&this->s_, stream, buf, nbufs, addr, cb));
}

manapi::ev::udp_send::udp_send(uv_udp_t *stream, const uv_buf_t *buf, uint32_t nbufs, const sockaddr *addr)
    : udp_send(stream, buf, nbufs, callback_watcher_udp_send, addr) {}

manapi::ev::prepare::prepare(loop_ref loop) : s_() {
    MANAPI_EV_CHECK(uv_prepare_init(loop, &this->s_));
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

manapi::ev::timer::timer(loop_ref loop) : s_() {
    uv_timer_init(loop, &this->s_);
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
