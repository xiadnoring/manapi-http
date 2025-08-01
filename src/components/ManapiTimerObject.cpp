#include "./components/ManapiTimerObject.hpp"
#include "async/ManapiAsyncContext.hpp"

manapi::timer::timer() {
    this->data = nullptr;
}

manapi::timer::timer(nullptr_t) {
    this->data = nullptr;
}

manapi::timer::timer(std::shared_ptr<timer_data_t> data) {
    this->data = std::move(data);
}

manapi::timer::timer(bool interval,manapi::timer::sync_cb_t sync_cb) {
    this->data = std::make_shared<timer_data_t>();
    if (interval) {
        this->data->flags |= TIMER_TASK_INTERVAL;
    }
    this->data->flags |= TIMER_TASK_ENABLED;
    this->data->sync_cb = std::make_unique<decltype(this->data->sync_cb)::element_type>([cb = std::move(sync_cb)] (manapi::timer data) mutable
            -> void {
        if (!(data.data->flags & TIMER_TASK_INTERVAL
            && data.data->flags & TIMER_TASK_ENABLED)) {
            data.data->flags ^= TIMER_TASK_ENABLED;
        }

        try {
            cb(data);
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->debug(manapi::logger::default_service, "timer: cb failed due to: {}", e.what());
        }

        if (data.data->flags & TIMER_TASK_INTERVAL) {
            manapi::async::current()->timerpool()->update_interval_state(data.data);
        }
    });
}

manapi::timer::timer(bool interval,async_cb_t async_cb) {
    this->data = std::make_shared<timer_data_t>();
    if (interval) {
        this->data->flags |= TIMER_TASK_INTERVAL;
    }
    this->data->flags |= TIMER_TASK_ENABLED;
    this->data->async_cb = std::make_unique<decltype(this->data->async_cb)::element_type>([cb = std::move(async_cb)] (manapi::timer data) mutable
        -> manapi::future<> {
        if (!(data.data->flags & TIMER_TASK_INTERVAL)
            && data.data->flags & TIMER_TASK_ENABLED) {
            data.data->flags ^= TIMER_TASK_ENABLED;
        }

        try {
            co_await cb (timer{data});
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->debug(manapi::logger::default_service, "timer: cb failed due to: {}", e.what());
        }
        if (data.data->flags & TIMER_TASK_INTERVAL) {
            manapi::async::current()->timerpool()->update_interval_state(data.data);
        }
    });
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

manapi::timer & manapi::timer::operator=(const timer &n) {
    this->data = n.data;
    return *this;
}

manapi::timer & manapi::timer::operator=(nullptr_t) {
    this->data = nullptr;
    return *this;
}

manapi::timer::~timer() = default;

manapi::timer::operator bool() const {
    return this->data != nullptr;
}

size_t manapi::timer::id() const {
    return manapi::timer::id_(this->data);
}

void manapi::timer::call_() {
    if (!(this->data->flags & TIMER_TASK_ENABLED)) {
        return;
    }

    if (this->data->sync_cb) {
        try {
            this->data->sync_cb->operator()(*this);
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->debug(manapi::logger::default_service, "timer: User Callback Error: {}", e.what());
        }
    }

    if (this->data->async_cb) {
        manapi::async::run (this->data->async_cb->operator()(*this));
    }
}

void manapi::timer::clear() MANAPIHTTP_NOEXCEPT {
    this->clear_();
}

void manapi::timer::clear_() MANAPIHTTP_NOEXCEPT {
    if (!(this->data->flags & TIMER_TASK_ENABLED)) {
        this->data->async_cb = {};
        this->data->sync_cb = {};
    }
}

void manapi::timer::stop() MANAPIHTTP_NOEXCEPT {
    if (!this->data)
        return;

    if (!(this->data->flags & TIMER_TASK_ENABLED)) {
        return;
    }

    this->data->flags ^= TIMER_TASK_ENABLED;

    manapi::async::current()->timerpool()->remove_timer(this->data);
}

void manapi::timer::callback_async(async_cb_t cb) {
    if (!this->data->async_cb) {
        this->data->async_cb = std::make_unique<async_cb_t>(std::move(cb));
    }
    else {
        *this->data->async_cb = std::move(cb);
    }

    this->data->sync_cb.reset();
}

void manapi::timer::callback_sync(sync_cb_t cb) {
    if (!this->data->sync_cb) {
        this->data->sync_cb = std::make_unique<sync_cb_t>(std::move(cb));
    }
    else {
        *this->data->sync_cb = std::move(cb);
    }

    this->data->async_cb.reset();
}

manapi::error::status manapi::timer::again(std::size_t ms) MANAPIHTTP_NOEXCEPT {
    this->data->flags |= TIMER_TASK_ENABLED;

    if (this->data->flags & TIMER_TASK_ACTIVE)
        manapi::async::current()->timerpool()->remove_timer(this->data);

    this->data->delay = std::chrono::milliseconds{ms};
    this->data->point = std::chrono::steady_clock::now() + this->data->delay;

    return manapi::async::current()->timerpool()->again_timer(this->data);
}

bool manapi::timer::is_async() const {
    return !!this->data->async_cb;
}

bool manapi::timer::is_sync() const {
    return !!this->data->sync_cb;
}

bool manapi::timer::enabled() const {
    return (this->data->flags & TIMER_TASK_ENABLED);
}

std::shared_ptr<manapi::timer::timer_data_t> manapi::timer::data_() const {
    return this->data;
}


size_t manapi::timer::id_(const std::shared_ptr<timer_data_t> &data) {
    return reinterpret_cast<size_t>(data.get());
}





