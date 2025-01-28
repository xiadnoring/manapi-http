#include "components/SmartBuffer.hpp"

#include "ManapiUtils.hpp"

manapi::net::worker::smart_w_buffer::smart_w_buffer(std::shared_ptr<threadpool<task>> taskpool, const std::function<future<ssize_t> (void *, ssize_t size, bool flag)> &callback, size_t sent, ssize_t buffer_size) : gmx(taskpool), cv(taskpool) {
    this->flag = false;
    this->sent.store(static_cast<ssize_t>(sent));
    this->callback = callback;
    this->buffer_size = buffer_size;
    this->taskpool = std::move(taskpool);
}

manapi::net::worker::smart_w_buffer::~smart_w_buffer() = default;

manapi::net::worker::smart_w_buffer::smart_w_buffer(smart_w_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool) {
    this->flag = false;
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_w_buffer & manapi::net::worker::smart_w_buffer::operator=(smart_w_buffer &&n) noexcept {
    this->buffer = std::move(n.buffer);
    this->flag = std::exchange(n.flag, false);
    this->sent.store(n.sent.exchange(0));
    this->callback = std::move(n.callback);
    this->buffer_size = std::exchange(n.buffer_size, 0);
    this->disabled.store(n.disabled.exchange(false));
    this->taskpool = std::move(n.taskpool);
    this->buffer_cursor = std::exchange(n.buffer_cursor, 0);
    this->buffer_pos = std::exchange(n.buffer_pos, 0);

    return *this;
}

manapi::future<void> manapi::net::worker::smart_w_buffer::resize(size_t size) {
    auto lk = co_await this->gmx.lock_guard();
    this->buffer.resize(size);
}

manapi::future<void> manapi::net::worker::smart_w_buffer::add_allow_to_sent(int size) {
    this->sent.fetch_add(size);
    co_await this->cv.notify_all();
}

manapi::future<size_t> manapi::net::worker::smart_w_buffer::add(const void *c, ssize_t len, bool flag) {
    auto lk = co_await this->gmx.lock_guard();
    size_t total_res = 0;
    size_t align = 0;
    do {
        auto res = std::min (static_cast<ssize_t>(this->buffer.size() - this->buffer_cursor), len);
        memcpy(this->buffer.data() + this->buffer_cursor, static_cast<const char *>(c) + align, res);
        this->buffer_cursor += res;

        align += res;
        len -= res;
        total_res += res;
        co_await _work(flag && (len == 0));
    } while (len > 0);

    co_return total_res;
}

manapi::future<void> manapi::net::worker::smart_w_buffer::disable() {
    disabled.store(true);
    co_await cv.notify_all();
}


manapi::future<ssize_t> manapi::net::worker::smart_w_buffer::_work(bool flag) {
    ssize_t total = 0;
    while (true) {
        co_await this->cv.wait([this] () -> bool {
            return this->sent > 0 || this->disabled;
        });

        if (this->disabled) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_CONNECTION_WAS_CLOSED, "Connection was closed");
        }
        if (this->callback == nullptr) { THROW_MANAPIHTTP_EXCEPTION2(ERR_FUNCTION_IS_NULL, "class smart_w_buffer(...): Function was not set"); }

        auto _available_sent = static_cast<ssize_t>(this->sent);
        auto prev_buffer_pos = this->buffer_pos;
        ssize_t buff_size = std::min(static_cast<ssize_t>(this->buffer_cursor - this->buffer_pos), _available_sent);
        if (this->buffer_size == 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_DIVIDED_BY_ZERO, "Why buffer_size equals 0?"); }
        // buffer_size only
        auto limit = this->buffer_pos + (buff_size / this->buffer_size) * this->buffer_size;
        while (limit > this->buffer_pos) {
            const bool last = this->buffer_cursor <= this->buffer_pos + this->buffer_size;
            auto rhs = co_await this->callback (this->buffer.data() + this->buffer_pos, this->buffer_size, (flag && last));
            if (rhs <= 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "эм"); }
            this->buffer_pos += rhs;
            _available_sent -= rhs;
        }

        if (_available_sent > 0 && (this->buffer_pos < this->buffer_cursor)) {
            // less than buffer_size
            auto pred = static_cast<ssize_t>(this->buffer_cursor - this->buffer_pos);
            auto rhs = co_await this->callback (this->buffer.data() + this->buffer_pos, std::min(_available_sent, pred), flag && pred <= _available_sent);
            this->buffer_pos += rhs;
        }

        auto difference = static_cast<ssize_t>(this->buffer_pos - prev_buffer_pos);
        this->sent.fetch_sub(difference);
        total += difference;

        if (flag && this->buffer_pos < this->buffer_cursor) {
            continue;
        }

        this->buffer_pos = 0;
        this->buffer_cursor = 0;

        co_return total;
    }
}

