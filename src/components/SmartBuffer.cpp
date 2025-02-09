#include "components/SmartBuffer.hpp"

#include "ManapiUtils.hpp"

manapi::net::worker::smart_w_buffer::smart_w_buffer(std::shared_ptr<threadpool<task>> taskpool, write_cb callback, size_t sent, ssize_t buffer_size, ssize_t frame_size) : gmx(taskpool), cv(taskpool) {
    this->flag = false;
    this->sent.store(static_cast<ssize_t>(sent));
    this->callback = std::move(callback);
    this->taskpool = std::move(taskpool);
    this->buffer.resize(buffer_size);
    this->frame_size = frame_size;
}

manapi::net::worker::smart_w_buffer::~smart_w_buffer() = default;

manapi::net::worker::smart_w_buffer::smart_w_buffer(smart_w_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool) {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_w_buffer & manapi::net::worker::smart_w_buffer::operator=(smart_w_buffer &&n) noexcept {
    this->buffer = std::move(n.buffer);
    this->flag = std::exchange(n.flag, false);
    this->sent.store(n.sent.exchange(0));
    this->callback = std::move(n.callback);
    this->frame_size = std::exchange(n.frame_size, 0);
    this->disabled.store(n.disabled.exchange(false));
    this->taskpool = std::move(n.taskpool);
    this->buffer_cursor = std::exchange(n.buffer_cursor, 0);
    this->buffer_pos = std::exchange(n.buffer_pos, 0);

    return *this;
}

manapi::future<void> manapi::net::worker::smart_w_buffer::resize(ssize_t size) {
    auto lk = co_await this->gmx.lock_guard();
    this->buffer.resize(size);
}

manapi::future<void> manapi::net::worker::smart_w_buffer::add_allow_to_send(ssize_t size) {
    if (size < 0) {
        this->sent.store(std::numeric_limits<ssize_t>::max());
    }
    else {
        this->sent.fetch_add(size);
    }
    co_await this->cv.notify_all();
}

manapi::future<ssize_t> manapi::net::worker::smart_w_buffer::add(const void *c, ssize_t len, bool flag) {
    auto lk = co_await this->gmx.lock_guard();
    ssize_t total_res = 0;
    ssize_t align = 0;
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
    this->disabled.store(true);
    co_await this->cv.notify_all();
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

        auto prev_buffer_pos = this->buffer_pos;
        ssize_t buff_size = std::min(static_cast<ssize_t>(this->buffer_cursor - this->buffer_pos), this->sent.load());
        // buffer_size only
        auto limit = this->buffer_pos + (buff_size / this->frame_size) * this->frame_size;
        while (limit > this->buffer_pos) {
            const bool last = this->buffer_cursor == this->buffer_pos + this->frame_size;
            auto rhs = co_await this->callback (this->buffer.data() + this->buffer_pos, this->frame_size, (flag && last), this->disabled);
            if (rhs <= 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "эм"); }
            this->buffer_pos += rhs;
        }

        auto difference = static_cast<ssize_t>(this->buffer_pos - prev_buffer_pos);
        this->sent.fetch_sub(difference);

        if (this->sent > 0 && (this->buffer_pos < this->buffer_cursor)) {
            // less than frame_size
            auto pred = static_cast<ssize_t>(this->buffer_cursor - this->buffer_pos);
            auto rhs = co_await this->callback (this->buffer.data() + this->buffer_pos, std::min(this->sent.load(), pred), flag && pred <= this->sent.load(), this->disabled);
            this->buffer_pos += rhs;
            this->sent.fetch_sub(rhs);
        }

        total += difference;

        if (flag && this->buffer_pos < this->buffer_cursor) {
            continue;
        }

        if (this->buffer_pos==this->buffer_cursor) {
            this->buffer_pos=0;
            this->buffer_cursor=0;
        }

        co_return total;
    }
}

manapi::net::worker::smart_r_buffer::smart_r_buffer(std::shared_ptr<threadpool<task>> taskpool, read_cb callback, std::atomic<int> &want_read, int buffer_size) : want_read(want_read), gmx(taskpool), cv(taskpool) {
    this->callback = std::move(callback);
    this->buffer.resize(buffer_size);
    this->taskpool = std::move(taskpool);
    this->read_window = buffer_size;
}

manapi::net::worker::smart_r_buffer::~smart_r_buffer() = default;

manapi::net::worker::smart_r_buffer::smart_r_buffer(smart_r_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool), want_read(n.want_read) {
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

manapi::future<void> manapi::net::worker::smart_r_buffer::resize(ssize_t buffer_size) {
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
    if (!available_read ()) {
        this->want_read.fetch_add(1);
        co_await this->cv.wait([this] ()
            -> bool { return this->available_read(); });
        this->want_read.fetch_sub(1);
    }

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

bool manapi::net::worker::smart_r_buffer::available_read() {
    return this->buffer_pos != this->buffer_cursor || this->disabled;
}

