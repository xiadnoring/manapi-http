#include "components/SmartBuffer.hpp"

#include "ManapiUtils.hpp"

manapi::net::worker::smart_w_buffer::smart_w_buffer(const std::function<future<ssize_t> (void *, ssize_t size, bool flag)> &callback, size_t sent, ssize_t frame_size) {
    this->flag = false;
    this->sent.store(static_cast<ssize_t>(sent));
    this->callback = callback;
    this->frame_size = frame_size;
}

manapi::net::worker::smart_w_buffer::~smart_w_buffer() = default;

manapi::net::worker::smart_w_buffer::smart_w_buffer(smart_w_buffer &&n) noexcept {
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
    this->disabled = n.disabled;

    n.disabled = false;
    n.flag = false;
    n.sent = 0;
    n.maxsize = 0;
    n.frame_size = 0;
    return *this;
}

void manapi::net::worker::smart_w_buffer::resize(size_t size) {
    std::lock_guard<std::mutex> lk (gmx);
    this->buffer.reserve(size);
    this->maxsize = size;
}

void manapi::net::worker::smart_w_buffer::add_allow_to_sent(int size) {
    this->sent += size;
    this->cv.notify_all();
}

manapi::net::future<size_t> manapi::net::worker::smart_w_buffer::add(const void *c, size_t len, bool flag) {
    std::lock_guard<std::mutex> lk (gmx);
    size_t total_res = 0;
    size_t align = 0;
    do {
        size_t res = std::min (this->maxsize - this->buffer.size(), len);
        this->buffer.append(static_cast<const char *> (c) + align, res);
        align += res;
        len -= res;
        total_res += res;
        co_await _work(flag);
    } while (len > this->frame_size);
    co_return total_res;
}

void manapi::net::worker::smart_w_buffer::disable() {
    disabled = true;
    cv.notify_all();
}


manapi::net::future<ssize_t> manapi::net::worker::smart_w_buffer::_work(bool flag) {
    std::unique_lock<std::mutex> lk (mx);
    cv.wait(lk, [this] () -> bool {
        return this->sent > 0 || disabled;
    });
    if (disabled) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_CONNECTION_WAS_CLOSED, "Connection was closed"); }
    if (callback == nullptr) { THROW_MANAPIHTTP_EXCEPTION2(ERR_FUNCTION_IS_NULL, "class smart_w_buffer(...): Function was not set"); }

    ssize_t buff_size = std::min(static_cast<ssize_t>(buffer.size()), static_cast<ssize_t>(this->sent));
    auto ptr = buffer.data();
    auto end = buffer.data() + buffer.size();
    if (frame_size == 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_DIVIDED_BY_ZERO, "Why frame_size equals 0?"); }
    // frame_size only
    auto limit = ptr + (buff_size / frame_size) * frame_size;
    while (limit > ptr) {
        const bool last = end <= ptr + frame_size;
        auto rhs = co_await callback (ptr, frame_size, (flag && last));
        if (rhs <= 0) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "эм"); }
        ptr += frame_size;
    }

    if (flag && (ptr < end)) {
        // less than frame_size
        auto rhs = co_await callback (ptr, end - ptr, flag);
        ptr += rhs;
    }

    ssize_t total = ptr - buffer.data();
    this->sent -= total;
    try { buffer = buffer.substr( total); }
    catch (std::exception const &e) { MANAPIHTTP_LOG("тут ошибка: {}", e.what()); }

    co_return total;
}

manapi::net::worker::smart_r_buffer::smart_r_buffer(const std::function<manapi::net::future<void>(int)> &callback, int frame_size) {
    this->frame_size = frame_size;
    this->callback = callback;
    this->buffer.reserve(this->frame_size);
}

manapi::net::worker::smart_r_buffer::~smart_r_buffer() = default;

manapi::net::worker::smart_r_buffer::smart_r_buffer(smart_r_buffer &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::worker::smart_r_buffer & manapi::net::worker::smart_r_buffer::operator=(smart_r_buffer &&n) noexcept {
    std::lock_guard<std::mutex> lk1 (gmx);
    std::lock_guard<std::mutex> lk2 (n.gmx);

    this->buffer = std::move(n.buffer);
    this->callback = std::move(n.callback);
    this->frame_size = n.frame_size;
    this->disabled = n.disabled;

    n.frame_size = 0;
    n.disabled = false;
    return *this;
}

void manapi::net::worker::smart_r_buffer::resize(int frame_size) {
    std::lock_guard<std::mutex> lk (gmx);
    this->frame_size = frame_size;
    this->buffer.reserve(this->frame_size);
}

void manapi::net::worker::smart_r_buffer::add(const void *c, size_t len, bool flag) {
    std::lock_guard<std::mutex> lk (gmx);
    if (i >= this->buffer.size()) {
        i = 0;
        this->buffer.clear();
    }
    this->buffer.append(static_cast<const char *> (c), len);
    //auto size = this->buffer.size();
    cv.notify_all();
    //std::cout << "ура, мы получили данные " << len << "flag: " << flag << "\n";
}

manapi::net::future<ssize_t> manapi::net::worker::smart_r_buffer::read(void *c, size_t len) {
    std::unique_lock<std::mutex> lk1 (this->mx);
    this->cv.wait(lk1, [this] () -> bool {
        return this->buffer.size() > this->i || this->disabled;
    });
    if (this->disabled) {
        co_return -1;
    }
    std::lock_guard<std::mutex> lk2 (gmx);
    size_t size = static_cast<size_t> (std::min(this->buffer.size() - this->i, len));
    memcpy(c, this->buffer.data() + this->i, size);
    i += size;

    if (this->buffer.size() == this->i) {
        co_await this->callback (this->frame_size);
    }

    co_return static_cast<ssize_t>(size);
}

void manapi::net::worker::smart_r_buffer::disable() {
    std::lock_guard<std::mutex> lk (this->gmx);
    disabled = true;
    cv.notify_all();
}

