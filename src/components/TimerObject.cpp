#include "./components/TimerObject.hpp"
#include "async/ManapiAsyncContext.hpp"

manapi::timer::timer() {
    this->data = nullptr;
}

manapi::timer::timer(std::shared_ptr<timer_data_t> data) {
    this->data = std::move(data);
}

manapi::timer::timer(std::move_only_function<void(manapi::timer t)> sync_cb) {
    this->data = std::make_shared<timer_data_t>();
    this->data->sync_cb = [cb = std::move(sync_cb)] (const std::shared_ptr<timer_data_t> &data, const std::shared_ptr<event_loop> &eventloop, const std::shared_ptr<threadpool<task>> &taskpool) mutable
            -> void { cb(timer{data}); };
}

manapi::timer::timer(std::move_only_function<manapi::future<>(manapi::timer t)> async_cb) {
    this->data = std::make_shared<timer_data_t>();
    this->data->async_cb = [cb = std::move(async_cb)] (std::shared_ptr<timer_data_t> data, std::shared_ptr<event_loop> eventloop, std::shared_ptr<threadpool<task>> taskpool) mutable
        -> manapi::future<> {
        try {
            co_await cb (timer{data});
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("timer: User Callback Error: {}", e.what());
        }
        co_await eventloop->update_state_interval(manapi::timer::_id(data));
    };
}

manapi::timer::timer(const timer &n) {
    this->data = n.data;
}

manapi::timer::timer(timer &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::timer & manapi::timer::operator=(timer &&n) noexcept {
    this->data = std::move(n.data);
    return *this;
}

manapi::timer::~timer() = default;

size_t manapi::timer::id() const {
    return manapi::timer::_id(this->data);
}

void manapi::timer::_call(const std::shared_ptr<event_loop> &eventloop, const std::shared_ptr<threadpool<task>> &taskpool) {
    if (!this->data->enabled) {
        return;
    }

    if (this->data->sync_cb.has_value()) {
        try {
            this->data->sync_cb.value()(this->data, eventloop, taskpool);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("timer: User Callback Error: {}", e.what());
        }
    }

    if (this->data->async_cb.has_value()) {
        manapi::async::run (taskpool, this->data->async_cb.value()(this->data, eventloop, taskpool));
    }
}

void manapi::timer::_clear() {
    this->data->async_cb = {};
    this->data->sync_cb = {};
}

manapi::future<> manapi::timer::async_stop(const std::shared_ptr<manapi::event_loop> &events) {
    if (!this->data->enabled.exchange(false)) {
        co_return;
    }

    co_await events->remove_timer(this->id());
    this->_clear();
}

manapi::future<> manapi::timer::async_stop(const std::shared_ptr<manapi::async::context> &ctx) {
    return async_stop(ctx->eventloop());
}

void manapi::timer::sync_stop(const std::shared_ptr<manapi::timerpool> &timerpool) {
    if (!this->data->enabled.exchange(false)) {
        return;
    }

    timerpool->remove_timer(this->id());
    this->_clear();
}

void manapi::timer::sync_stop(const std::shared_ptr<manapi::async::context> &ctx) {
    return this->sync_stop(ctx->timerpool());
}

bool manapi::timer::is_async() const {
    return this->data->async_cb.has_value();
}

bool manapi::timer::is_sync() const {
    return this->data->sync_cb.has_value();
}

bool manapi::timer::enabled() const {
    return this->data->enabled.load();
}


size_t manapi::timer::_id(const std::shared_ptr<timer_data_t> &data) {
    return reinterpret_cast<size_t>(data.get());
}





