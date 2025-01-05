#include "components/SmartBuffer.hpp"

#include "ManapiUtils.hpp"

manapi::net::worker::smart_w_buffer::smart_w_buffer(std::shared_ptr<threadpool<task>> taskpool, const std::function<future<ssize_t> (void *, ssize_t size, bool flag)> &callback, size_t sent, ssize_t frame_size) : gmx(taskpool), cv(taskpool) {
    this->flag = false;
    this->sent.store(static_cast<ssize_t>(sent));
    this->callback = callback;
    this->frame_size = frame_size;
    this->taskpool = std::move(taskpool);
}

manapi::net::worker::smart_w_buffer::~smart_w_buffer() = default;

manapi::net::worker::smart_w_buffer::smart_w_buffer(smart_w_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool) {
    this->flag = false;
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_w_buffer & manapi::net::worker::smart_w_buffer::operator=(smart_w_buffer &&n) noexcept {
    this->buffer = std::move(n.buffer);
    this->flag = n.flag;
    this->maxsize = n.maxsize;
    this->sent.store(n.sent);
    this->callback = std::move(n.callback);
    this->frame_size = n.frame_size;
    this->disabled.store(n.disabled);
    this->taskpool = std::move(n.taskpool);

    n.disabled.store(false);
    n.flag = false;
    n.sent = 0;
    n.maxsize = 0;
    n.frame_size = 0;
    return *this;
}

manapi::future<void> manapi::net::worker::smart_w_buffer::resize(size_t size) {
    auto lk = co_await this->gmx.lock_guard();
    this->buffer.reserve(size);
    this->maxsize = size;
}

manapi::future<void> manapi::net::worker::smart_w_buffer::add_allow_to_sent(int size) {
    this->sent.fetch_add(size);
    co_await this->cv.notify_all();
}

manapi::future<size_t> manapi::net::worker::smart_w_buffer::add(const void *c, size_t len, bool flag) {
    auto lk = co_await this->gmx.lock_guard();
    size_t total_res = 0;
    size_t align = 0;
    do {
        size_t res = std::min (this->maxsize - this->buffer.size(), len);
        this->buffer.append(static_cast<const char *> (c) + align, res);
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
    while (true) {
        co_await this->cv.wait([this] () -> bool {
            return this->sent > 0 || disabled;
        });

        if (disabled) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_CONNECTION_WAS_CLOSED, "Connection was closed");
        }
        if (callback == nullptr) { THROW_MANAPIHTTP_EXCEPTION2(ERR_FUNCTION_IS_NULL, "class smart_w_buffer(...): Function was not set"); }

        auto _available_sent = static_cast<ssize_t>(this->sent);
        ssize_t buff_size = std::min(static_cast<ssize_t>(buffer.size()), _available_sent);
        auto ptr = buffer.data();
        auto end = buffer.data() + buffer.size();
        if (frame_size == 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_DIVIDED_BY_ZERO, "Why frame_size equals 0?"); }
        // frame_size only
        auto limit = ptr + (buff_size / frame_size) * frame_size;
        while (limit > ptr) {
            const bool last = end <= ptr + frame_size;
            auto rhs = co_await callback (ptr, frame_size, (flag && last));
            if (rhs <= 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "эм"); }
            ptr += rhs;
            _available_sent -= rhs;
        }

        if (_available_sent > 0 && (ptr < end)) {
            // less than frame_size
            auto pred = static_cast<ssize_t>(end - ptr);
            auto rhs = co_await callback (ptr, std::min(_available_sent, pred), flag && pred <= _available_sent);
            ptr += rhs;
        }

        ssize_t total = ptr - buffer.data();
        this->sent.fetch_sub(total);


        try { buffer = buffer.substr( total); }
        catch (std::exception const &e) { MANAPIHTTP_LOG("тут ошибка: {}", e.what()); }

        if (flag && ptr < end) {
            continue;
        }

        co_return total;
    }
}

manapi::net::worker::smart_r_buffer::smart_r_buffer(std::shared_ptr<threadpool<task>> taskpool, const std::function<manapi::future<void>(int)> &callback, int frame_size) : gmx(taskpool), cv(taskpool) {
    this->frame_size = frame_size;
    this->callback = callback;
    this->buffer.reserve(this->frame_size);
    this->taskpool = std::move(taskpool);
}

manapi::net::worker::smart_r_buffer::~smart_r_buffer() = default;

manapi::net::worker::smart_r_buffer::smart_r_buffer(smart_r_buffer &&n) noexcept : gmx(n.taskpool), cv(n.taskpool) {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_r_buffer & manapi::net::worker::smart_r_buffer::operator=(smart_r_buffer &&n) noexcept {
    this->buffer = std::move(n.buffer);
    this->callback = std::move(n.callback);
    this->frame_size = n.frame_size;
    this->disabled.store(n.disabled);
    this->taskpool = std::move(n.taskpool);

    n.frame_size = 0;
    n.disabled.store(false);
    return *this;
}

manapi::future<void> manapi::net::worker::smart_r_buffer::resize(int frame_size) {
    auto lk = co_await this->gmx.lock_guard();
    this->frame_size = frame_size;
    this->buffer.reserve(this->frame_size);
}

manapi::future<void> manapi::net::worker::smart_r_buffer::add(const void *c, size_t len, bool flag) {
    {
        auto lk = co_await this->gmx.lock_guard();
        if (i >= this->buffer.size()) {
            i = 0;
            this->buffer.clear();
        }
        this->buffer.append(static_cast<const char *> (c), len);
        //auto size = this->buffer.size();
    }
    co_await cv.notify_all();
    //std::cout << "ура, мы получили данные " << len << "flag: " << flag << "\n";
}

manapi::future<ssize_t> manapi::net::worker::smart_r_buffer::read(void *c, size_t len) {
    co_await this->cv.wait([this] () -> bool {
        return this->buffer.size() > this->i || this->disabled;
    });
    if (this->disabled) {
        co_return -1;
    }
    auto lk = co_await this->gmx.lock_guard();
    size_t size = static_cast<size_t> (std::min(this->buffer.size() - this->i, len));
    memcpy(c, this->buffer.data() + this->i, size);
    i += size;

    if (this->buffer.size() == this->i) {
        lk.call();
        if(this->callback) {
            co_await this->callback (this->frame_size);
        }
    }

    co_return static_cast<ssize_t>(size);
}

manapi::future<void> manapi::net::worker::smart_r_buffer::disable() {
    auto lk = co_await this->gmx.lock_guard();
    disabled.store(true);
    co_await cv.notify_all();
}

