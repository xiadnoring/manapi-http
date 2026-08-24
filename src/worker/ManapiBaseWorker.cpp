#include <memory.h>
#include <fcntl.h>
#include <memory>

#include "std/ManapiPromise.hpp"
#include "worker/ManapiBaseWorker.hpp"
#include "worker/ManapiBaseUtils.hpp"
#include "../include/ManapiUtils.hpp"

struct wb_write_ctx_t {
    typedef manapi::async::promise_sync<ssize_t> promise;

    manapi::net::worker::base *w;
    manapi::ev::buff_t *buff;
    uint32_t nbuff;
    promise::resolve_t resolve;
};

manapi::net::worker::connection::connection(void *ptr, void (*deleter)(connection *data)): refcnt(0), ptr (ptr), wrk(), version(), deleter(deleter), ipdata(), cancellation() {}

manapi::net::worker::connection::~connection() {
    if (this->deleter)
        this->deleter(this);
}

manapi::net::worker::base::base() = default;

manapi::net::worker::base::~base() = default;

ssize_t manapi::net::worker::base::sync_write_ex(const shared_conn &conn, manapi::slice_view buffs, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    uint32_t const count = std::min<uint32_t>(buffs.slices_size(), 128);
#ifdef _MSC_VER
    ev::buff_t *slices = static_cast<ev::buff_t*>(alloca(sizeof (ev::buff_t) * count));
#else
    ev::buff_t slices[count];
#endif
    std::size_t sz = 0;
    buffs.slices_buffs(slices, count, &sz);
    return this->sync_write_ex(conn, slices, count, sz, finish, maxcnt);
}

ssize_t manapi::net::worker::base::sync_write_ex(const shared_conn &conn, const void *buff, std::size_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    ev::buff_t buffs;
    buffs.base = (char*)(buff);
    buffs.len = static_cast<decltype(buffs.len)>(size);
    return this->sync_write_ex(conn, &buffs, 1, size, finish, maxcnt);
}

ssize_t manapi::net::worker::base::sync_write(const shared_conn &conn, slice_view buffs, bool finish) MANAPIHTTP_NOEXCEPT {
    uint32_t const count = std::min<uint32_t>(buffs.slices_size(), 128);
#ifdef _MSC_VER
    ev::buff_t *slices = static_cast<ev::buff_t*>(alloca(sizeof (ev::buff_t) * count));
#else
    ev::buff_t slices[count];
#endif
    buffs.slices_buffs(slices, count);
    return this->sync_write(conn, slices, count, finish);
}

ssize_t manapi::net::worker::base::sync_write(const shared_conn &conn, const void *buff, std::size_t size, bool finish) MANAPIHTTP_NOEXCEPT {
    ev::buff_t buffs;
    buffs.base = (char*)(buff);
    buffs.len = static_cast<decltype(buffs.len)>(size);
    return this->sync_write(conn, &buffs, 1, finish);
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, const void *buff, std::size_t size, bool finish) {
    ev::buff_t d;
    d.base = (char*)buff;
    d.len = static_cast<decltype(d.len)>(size);
    co_return co_await this->write(conn, &d, 1, finish);
}