manapi::net::worker::smart_r_buffer::smart_r_buffer(std::shared_ptr<threadpool<task>> taskpool, const std::function<manapi::future<void>(int)> &callback, int buffer_size) : gmx(taskpool), cv(taskpool) {
    this->callback = callback;
    this->buffer.resize(buffer_size);
    this->taskpool = std::move(taskpool);
    this->read_window = buffer_size;
}

manapi::net::worker::smart_r_buffer::~smart_r_buffer() = default;

manapi::net::worker::smart_r_buffer::smart_r_buffer(smart_r_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool) {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_r_buffer & manapi::net::worker::smart_r_buffer::operator=(smart_r_buffer &&n) noexcept {
    this->buffer = std::move(n.buffer);
    this->callback = std::move(n.callback);
    this->disabled.store(n.disabled.exchange(false));
    this->taskpool = std::move(n.taskpool);
    this->buffer_pos = std::exchange(n.buffer_pos, 0);
    this->buffer_cursor = std::exchange(n.buffer_cursor, 0);
    this->read_window = std::exchange(n.read_window, 0);

    return *this;
}

manapi::future<void> manapi::net::worker::smart_r_buffer::resize(int buffer_size) {
    auto lk = co_await this->gmx.lock_guard();
    this->buffer.resize(buffer_size);
}

manapi::future<ssize_t> manapi::net::worker::smart_r_buffer::add(const void *c, ssize_t len, bool flag) {

    auto lk = co_await this->gmx.lock_guard();
    if (this->buffer_pos == this->buffer_cursor) {
        this->buffer_pos = 0;
        this->buffer_cursor = 0;
    }
    auto res = std::min(len, static_cast<ssize_t>(this->buffer.size() - this->buffer_cursor));
    if (res != len) {
        co_return -1;
    }
    memcpy(this->buffer.data() + this->buffer_cursor, c, res);
    this->buffer_cursor += res;
    this->read_window -= static_cast<int>(res);

    lk.call();
    //std::cout << "wait " << len << "flag: " << flag << "\n";
    co_await this->cv.notify_all();
    //std::cout << "ура, мы получили данные " << len << "flag: " << flag << "\n";
    co_return len;
}

manapi::future<ssize_t> manapi::net::worker::smart_r_buffer::read(void *c, ssize_t len) {
    co_await this->cv.wait([this] () -> bool {
        return this->buffer_pos != this->buffer_cursor || this->disabled;
    });

    if (this->disabled) {
        co_return -1;
    }

    auto lk = co_await this->gmx.lock_guard();
    auto size = static_cast<size_t> (std::min(static_cast<ssize_t>(this->buffer_cursor - this->buffer_pos), len));
    memcpy(c, this->buffer.data() + this->buffer_pos, size);
    this->buffer_pos += size;

    if (this->buffer_pos == this->buffer_cursor) {
        this->buffer_pos=0;
        this->buffer_cursor=0;
        lk.call();
        if(this->callback && this->read_window < this->buffer.size()) {
            int fetchadd = static_cast<int>(this->buffer.size()) - this->read_window;
            this->read_window += fetchadd;
            co_await this->callback (fetchadd);
        }
    }

    co_return static_cast<ssize_t>(size);
}

manapi::future<void> manapi::net::worker::smart_r_buffer::disable() {
    auto lk = co_await this->gmx.lock_guard();
    this->disabled.store(true);
    co_await this->cv.notify_all();
}

