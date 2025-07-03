#include "worker/ManapiBaseWorker.hpp"
#include <memory.h>
#ifdef _WIN32
#   include <processthreadsapi.h>
#endif
#include <fcntl.h>
#include <memory>

struct wb_write_ctx_t {
    using promise = manapi::async::promise<ssize_t, std::false_type>;

    manapi::net::worker::base *w;
    manapi::ev::buff_t *buff;
    uint32_t nbuff;
    promise::resolve_t resolve;
};

manapi::net::worker::connection::connection(void *ptr): ptr (ptr), wrk(), version(), ipdata(), cancellation() {}

manapi::net::worker::base::base() = default;

manapi::net::worker::base::~base() = default;

ssize_t manapi::net::worker::base::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    ev::buff_t buffs;
    buffs.base = (char*)(buff);
    buffs.len = static_cast<std::size_t> (size);
    return this->sync_write_ex(conn, &buffs, 1, size, finish, maxcnt);
}

ssize_t manapi::net::worker::base::sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    ev::buff_t buffs;
    buffs.base = (char*)(buff);
    buffs.len = static_cast<std::size_t> (size);
    return this->sync_write(conn, &buffs, 1, finish);
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    using promise = manapi::async::promise<ssize_t, std::false_type>;

    auto rhs = this->sync_write(conn, buff, size, finish);

    if (rhs)
        co_return rhs;

    int prev_flags;
    std::unique_ptr<worker_watcher_cb> prev_cb;

    this->waiting(conn, true);

    try {
        rhs = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            prev_cb = this->event_on(conn, std::make_unique<worker_watcher_cb>([this, buff, size, finish, resolve = std::move(resolve), reject = std::move(reject)]
                (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) -> void {
                    try {
                        assert(!buffer && !nsize);

                        if (flags & ev::DISCONNECT) {
                            resolve(-1);
                            goto finish;
                        }

                        if (flags & ev::WRITE) {
                            auto const rhs = this->sync_write(conn, buff, size, finish);
                            if (rhs) {
                                resolve(rhs);
                                goto finish;
                            }
                            return;
                        }
                    }
                    catch (...) {
                        //reject(std::current_exception());
                        resolve(-1);
                        goto finish;
                    }

                    return;
                    finish: this->event_flags(conn, 0);
            }));
            prev_flags = this->event_flags(conn, ev::WRITE);
        });
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("write(...) failed due to {}", e.what());

        rhs = -1;
    }

    this->waiting(conn, false);

    this->event_on(conn, std::move(prev_cb));
    this->event_flags(conn, prev_flags);

    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    using promise = manapi::async::promise<ssize_t, std::false_type>;
    assert(nbuff > 0);
    auto rhs = this->sync_write(conn, buff, nbuff, finish);

    if (rhs)
        co_return rhs;

    int prev_flags;
    std::unique_ptr<worker_watcher_cb> prev_cb;

    this->waiting(conn, true);

    try {
        rhs = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
            -> void {
            prev_cb = this->event_on(conn, std::make_unique<worker_watcher_cb>([this, buff, nbuff, finish, resolve = std::move(resolve), reject = std::move(reject)]
                (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) -> void {
                    try {
                        assert(!buffer && !nsize);

                        if (flags & ev::DISCONNECT) {
                            resolve(-1);
                            goto finish;
                        }

                        if (flags & ev::WRITE) {
                            auto const rhs = this->sync_write(conn, buff, nbuff, finish);
                            if (rhs) {
                                resolve(rhs);
                                goto finish;
                            }
                            return;
                        }
                    }
                    catch (...) {
                        //reject(std::current_exception());
                        resolve(-1);
                        goto finish;
                    }

                    return;
                    finish: this->event_flags(conn, 0);
            }));
            prev_flags = this->event_flags(conn, ev::WRITE);
        });
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("write(...) failed due to {}", e.what());

        rhs = -1;
    }

    this->waiting(conn, false);

    this->event_on(conn, std::move(prev_cb));
    this->event_flags(conn, prev_flags);

    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn,const void *buff, ssize_t size, bool finish) {
    ssize_t total = 0;
    ssize_t rhs = 0;
    while (total < size) {
        rhs = co_await this->write(conn, static_cast<const char *>(buff) + total, size - total, finish);
        if (rhs <= 0)
            co_return rhs;

        total += rhs;
    }
    co_return total;
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn, manapi::slice_view slice, bool finish) {
    ssize_t total = 0;
    ssize_t rhs = 0;
    auto const size = slice.size();

    while (total < size) {
        auto buffs = slice.slices_buffs();
        rhs = co_await this->write(conn, buffs.get(), slice.slices_size(), finish);
        if (rhs <= 0)
            co_return rhs;

        total += rhs;

        if (total == size)
            break;

        slice = slice.subslice(rhs).unwrap();
        //
        // while (nbuff && rhs >= buffptr->len) {
        //     rhs -= buffptr->len;
        //     buffptr++;
        //     nbuff--;
        // }
        //
        // if (nbuff) {
        //     buffptr->base += rhs;
        //     buffptr->len -= rhs;
        // }
    }
    co_return total;
}

