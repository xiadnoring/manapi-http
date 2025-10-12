#include "ManapiTimerObject.hpp"
#include "ManapiTimerPool.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"

static void destroy_timer_data_cb (int *flags, manapi::timer::timer_data_t::timer_data_cb_t *cb, bool renew = true) MANAPIHTTP_NOEXCEPT {
    try {
        if (*flags & manapi::TIMER_TASK_IS_ASYNC) {
            cb->async_cb.~move_only_function();
            *flags ^= manapi::TIMER_TASK_IS_ASYNC;
        }
        else {
            cb->sync_cb.~move_only_function();
        }

        if (renew)
            new (&cb->sync_cb) manapi::timer::sync_cb_t{nullptr};
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
}

static void run_sync_cb (std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    if (!(data->flags & manapi::TIMER_TASK_INTERVAL
        && data->flags & manapi::TIMER_TASK_ENABLED)) {
        data->flags ^= manapi::TIMER_TASK_ENABLED;
    }

    try {
        if (data->cb.sync_cb)
            data->cb.sync_cb(data);

    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "timer", "user sync callback", e.what());
    }

    if (data->flags & manapi::TIMER_TASK_INTERVAL) {
        manapi::async::current()->timerpool()->update_interval_state(std::move(data));
    }
}

static manapi::future<> run_async_cb (std::shared_ptr<manapi::timer::timer_data_t> data) {
    if (!(data->flags & manapi::TIMER_TASK_INTERVAL)
        && data->flags & manapi::TIMER_TASK_ENABLED) {
        data->flags ^= manapi::TIMER_TASK_ENABLED;
    }

    try {
        if (data->cb.async_cb)
            co_await data->cb.async_cb (data);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "timer", "user async callback", e.what());
    }

    if (data->flags & manapi::TIMER_TASK_INTERVAL) {
        manapi::async::current()->timerpool()->update_interval_state(data);
    }
}

manapi::timer::timer_data_t::timer_data_cb_t::~timer_data_cb_t() {}

manapi::timer::timer_data_t::~timer_data_t() {
    destroy_timer_data_cb(&this->flags, &this->cb, false);
}

manapi::timer::timer() {
    this->data = nullptr;
}

manapi::timer::timer(nullptr_t) {
    this->data = nullptr;
}

manapi::timer::timer(std::shared_ptr<timer_data_t> data) {
    this->data = std::move(data);
}

manapi::error::status_or<manapi::timer> manapi::timer::create (bool interval,bool important,manapi::timer::sync_cb_t sync_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = manapi::timer ();

        w.data = std::make_shared<timer_data_t>();

        if (interval) {
            w.data->flags |= TIMER_TASK_INTERVAL;
        }
        if (important) {
            w.data->flags |= TIMER_TASK_IMPORTANT;
        }
        w.data->flags |= TIMER_TASK_ENABLED;

        new (&w.data->cb.sync_cb) sync_cb_t (std::move(sync_cb));

        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::error::status_resource_exhausted();
    }
}

manapi::error::status_or<manapi::timer> manapi::timer::create(bool interval,bool important, async_cb_t async_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = manapi::timer ();

        w.data = std::make_shared<timer_data_t>();

        if (interval) {
            w.data->flags |= TIMER_TASK_INTERVAL;
        }
        if (important) {
            w.data->flags |= TIMER_TASK_IMPORTANT;
        }
        w.data->flags |= TIMER_TASK_ENABLED|TIMER_TASK_IS_ASYNC;

        new (&w.data->cb.async_cb) async_cb_t (std::move(async_cb));

        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::error::status_resource_exhausted();
    }

}

manapi::timer::timer(const timer &n) {
    this->data = n.data;
}

manapi::timer::timer(timer &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
}

manapi::timer & manapi::timer::operator=(timer &&n) MANAPIHTTP_NOEXCEPT {
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

manapi::timer::operator bool() const MANAPIHTTP_NOEXCEPT {
    return this->data != nullptr;
}

size_t manapi::timer::id() const MANAPIHTTP_NOEXCEPT {
    return manapi::timer::id_(this->data);
}

void manapi::timer::call_() MANAPIHTTP_NOEXCEPT {
    if (!(this->data->flags & TIMER_TASK_ENABLED)) {
        return;
    }

    try {
        if (this->data->flags & TIMER_TASK_IS_ASYNC) {
            manapi::async::run(run_async_cb(this->data));
        }
        else {
            run_sync_cb(this->data);
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due %s", "timer::call", e.what());
    }
}

void manapi::timer::clear() MANAPIHTTP_NOEXCEPT {
    this->clear_();
}

void manapi::timer::clear_() MANAPIHTTP_NOEXCEPT {
    if (!(this->data->flags & TIMER_TASK_ENABLED)) {
        destroy_timer_data_cb(&this->data->flags, &this->data->cb);
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

void manapi::timer::callback_async(async_cb_t cb) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->data)
            return;
        destroy_timer_data_cb(&this->data->flags, &this->data->cb, false);
        new (&this->data->cb.async_cb) async_cb_t (std::move(cb));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
}

void manapi::timer::callback_sync(sync_cb_t cb) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->data)
            return;
        destroy_timer_data_cb(&this->data->flags, &this->data->cb, false);
        new (&this->data->cb.sync_cb) sync_cb_t (std::move(cb));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
}

manapi::error::status manapi::timer::again(std::size_t ms) MANAPIHTTP_NOEXCEPT {
    this->data->flags |= TIMER_TASK_ENABLED;

    if (this->data->flags & TIMER_TASK_ACTIVE)
        manapi::async::current()->timerpool()->remove_timer(this->data);

    this->data->delay = std::chrono::milliseconds{ms};
    this->data->point = std::chrono::steady_clock::now() + this->data->delay;

    return manapi::async::current()->timerpool()->again_timer(this->data);
}

bool manapi::timer::is_async() const MANAPIHTTP_NOEXCEPT {
    return this->data->flags & TIMER_TASK_IS_ASYNC;
}

bool manapi::timer::is_sync() const MANAPIHTTP_NOEXCEPT {
    return !this->is_async();
}

bool manapi::timer::is_enabled() const MANAPIHTTP_NOEXCEPT {
    return (this->data->flags & TIMER_TASK_ENABLED);
}

bool manapi::timer::is_important() const MANAPIHTTP_NOEXCEPT {
    return (this->data->flags & TIMER_TASK_IMPORTANT);
}

std::shared_ptr<manapi::timer::timer_data_t> manapi::timer::data_() const MANAPIHTTP_NOEXCEPT {
    return this->data;
}


size_t manapi::timer::id_(const std::shared_ptr<timer_data_t> &data) {
    return reinterpret_cast<size_t>(data.get());
}