manapi::future<ssize_t> write_internal (manapi::net::worker::base *w, ssize_t rhs, const manapi::net::worker::shared_conn &conn, manapi::slice_view buffs, bool finish) {
    using namespace manapi;
    using namespace manapi::net;
    using namespace manapi::net::worker;

    typedef manapi::async::promise_sync<ssize_t> promise;

    if (rhs)
        co_return rhs;

    w->waiting(conn, true);

    struct write_data_t {
        worker::base *w;
        const shared_conn *conn;
        slice_view *buffs;
        bool finish;
        worker_watcher_cb prev_cb;
        int prev_flags;
        promise::resolve_t resolve;
    } write_data{};

    write_data.w = w;
    write_data.conn = &conn;
    write_data.buffs = &buffs;
    write_data.finish = finish;

    try {
        rhs = co_await promise ([&write_data]
            (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            // if (write_data.w->is_send_pending(*write_data.conn)) {
            //     resolve(0);
            //     return;
            // }
            //auto top = (*write_data.conn)->as<connection_prepared_t>()->top.get();
            //assert(top->cur_send_size != top->send_size);
            write_data.resolve=std::move(resolve);
            write_data.prev_cb = write_data.w->event_on(*write_data.conn, [&write_data]
                (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) -> void {
                    try {
                        assert(!buffer && !nsize);

                        if (flags & ev::DISCONNECT) {
                            write_data.resolve(-1);
                            goto finish;
                        }

                        if (flags & ev::WRITE) {
                            auto const rhs = write_data.w->sync_write(conn, *write_data.buffs, write_data.finish);
                            if (rhs) {
                                write_data.resolve(rhs);
                                goto finish;
                            }
                            return;
                        }
                    }
                    catch (...) {
                        //reject(std::current_exception());
                        write_data.resolve(-1);
                        goto finish;
                    }

                    return;
                    finish: write_data.w->event_flags(conn, 0);
            });
            write_data.prev_flags = write_data.w->event_flags(*write_data.conn, ev::WRITE);
        });
        w->waiting(conn, false);

        w->event_on(conn, std::move(write_data.prev_cb));
        w->event_flags(conn, write_data.prev_flags);
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due to %s", "fetch:write", e.what());
        w->waiting(conn, false);
        rhs = -1;
    }

    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    using promise = manapi::async::promise_sync<ssize_t>;
    assert(nbuff > 0);
    auto rhs = this->sync_write(conn, buff, nbuff, finish);

    if (rhs)
        co_return rhs;

    this->waiting(conn, true);

    struct write_data_t {
        worker::base *w;
        const shared_conn *conn;
        manapi::ev::buff_t *buff;
        uint32_t nbuff;
        bool finish;
        worker_watcher_cb prev_cb;
        int prev_flags;
        promise::resolve_t resolve;
    } write_data{};

    write_data.w = this;
    write_data.conn = &conn;
    write_data.buff = buff;
    write_data.nbuff = nbuff;
    write_data.finish = finish;

    try {
        rhs = co_await promise ([&write_data] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            write_data.resolve = std::move(resolve);
            write_data.prev_cb = write_data.w->event_on(*write_data.conn, [&write_data]
                (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) -> void {
                    try {
                        assert(!buffer && !nsize);

                        if (flags & ev::DISCONNECT) {
                            write_data.resolve(-1);
                            goto finish;
                        }

                        if (flags & ev::WRITE) {
                            auto const rhs = write_data.w->sync_write(conn, write_data.buff, write_data.nbuff, write_data.finish);
                            if (rhs) {
                                write_data.resolve(rhs);
                                goto finish;
                            }
                            return;
                        }
                    }
                    catch (...) {
                        //reject(std::current_exception());
                        write_data.resolve(-1);
                        goto finish;
                    }

                    return;
                    finish: write_data.w->event_flags(conn, 0);
            });
            write_data.prev_flags = write_data.w->event_flags(*write_data.conn, ev::WRITE);
        });

        this->waiting(conn, false);
        this->event_on(conn, std::move(write_data.prev_cb));
        this->event_flags(conn, write_data.prev_flags);
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due to %s", "fetch:write", e.what());

        this->waiting(conn, false);
        rhs = -1;
    }


    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, manapi::slice_view buffs, bool finish) {
    using promise = manapi::async::promise_sync<ssize_t>;

    auto rhs = this->sync_write(conn, buffs, finish);

    return write_internal(this, rhs, conn, buffs, finish);
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn,const void *buff, std::size_t size, bool finish) {
    std::size_t total = 0;
    ssize_t rhs = 0;
    while (total < size) {
        rhs = co_await this->write(conn, static_cast<const char *>(buff) + total, size - total, finish);
        if (rhs <= 0)
            co_return rhs;

        total += static_cast<std::size_t> (rhs);
    }
    co_return static_cast<ssize_t>(total);
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn, manapi::slice_view slice, bool finish) {
    size_t total = 0;
    ssize_t rhs = 0;
    auto const size = slice.size();

    while (total < size) {
        rhs = co_await this->write(conn, slice, finish);
        if (rhs <= 0)
            co_return rhs;

        total += static_cast<std::size_t>(rhs);

        if (total == size)
            break;

        slice = slice.subslice(static_cast<std::size_t>(rhs)).unwrap();
    }
    co_return static_cast<ssize_t>(total);
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn, manapi::slice &slice, std::size_t size, bool finish) {
    std::size_t total = 0;
    ssize_t rhs = 0;
    std::size_t shifted = 0;
    auto const slice_size = size;
    auto sv = slice.subslice(0, slice_size).unwrap();

    if (!size) {
        if (finish)
            co_return this->sync_write(conn, sv, finish);
        co_return 0;
    }

    while (total < slice_size) {
        rhs = this->sync_write(conn, sv, finish);

        if (rhs < 0)
            co_return rhs;

        if (!rhs) {
            if (size) {
                assert(!shifted);
                slice.resize(size).unwrap();
                assert(slice.size() == size);
                size = 0;
            }

            assert(slice.size() == (slice_size - shifted));

            if (shifted != total) {
                slice.shift_add(total - shifted).unwrap();
                shifted = total;
                assert(slice.size() == (slice_size - shifted));
            }

            sv = slice_view{slice};

            assert(sv.size() == (slice_size - shifted));
            rhs = co_await write_internal(this, rhs, conn, sv, finish);
            if (rhs <= 0)
                co_return rhs;
        }

        total += static_cast<std::size_t> (rhs);

        if (total == slice_size)
            break;

        sv = sv.subslice(static_cast<std::size_t>(rhs)).unwrap();
    }

    co_return static_cast<ssize_t>(total);
}