void manapi::net::worker::base::event_toggle(const shared_conn & conn, bool state, int flag) {
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

manapi::net::worker::connection::ipdata_t * manapi::net::worker::base::ipdata(worker::connection *conn) {
    return conn->ipdata.get();
}

manapi::object_pool & manapi::net::worker::base::bufferpool() {
    return manapi::async::current()->memory_fabric();
}




void manapi::net::worker::base::connection_io_merge(connection_io_part *dest, connection_io_part *src, int *dest_cnt, int *src_cnt, int max_cnt) {
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
                src->deque->buffer.resize(size);
            }
        }

        auto obj = std::move(src->deque);
        src->deque = std::move(obj->next);

        auto prev_deque_cursor = dest->deque_cursor;

        if (src->deque) {
            dest->deque_cursor = static_cast<int>(obj->buffer.size());
        }
        else {
            dest->deque_cursor = src->deque_cursor;
            obj->buffer.resize(src->deque_cursor);
            src->last_deque = nullptr;
            src->deque_cursor = 0;
        }

        if (dest->last_deque) {
            if (dest->last_deque->buffer.size() != prev_deque_cursor) {
                dest->last_deque->buffer.resize(prev_deque_cursor);
            }
            dest->last_deque->next = std::move(obj);
            dest->last_deque = dest->last_deque->next.get();
        }
        else {
            dest->deque = std::move(obj);
            dest->last_deque = dest->deque.get();
        }

        (*dest_cnt)++;
        (*src_cnt)--;
    }
}

ssize_t manapi::net::worker::base::connection_io_send(connection_io_part *top, const char *buffer, ssize_t size, object_pool *bufferpool, int buffer_size, int *cnt, int max_cnt) {
    try {
        ssize_t rhs = 0;
        while (rhs != size) {
            if (!top->last_deque
                || top->deque_cursor == top->last_deque->buffer.size()) {
                if (cnt && *cnt >= max_cnt)
                    break;

                auto object = std::make_unique<buffer_deque>(bufferpool->buffer(buffer_size), nullptr);

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

            auto const copy = std::min(size - rhs, static_cast<ssize_t>(top->last_deque->buffer.size() - top->deque_cursor));
            memcpy (top->last_deque->buffer.data() + top->deque_cursor, buffer + rhs, copy);
            rhs += copy;
            top->deque_cursor += copy;
        }
        return rhs;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            manapi::ERR_INTERNAL, "connection_io_send(...): {}", e.what());
    }

    return -1;
}

