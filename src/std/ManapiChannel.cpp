#include "std/ManapiChannel.hpp"
#include "std/ManapiAsyncPromise.hpp"

enum channel__flags {
    CHANNEL__FLAG_RECV_FINISHED = 1<<0,
    CHANNEL__FLAG_SEND_FINISHED = 1<<1,
    CHANNEL__FLAG_FINISHED = CHANNEL__FLAG_RECV_FINISHED|CHANNEL__FLAG_SEND_FINISHED
};

struct manapi::channel_data_t {
    int flags;
    manapi::slice sv;
    std::size_t max_size;

    manapi::async::promise_sync<manapi::status>::resolve_t resolve_cb;
};

std::shared_ptr<manapi::channel_data_t> manapi::create_channel(std::size_t max_size) {
    auto c = std::make_shared<manapi::channel_data_t>();
    c->max_size = max_size;

    return std::move(c);
}

manapi::channel_send::channel_send(const std::shared_ptr<channel_data_t>& data) : m_data(data) {
}

manapi::channel_send::~channel_send() {
    if (!!this->m_data)
        this->finish();
}

manapi::future<manapi::status> manapi::channel_send::send(manapi::slice &&z, bool fin) {
    if (this->m_data->flags & CHANNEL__FLAG_FINISHED) co_return manapi::status_aborted();
    this->m_data->sv.push_back(std::forward<decltype(z)>(z));
    if (fin) this->m_data->flags |= CHANNEL__FLAG_SEND_FINISHED;
    if (this->m_data->resolve_cb) {
        auto cb = std::move(this->m_data->resolve_cb);
        cb (manapi::status_ok());
    }
    if (this->m_data->sv.size() > this->m_data->max_size) {
        co_return co_await async::promise_sync<manapi::status> ([data = this->m_data.get()] (auto resolve) -> void {
            if (data->resolve_cb) resolve (manapi::status_ok());
            else if (data->flags & CHANNEL__FLAG_RECV_FINISHED) resolve (manapi::status_aborted());
            else data->resolve_cb = std::move(resolve);
        });
    }

    co_return manapi::status_ok();
}

bool manapi::channel_send::is_finished() const MANAPIHTTP_NOEXCEPT {
    return this->m_data->flags & CHANNEL__FLAG_FINISHED;
}

void manapi::channel_send::finish() MANAPIHTTP_NOEXCEPT {
    this->m_data->flags |= CHANNEL__FLAG_SEND_FINISHED;

    if (this->m_data->resolve_cb) {
        auto cb = std::move(this->m_data->resolve_cb);
        cb (manapi::status_aborted());
    }
}

manapi::channel_recv::channel_recv(const std::shared_ptr<channel_data_t> &data) : m_data(data) {
}

manapi::channel_recv::~channel_recv() {
    if (!!this->m_data)
        this->finish();
}

manapi::future<manapi::status_or<manapi::slice>> manapi::channel_recv::recv(ssize_t max_size) {
    std::size_t size;

    while (true) {
        size = this->m_data->sv.size();
        if (max_size >= 0) size = std::min<std::size_t> (static_cast<std::size_t>(max_size), size);
        if (!size) {
            if (this->m_data->flags & CHANNEL__FLAG_FINISHED)
                co_return manapi::status_aborted();

            auto z = co_await async::promise_sync<manapi::status> ([data = this->m_data.get()] (auto resolve) -> void {
                if (data->resolve_cb) resolve(manapi::status_ok());
                else if (data->flags & CHANNEL__FLAG_FINISHED) resolve(manapi::status_aborted());
                else data->resolve_cb = std::move(resolve);
            });

            if (!z) co_return std::move(z);

            continue;
        }

        break;
    }

    auto sv = this->m_data->sv.split (0, size);
    if (!sv) co_return sv.err();

    if (this->m_data->resolve_cb) {
        if (this->m_data->sv.size() > this->m_data->max_size) {
            auto cb = std::move(this->m_data->resolve_cb);
            cb (manapi::status_ok());
        }
    }

    co_return sv.unwrap();
}

bool manapi::channel_recv::is_finished() const MANAPIHTTP_NOEXCEPT {
    return this->m_data->flags & CHANNEL__FLAG_FINISHED;
}

void manapi::channel_recv::finish() MANAPIHTTP_NOEXCEPT {
    this->m_data->flags |= CHANNEL__FLAG_RECV_FINISHED;

    if (this->m_data->resolve_cb) {
        auto cb = std::move(this->m_data->resolve_cb);
        cb (manapi::status_aborted());
    }
}