std::size_t manapi::net::worker::base::wrk_recv_count(const shared_conn &conn) {
    auto const global = this->wrk_global();
    return global->recv_cnt_pending(conn, global, this);
}

manapi::bytebuffer manapi::net::worker::base::wrk_recv_first_buffer(const shared_conn &conn) {
    auto const global = this->wrk_global();
    return global->recv_buf_pending(conn, global, this);
}

void manapi::net::worker::base::event_toggle(const shared_conn & conn, bool state, int flag) MANAPIHTTP_NOEXCEPT {
    auto flags = this->event_flags(conn);
    if (state) {
        if (!(flags & flag)) {
            this->event_flags(conn, flags | flag);
        }
    }
    else if (flags & flag) {
        this->event_flags(conn, flags ^ flag);
    }
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::base::ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    return conn->ipdata.get();
}

manapi::object_pool & manapi::net::worker::base::bufferpool() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->memory_fabric();
}

int64_t manapi::net::worker::base::stream_id(const shared_conn & s) MANAPIHTTP_NOEXCEPT {
    return 0;
}

manapi::net::worker::shared_conn manapi::net::worker::base::stream_id(const shared_conn &conn, int64_t id) MANAPIHTTP_NOEXCEPT {
    return nullptr;
}

std::size_t manapi::net::worker::base::streams_size(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    return 0;
}

manapi::status_or<manapi::reference<manapi::net::worker::connection>> manapi::net::worker::base::new_stream(const shared_conn & conn, int flags) MANAPIHTTP_NOEXCEPT {
    return status_unimplemented("worker:Streams not supported");
}

void manapi::net::worker::base::connection_io_merge(connection_io_part *dest, connection_io_part *src, uint32_t *dest_cnt, uint32_t *src_cnt, std::size_t max_cnt) MANAPIHTTP_NOEXCEPT {
    while ((*dest_cnt) < max_cnt) {
        if (!src->deque) {
            return;
        }

        if (src->deque_current) {
            auto size = src->deque->buffer.size();

            if (src->deque.get() == src->last_deque) {
                src->deque_cursor -= src->deque_current;
                size = src->deque_cursor;
            }
            else {
                size -= src->deque_current;
            }

            memcpy (src->deque->buffer.data(), src->deque->buffer.data() + src->deque_current, size);
            src->deque_current = 0;

            if (src->deque.get() != src->last_deque) {
                auto res = src->deque->buffer.resize(size).ok();
                assert(res);
            }
        }

        auto obj = std::move(src->deque);
        src->deque = std::move(obj->next);

        auto prev_deque_cursor = dest->deque_cursor;

        if (src->deque) {
            dest->deque_cursor = static_cast<uint32_t>(obj->buffer.size());
        }
        else {
            dest->deque_cursor = src->deque_cursor;
            auto res = obj->buffer.resize(src->deque_cursor).ok();
            assert(res);
            src->last_deque = nullptr;
            src->deque_cursor = 0;
        }

        if (dest->last_deque) {
            if (dest->last_deque->buffer.size() != prev_deque_cursor) {
                auto res = dest->last_deque->buffer.resize(prev_deque_cursor).ok();
                assert(res);
            }
            dest->last_deque->next = std::move(obj);
            dest->last_deque = dest->last_deque->next.get();
        }
        else {
            dest->deque = std::move(obj);
            dest->last_deque = dest->deque.get();
        }

        (*dest_cnt)++;
        assert((*src_cnt));
        (*src_cnt)--;
    }
}

