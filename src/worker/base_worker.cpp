#include "worker/base_worker.hpp"
#include <memory.h>
#ifdef _WIN32
#   include <processthreadsapi.h>
#endif
#include <fcntl.h>
#include <memory>

manapi::net::worker::connection::connection(void *ptr, void(*eraser)(void*)): client(), ptr (ptr, eraser) {}

manapi::net::worker::base::base(net::site &site) :site_(site) {
    this->bufferpool_ = std::make_shared<decltype(this->bufferpool_)::element_type>();
    // initialization object pools
    this->bufferpool()->init(0);
}

manapi::net::worker::base::~base() = default;

void manapi::net::worker::base::config(std::shared_ptr<manapi::net::http::config> config) {
    this->config_ = std::move(config);
}

manapi::future<ssize_t> manapi::net::worker::base::write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    using promise = manapi::async::promise<ssize_t, std::false_type>;

    auto rhs = this->sync_write(conn, buff, size, finish);

    if (rhs) {
        co_return rhs;
    }

    int prev_flags;
    std::unique_ptr<worker_watcher_cb> prev_cb;

    rhs = co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject)
        -> void {
        prev_flags = this->event_flags(conn.get(), ev::WRITE);
        prev_cb = this->event_on(conn.get(), [this, buff, size, finish, resolve = std::move(resolve), reject = std::move(reject)]
            (const shared_conn &conn, int flags, ibuffpool_t buffer) -> void {
                try {
                    if (flags & ev::DISCONNECT) {
                        resolve(-1);
                        return;
                    }

                    if (flags & ev::WRITE) {
                        resolve(this->sync_write(conn, buff, size, finish));
                        return;
                    }
                }
                catch (...) {
                    //reject(std::current_exception());
                    resolve(-1);
                }
        });
    });

    this->event_flags(conn.get(), prev_flags);
    this->event_on(conn.get(), std::move(prev_cb));

    co_return rhs;
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(const shared_conn &conn,const void *buff, ssize_t size, bool finish) {
    ssize_t total = 0;
    ssize_t rhs = 0;
    while (total < size) {
        rhs = co_await this->write(conn, static_cast<const char *>(buff) + total, size - total, finish);
        if (rhs <= 0) {
            co_return rhs;
        }
        total += rhs;
    }
    co_return total;
}

manapi::future<ssize_t> manapi::net::worker::base::response(const worker::shared_conn &connection, http::response *resp, bool finish) {
    co_return -1;
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::base::event_on(worker::connection *conn, worker_watcher_cb callback) {
    return this->event_on(conn, std::make_unique<decltype(callback)>(std::move(callback)));
}

void manapi::net::worker::base::event_toggle(worker::connection *conn, bool state, int flag) {
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

manapi::net::site & manapi::net::worker::base::site() {
    return this->site_;
}

manapi::net::http::config *manapi::net::worker::base::config() {
    return this->config_.get();
}

const manapi::net::worker::base::bufferpool_t & manapi::net::worker::base::bufferpool() {
    return this->bufferpool_;
}


void manapi::net::worker::base::connection_io_merge(connection_io_part *dest, connection_io_part *src, int *dest_cnt, int *src_cnt, int max_cnt) {
    while ((*dest_cnt) < max_cnt) {
        if (!src->deque) {
            return;
        }

        if (src->deque_current) {
            auto size = src->deque->buffer->size();

            if (src->deque.get() == src->last_deque) {
                src->deque_cursor -= src->deque_current;
                size = src->deque_cursor;
            }
            else {
                size -= src->deque_current;
            }

            memcpy (src->deque->buffer->data(), src->deque->buffer->data() + src->deque_current, size);
            src->deque_current = 0;

            if (src->deque.get() != src->last_deque) {
                src->deque->buffer->resize(size);
            }
        }

        auto obj = std::move(src->deque);
        src->deque = std::move(obj->next);

        auto prev_deque_cursor = dest->deque_cursor;

        if (src->deque) {
            dest->deque_cursor = static_cast<int>(obj->buffer->size());
        }
        else {
            dest->deque_cursor = src->deque_cursor;
            obj->buffer->resize(src->deque_cursor);
            src->last_deque = nullptr;
            src->deque_cursor = 0;
        }

        if (dest->last_deque) {
            if (dest->last_deque->buffer->size() != prev_deque_cursor) {
                dest->last_deque->buffer->resize(prev_deque_cursor);
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

ssize_t manapi::net::worker::base::connection_io_send(connection_io_part *top, const char *buffer, ssize_t size, object_pool<bytebuffer, std::false_type, std::size_t> *bufferpool, int buffer_size, int *cnt, int max_cnt) {
    ssize_t rhs = 0;
    while (rhs != size) {
        if (!top->last_deque || top->deque_cursor == buffer_size) {
            if (cnt && *cnt >= max_cnt)
                break;

            auto object = std::make_unique<buffer_deque>(bufferpool->get(), nullptr);
            object->buffer->resize(buffer_size);
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

        auto const copy = std::min(size - rhs, static_cast<ssize_t>(top->last_deque->buffer->size() - top->deque_cursor));
        memcpy (top->last_deque->buffer->data() + top->deque_cursor, buffer + rhs, copy);
        rhs += copy;
        top->deque_cursor += copy;
    }
    return rhs;
}

void manapi::net::worker::base::connection_io_trim(struct connection_io_part *top, buffer_deque *parent, int *cnt) {
    if (!top->deque_cursor && top->last_deque) {
        if (parent) {
            parent->next = nullptr;
            top->deque_cursor = static_cast<int>(parent->buffer->size());
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
            : static_cast<ssize_t>(top->deque->buffer->size()));

        auto copy = std::min(size - rhs, buffer_size - top->deque_current);
        memcpy(static_cast<char *>(buffer) + rhs,
            top->deque->buffer->data() + top->deque_current, copy);

        rhs += copy;
        top->deque_current += static_cast<int>(copy);

        if (top->deque_current == buffer_size) {
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