void manapi::net::worker::base::connection_io_send_start(connection_io_part *top, const char *buffer, ssize_t size, object_pool *bufferpool, int buffer_size, ibuffpool_t *buff, int *cnt) {
    if (top->deque && top->deque_current >= size) {
        top->deque_current -= static_cast<int>(size);
        memcpy (top->deque->buffer.data() + top->deque_current, buffer, size);
        return;
    }

    if (buff && buff->size() > buffer_size)
        /* it may be a recv buffer */
        buff = nullptr;

    ibuffpool_t tmp;
    if (buff) {
        buff->shift_add(static_cast<int>(buff->size() - size));
    }
    else {
        tmp = bufferpool->buffer (size, buffer_size);
        buff = &tmp;
        memcpy (buff->data(), buffer, size);
    }

    if (top->deque) {
        top->deque->buffer.shift_add(top->deque_current);
        top->deque_current = 0;
        auto obj = std::make_unique<buffer_deque>(std::move(*buff), std::move(top->deque));
        top->deque = std::move(obj);
        (*cnt)++;
    }
    else {
        top->deque = std::make_unique<buffer_deque>(std::move(*buff), nullptr);
        top->last_deque = top->deque.get();
        top->deque_current = 0;
        top->deque_cursor = static_cast<int>(top->deque->buffer.size());
        top->deque->buffer.realresize(top->deque->buffer.realsize());
        (*cnt)++;
    }
}

ssize_t manapi::net::worker::base::buffs_cut_by_size(ev::buff_t *buff, uint32_t &nbuff, ssize_t limit_size, bool &fin) {
    ssize_t size = 0;

    if (limit_size <= 0)
        return 0;

    for (uint32_t i = 0; i < nbuff; ++i) {
        auto const want = (limit_size - size);
        if (buff[i].len >= want) {
            /* cut it */
            buff[i].len = want;
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

    return size;
}

int manapi::net::worker::base::call_user_callback(const std::unique_ptr<worker_watcher_cb> &cb, const shared_conn & conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) {
    try {
        cb->operator()(conn, flags, buffer, nsize, p);
        return manapi::ERR_OK;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("worker: user callback failed due to {}", e.what());
    }

    return manapi::ERR_ABORTED;
}

void manapi::net::worker::base::feed_event_read_(const shared_conn &conn, worker_watcher_cb *cb, connection_io_part *recv, int *recv_size, int conn_flags, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    try {
        bool processed = false;
        if (conn_flags & ev::READ && cb) {
            if (flags & manapi::net::worker::base::CONN_TOP_READ) {
                if (conn_flags & ev::READ) {
                    cb->operator()(conn, flags, buff, size, p);
                    processed = true;
                }
            }
            else {
                if (conn_flags & ev::READ) {
                    cb->operator()(conn, flags, buff, size, p);
                    processed = true;
                }
            }
        }
        if (!processed && size) {
            if (conn_flags & CONN_TOP_READ) {
                connection_io_send_start(recv, buff, size, &this->bufferpool(), this->config()->buffer_size, p, recv_size);
            }
            else {
                connection_io_send(recv, buff, size, &this->bufferpool(), this->config()->buffer_size, recv_size, 1e5);
            }
        }
        return;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("feed_event failed: {}", e.what());
    }
    this->close_connection(conn, false);
}

void manapi::net::worker::base::connection_io_trim(struct connection_io_part *top, buffer_deque *parent, int *cnt) {
    if (!top->deque_cursor && top->last_deque) {
        if (parent) {
            parent->next = nullptr;
            top->deque_cursor = static_cast<int>(parent->buffer.size());
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
            (*cnt)--;
        }
    }
}

ssize_t manapi::net::worker::base::connection_io_recv(connection_io_part *top, char *buffer, ssize_t size, int *cnt) {
    ssize_t rhs = 0;

    while (rhs != size) {
        if (!top->last_deque) {
            break;
        }

        auto buffer_size = (top->last_deque == top->deque.get()
            ? static_cast<ssize_t>(top->deque_cursor)
            : static_cast<ssize_t>(top->deque->buffer.size()));

        auto copy = std::min(size - rhs, buffer_size - top->deque_current);
        memcpy(static_cast<char *>(buffer) + rhs,
            top->deque->buffer.data() + top->deque_current, copy);

        rhs += copy;
        top->deque_current += static_cast<int>(copy);
        top->deque->buffer.shift_add(top->deque_current);
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
    return rhs;
}