ssize_t manapi::net::worker::base::connection_io_send(connection_io_part *top, const char *buffer, std::size_t size, object_pool *bufferpool, uint32_t buffer_size, uint32_t *cnt, std::size_t max_cnt) MANAPIHTTP_NOEXCEPT {
    try {
        std::size_t rhs = 0;
        while (rhs != size) {
            if (!top->last_deque
                || top->deque_cursor == top->last_deque->buffer.size()) {

                if (top->last_deque) {
                    auto const payload_size = top->last_deque->buffer.size() + top->last_deque->buffer.shift();
                    if (top->last_deque->buffer.realsize() != payload_size) {
                        auto resize_res = top->last_deque->buffer.resize(top->last_deque->buffer.realsize() - top->last_deque->buffer.shift());
                        assert(resize_res.ok());
                        continue;
                    }
                }

                if (cnt && prepared::buffs_is_full(top, *cnt, max_cnt))
                    break;

                auto bufres = bufferpool->buffer(buffer_size);
                if (!bufres.ok())
                    return -1;

                std::unique_ptr<buffer_deque> object (new (std::nothrow) buffer_deque(bufres.unwrap(), nullptr));

                if (top->last_deque) {
                    top->last_deque->next = std::move(object);
                    top->last_deque = top->last_deque->next.get();
                }
                else {
                    top->deque = std::move(object);
                    top->last_deque = top->deque.get();
                }

                top->deque_cursor = 0;

                if (cnt)
                    (*cnt)++;
            }

            assert(top->last_deque->buffer.size() > top->deque_cursor);
            auto const copy = std::min<std::size_t>(size - rhs, top->last_deque->buffer.size() - top->deque_cursor);
            memcpy (top->last_deque->buffer.data() + top->deque_cursor, buffer + rhs, copy);
            rhs += copy;
            top->deque_cursor += static_cast<uint32_t>(copy);
        }
        return static_cast<ssize_t>(rhs);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "connection_io_send failed", e.what());
    }

    return -1;
}

int manapi::net::worker::base::connection_io_send_start(connection_io_part *top, const char *buffer, std::size_t size, object_pool *bufferpool, uint32_t buffer_size, ibuffpool_t *buff, uint32_t *cnt) MANAPIHTTP_NOEXCEPT {
    try {
        if (top->deque && top->deque_current >= size) {
            top->deque_current -= static_cast<uint32_t>(size);
            memcpy (top->deque->buffer.data() + top->deque_current, buffer, size);
            return ERR_OK;
        }

        if (buff && buff->size() > buffer_size)
            /* it may be a recv buffer */
                buff = nullptr;

        ibuffpool_t tmp;
        if (buff) {
            buff->shift_add(static_cast<uint32_t>(buff->size() - size));
        }
        else {
            auto bufres = bufferpool->buffer (size, buffer_size);
            if (!bufres.ok())
                return ERR_INTERNAL;

            tmp = bufres.unwrap();
            buff = &tmp;
            memcpy (buff->data(), buffer, size);
        }

        if (top->deque) {
            auto obj = std::make_unique<buffer_deque>();
            top->deque->buffer.shift_add(top->deque_current);
            if (top->deque.get() == top->last_deque) {
                assert(top->deque_cursor >= top->deque_current);
                top->deque_cursor -= top->deque_current;
            }
            top->deque_current = 0;
            obj->buffer = std::move(*buff);
            obj->next = std::move(top->deque);
            top->deque = std::move(obj);
            (*cnt)++;
        }
        else {
            top->deque = std::make_unique<buffer_deque>(std::move(*buff), nullptr);
            top->last_deque = top->deque.get();
            top->deque_current = 0;
            top->deque_cursor = static_cast<uint32_t>(top->deque->buffer.size());
            auto resize_res = top->deque->buffer.realresize(top->deque->buffer.realsize());
            assert(resize_res.ok());
            (*cnt)++;
        }
        return ERR_OK;
    }
    catch (std::bad_alloc const &) {
        return ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "connection_io_send_start", e.what());
    }
    return ERR_INTERNAL;
}

