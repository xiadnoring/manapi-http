#include "ManapiTimerObject.hpp"
#include "ManapiTimerPool.hpp"
#include "std/ManapiContext.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"
#include "./include/ManapiAsyncInternal.hpp"

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

manapi::timer::timer_data_t::timer_data_cb_t::~timer_data_cb_t() {}

manapi::timer::timer_data_t::~timer_data_t() {
    destroy_timer_data_cb(&this->flags, &this->cb, false);
}

manapi::timer::timer() {
    this->m_data = nullptr;
}

manapi::timer::timer(nullptr_t) {
    this->m_data = nullptr;
}

manapi::timer::timer(std::shared_ptr<timer_data_t> data) {
    this->m_data = std::move(data);
}

manapi::status_or<manapi::timer> manapi::timer::create (bool interval,timer_types type,manapi::timer::sync_cb_t sync_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = manapi::timer ();

        w.m_data = std::make_shared<timer_data_t>();

        if (interval) {
            w.m_data->flags |= TIMER_TASK_INTERVAL;
        }
        if (type == TIMER_IMPORTANT) {
            w.m_data->flags |= TIMER_TASK_IMPORTANT;
        }
        else if (type == TIMER_POOR) {
            w.m_data->flags |= TIMER_TASK_POOR;
        }
        w.m_data->flags |= TIMER_TASK_ENABLED;

        new (&w.m_data->cb.sync_cb) sync_cb_t (std::move(sync_cb));

        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
}

manapi::status_or<manapi::timer> manapi::timer::create(bool interval,timer_types type, async_cb_t async_cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = manapi::timer ();

        w.m_data = std::make_shared<timer_data_t>();

        if (interval) {
            w.m_data->flags |= TIMER_TASK_INTERVAL;
        }
        if (type == TIMER_IMPORTANT) {
            w.m_data->flags |= TIMER_TASK_IMPORTANT;
        }
        else if (type == TIMER_POOR) {
            w.m_data->flags |= TIMER_TASK_POOR;
        }
        w.m_data->flags |= TIMER_TASK_ENABLED|TIMER_TASK_IS_ASYNC;

        new (&w.m_data->cb.async_cb) async_cb_t (std::move(async_cb));

        return std::move(w);
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }

}

manapi::timer::timer(const timer &n) {
    this->m_data = n.m_data;
}

manapi::timer::timer(timer &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
}

manapi::timer & manapi::timer::operator=(timer &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
    return *this;
}

manapi::timer & manapi::timer::operator=(const timer &n) {
    if (this != &n) {
        this->m_data = n.m_data;
    }
    return *this;
}

manapi::timer & manapi::timer::operator=(nullptr_t) {
    this->m_data = nullptr;
    return *this;
}

manapi::timer::~timer() = default;

manapi::timer::operator bool() const MANAPIHTTP_NOEXCEPT {
    return this->m_data != nullptr;
}

std::size_t manapi::timer::id() const MANAPIHTTP_NOEXCEPT {
    return reinterpret_cast <std::size_t> (this->m_data.get());
}

void manapi::internal::timer__call (const std::shared_ptr<manapi::timer::timer_data_t> &data) MANAPIHTTP_NOEXCEPT {
    if (!(data->flags & manapi::TIMER_TASK_ENABLED)) {
        return;
    }

    try {
        if (data->flags & TIMER_TASK_IS_ASYNC) {
            manapi::async::run([data] () -> manapi::future<> {
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

                if (data->flags & manapi::TIMER_TASK_IMPORTANT) {
                    manapi::internal::timer__unref_important();
                }

                if ((data->flags & manapi::TIMER_TASK_INTERVAL)
                    && (data->flags & manapi::TIMER_TASK_ENABLED)
                    && !(data->flags & manapi::TIMER_TASK_ACTIVE)) {
                    manapi::internal::timer__update_interval_state(data);
                }
            });
        }
        else {
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

            if ((data->flags & manapi::TIMER_TASK_INTERVAL)
                && (data->flags & manapi::TIMER_TASK_ENABLED)
                && !(data->flags & manapi::TIMER_TASK_ACTIVE)) {
                internal::timer__update_interval_state(data);
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due %s", "timer::call", e.what());
    }
}

void manapi::timer::clear() MANAPIHTTP_NOEXCEPT {
    if (!(this->m_data->flags & TIMER_TASK_ENABLED)) {
        destroy_timer_data_cb(&this->m_data->flags, &this->m_data->cb);
    }
}

void manapi::timer::stop() MANAPIHTTP_NOEXCEPT {
    if (!this->m_data)
        return;

    if (!(this->m_data->flags & TIMER_TASK_ENABLED)) {
        return;
    }

    this->m_data->flags ^= TIMER_TASK_ENABLED;

    manapi::async::current()->timerpool()->remove_timer(this->m_data);
}

void manapi::timer::callback_async(async_cb_t cb) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->m_data)
            return;
        destroy_timer_data_cb(&this->m_data->flags, &this->m_data->cb, false);
        new (&this->m_data->cb.async_cb) async_cb_t (std::move(cb));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
}

void manapi::timer::callback_sync(sync_cb_t cb) MANAPIHTTP_NOEXCEPT {
    try {
        if (!this->m_data)
            return;
        destroy_timer_data_cb(&this->m_data->flags, &this->m_data->cb, false);
        new (&this->m_data->cb.sync_cb) sync_cb_t (std::move(cb));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
}

manapi::status manapi::timer::again(std::size_t ms) MANAPIHTTP_NOEXCEPT {
    this->m_data->flags |= TIMER_TASK_ENABLED;

    if (this->m_data->flags & TIMER_TASK_ACTIVE)
        manapi::async::current()->timerpool()->remove_timer(this->m_data);

    this->m_data->delay = std::chrono::milliseconds{ms};
    this->m_data->point = std::chrono::steady_clock::now() + this->m_data->delay;

    return manapi::async::current()->timerpool()->again_timer(this->m_data);
}

std::chrono::milliseconds manapi::timer::interval() const MANAPIHTTP_NOEXCEPT {
    return this->m_data->delay;
}

std::chrono::milliseconds manapi::timer::remaning() const MANAPIHTTP_NOEXCEPT {
    auto const now = std::chrono::steady_clock::now();
    if (this->m_data->point < now) {
        return std::chrono::milliseconds(0);
    }
    auto const diff = std::chrono::duration_cast <std::chrono::milliseconds>(this->m_data->point-now);
    return std::chrono::milliseconds(diff.count());
}

bool manapi::timer::is_async() const MANAPIHTTP_NOEXCEPT {
    return this->m_data->flags & TIMER_TASK_IS_ASYNC;
}

bool manapi::timer::is_sync() const MANAPIHTTP_NOEXCEPT {
    return !this->is_async();
}

bool manapi::timer::is_enabled() const MANAPIHTTP_NOEXCEPT {
    return (this->m_data->flags & TIMER_TASK_ENABLED);
}

bool manapi::timer::is_important() const MANAPIHTTP_NOEXCEPT {
    return (this->m_data->flags & TIMER_TASK_IMPORTANT);
}

std::shared_ptr<manapi::timer::timer_data_t> manapi::timer::data() const MANAPIHTTP_NOEXCEPT {
    return this->m_data;
}