ssize_t manapi::net::worker::base::buffs_cut_by_size(ev::buff_t *buff, uint32_t &nbuff, std::size_t limit_size, bool &fin) MANAPIHTTP_NOEXCEPT {
    std::size_t size = 0;

    if (!limit_size)
        return 0;

    for (uint32_t i = 0; i < nbuff; ++i) {
        auto const want = (limit_size - size);
        if (buff[i].len >= want) {
            /* cut it */
            buff[i].len = static_cast<decltype(buff[i].len)>(want);
            /* current number */
            nbuff = i + 1;
            /* obviously */
            size = limit_size;
            /* was cut */
            if (fin)
                fin = buff[i].len == want;

            break;
        }
        size += buff[i].len;
    }

    return static_cast<ssize_t>(size);
}

int manapi::net::worker::base::call_user_callback(worker_watcher_cb *cb, const shared_conn & conn, int flags, const char *buffer, std::size_t nsize, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    try {
        if (cb && *cb)
            cb->operator()(conn, flags, buffer, nsize, p);
        return manapi::ERR_OK;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "call_user_callback", e.what());
    }

    return manapi::ERR_ABORTED;
}

void manapi::net::worker::base::feed_event_read_(const shared_conn &conn, worker_watcher_cb *cb, connection_io_part *recv, uint32_t *recv_size, int conn_flags, int flags, const char *buff, std::size_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    try {
        bool processed = false;
        if (conn_flags & ev::READ && cb) {
            if (flags & manapi::net::worker::base::CONN_TOP_READ) {
                if (conn_flags & ev::READ) {
                    manapi::net::worker::base::call_user_callback(cb, conn, flags, buff, size, p);
                    processed = true;
                }
            }
            else {
                if (conn_flags & ev::READ) {
                    manapi::net::worker::base::call_user_callback(cb, conn, flags, buff, size, p);
                    processed = true;
                }
            }
        }
        if (!processed && size) {
            if (conn_flags & CONN_TOP_READ) {
                auto rhs = connection_io_send_start(recv, buff, size, &this->bufferpool(), this->config()->buffer_size, p, recv_size);
                if (!rhs)
                    goto err;
            }
            else {
                auto rhs = connection_io_send(recv, buff, size, &this->bufferpool(), this->config()->buffer_size, recv_size, WORKER_MAX_CNT);
                if (rhs < 0)
                    goto err;
            }
        }
        return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "feed_event", e.what());
    }

    err: this->close_connection(conn, CLOSE_CONN_ERR);
}

void manapi::net::worker::base::connection_io_trim(struct connection_io_part *top, buffer_deque *parent, uint32_t *cnt) MANAPIHTTP_NOEXCEPT {
    if (!top->deque_cursor && top->last_deque) {
        if (parent) {
            parent->next = nullptr;
            top->deque_cursor = static_cast<uint32_t>(parent->buffer.size());
            top->last_deque = parent;
        }
        else {
            /* there is one element in the stack */
            top->deque = nullptr;
            top->last_deque = nullptr;
            top->deque_current = 0;
            top->deque_cursor = 0;
        }

        if (cnt) {
            assert(*cnt);
            (*cnt)--;
        }
    }
}

ssize_t manapi::net::worker::base::connection_io_recv(connection_io_part *top, char *buffer, std::size_t size, int *cnt) MANAPIHTTP_NOEXCEPT {
    std::size_t rhs = 0;

    while (rhs != size) {
        if (!top->last_deque) {
            break;
        }

        auto buffer_size = (top->last_deque == top->deque.get()
            ? static_cast<std::size_t>(top->deque_cursor)
            : static_cast<std::size_t>(top->deque->buffer.size()));

        auto copy = std::min<std::size_t>(size - rhs, buffer_size - top->deque_current);
        memcpy(static_cast<char *>(buffer) + rhs,
            top->deque->buffer.data() + top->deque_current, copy);

        rhs += copy;
        top->deque_current += static_cast<uint32_t>(copy);
        top->deque->buffer.shift_add(top->deque_current);
        if (top->deque.get() == top->last_deque) {
            assert(top->deque_cursor >= top->deque_current);
            top->deque_cursor -= top->deque_current;
        }
        top->deque_current = 0;

        if (top->deque->buffer.empty()) {
            top->deque = std::move(top->deque->next);
            if (!top->deque) {
                top->last_deque = nullptr;
                top->deque_cursor = 0;
            }
            top->deque_current = 0;
            (*cnt)--;
        }
    }
    return static_cast<ssize_t>(rhs);
}