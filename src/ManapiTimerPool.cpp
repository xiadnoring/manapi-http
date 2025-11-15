#include <thread>
#include <limits>

#include "ManapiTimerPool.hpp"
#include "ManapiEventStructures.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiEventLoop.hpp"
#include "./include/ManapiUtils.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"

enum timerpool_flags {
    TIMERPOOL_FLAG_ACTIVE = 1,
    TIMERPOOL_FLAG_RUNNING = 2
};

struct manapi::timerpool::data_t {
    sorted_storage sorted_tasks;
    std::shared_ptr<event_loop> events{nullptr};
    int flags;
    std::shared_ptr<ev::timer> timer;
    std::size_t importants;
};

bool manapi::timerpool::sorted_tasks_compare_t::operator()(const sorted_storage_key &a, const sorted_storage_key &b) const MANAPIHTTP_NOEXCEPT {
    return a.first < b.first;
}

manapi::timerpool::timerpool() {

}

manapi::error::status_or<manapi::timerpool> manapi::timerpool::create(std::shared_ptr<event_loop> events) MANAPIHTTP_NOEXCEPT {
    try {
        timerpool d;
        d.data_ = std::make_shared<data_t>(sorted_storage(), std::move(events), 0, nullptr);
        return std::move(d);
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return error::status_resource_exhausted();
    }
}

manapi::timerpool::~timerpool() {
    this->stop();
}

manapi::timerpool::timerpool(timerpool &&n) MANAPIHTTP_NOEXCEPT {
    this->data_ = std::move(n.data_);
}

manapi::timerpool & manapi::timerpool::operator=(timerpool &&n) MANAPIHTTP_NOEXCEPT {
    this->data_ = std::move(n.data_);
    return *this;
}

manapi::timerpool::timerpool(const timerpool &n) {
    this->data_ = n.data_;
}

manapi::timerpool & manapi::timerpool::operator=(const timerpool &n) {
    if (this != &n) {
        this->data_ = n.data_;
    }
    return *this;
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_timer_sync(size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_timer_sync(ms, TIMER_DEFAULT, std::move(task));
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_timer_sync(size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), false, type);
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_timer_async(size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_timer_async(ms, TIMER_DEFAULT, std::move(task));
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_timer_async(size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, false, type);
}

void manapi::timerpool::remove_timer(std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    auto const it = this->data_->sorted_tasks.find(decltype(this->data_->sorted_tasks)::key_type{data->point, std::move(data)});
    if (it != this->data_->sorted_tasks.end()) {
        manapi::timerpool::erase_task_(this->data_, it);
    }
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_interval_async(size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_interval_async(ms, TIMER_DEFAULT, std::move(task));
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_interval_sync(size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_interval_sync(ms, TIMER_DEFAULT, std::move(task));
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_interval_async(size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, true, type);
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_interval_sync(size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), true, type);
}

manapi::error::status manapi::timerpool::update_interval_state(std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    return manapi::timerpool::update_interval_state_(this->data_, std::move(data));
}

manapi::error::status manapi::timerpool::again_timer(std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    try {
        auto const point = data->point;
        data->flags |= TIMER_TASK_ACTIVE;
        auto const res = this->data_->sorted_tasks.insert({point, std::move(data)});

        if (!res.second)
            return error::status_internal("timerpool:insert failed");


        if (res.first->second->flags & TIMER_TASK_IMPORTANT) {
            this->data_->importants++;
            if (!this->data_->timer) {
                if (manapi::async::context_exists()) {
                    auto result = this->init_timer_();
                    if (!result) {
                        return manapi::error::status_internal(result.msg());
                    }
                }
            }
        }

        if (res.first == this->data_->sorted_tasks.begin())
            reinit_timer_(this->data_);

        return error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return error::status_internal();
}

manapi::sys_error::status manapi::timerpool::start() MANAPIHTTP_NOEXCEPT {
    if ((this->data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
        return manapi::error::status_ok();
    }

    try {
        auto result = this->init_timer_();
        if (!result) {
            return std::move(result);
        }

        reinit_timer_(this->data_);

        this->data_->flags |= TIMERPOOL_FLAG_ACTIVE;

        return manapi::error::status_ok();
    }
    catch (std::exception const &) {
        return manapi::sys_error::status_resource_exhausted();
    }
}

void manapi::timerpool::stop () MANAPIHTTP_NOEXCEPT {
    if (this->data_)
        return manapi::timerpool::stop_(this->data_, false);
}

void manapi::timerpool::stop_(std::shared_ptr<data_t> data, bool evloop) MANAPIHTTP_NOEXCEPT {
    if (data->flags & TIMERPOOL_FLAG_ACTIVE) {
        data->flags ^= TIMERPOOL_FLAG_ACTIVE;
    }
    if (!data->importants) {
        if (data->timer) {
            assert(!data->timer->stop());
            int const res = ::uv_loop_alive(manapi::async::current()->eventloop()->loop());
            assert(!data->timer->start(0, 1));
            if (!res) {
                data->events->stop_watcher(std::move(data->timer));
            }
        }
    }
}

void manapi::timerpool::run_once() MANAPIHTTP_NOEXCEPT {
    if (this->data_)
        timerpool::start_(this->data_);
}


void manapi::timerpool::erase_task_(const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) MANAPIHTTP_NOEXCEPT {
    if (!data_->sorted_tasks.empty()) {
        if (sorted_task->second->flags & TIMER_TASK_IMPORTANT) {
            data_->importants--;
        }

        if (data_->sorted_tasks.begin() == sorted_task) {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
            reinit_timer_(data_);
        }
        else {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
        }


        if (!data_->importants && !(data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
            //data_->events->stop_watcher(std::move(data_->timer));
            stop_(data_, false);
        }
    }
}

void manapi::timerpool::start_(const std::shared_ptr<data_t> &data) MANAPIHTTP_NOEXCEPT {
    // if (data->flags & TIMERPOOL_FLAG_RUNNING) {
    //     /* it is already running */
    //     return;
    // }

    auto now = std::chrono::steady_clock::now();
    data->flags |= TIMERPOOL_FLAG_RUNNING;

    const bool active = data->flags & TIMERPOOL_FLAG_ACTIVE;

    while (!data->sorted_tasks.empty()) {
        auto sorted_task = data->sorted_tasks.begin();

        if (now < sorted_task->first) {
            break;
        }

        if (sorted_task->second->flags & TIMER_TASK_ACTIVE) {
            auto exdata = data->sorted_tasks.extract(sorted_task);
            auto &task = exdata.value().second;
            const bool important = task->flags & TIMER_TASK_IMPORTANT;

            manapi::timer timertask  (task);

            task->flags ^= TIMER_TASK_ACTIVE;
            if (important) {
                data->importants--;
                if (task->flags & TIMER_TASK_INTERVAL) {
                    timertask.call_();
                }
                else {
                    timertask.call_();
                }
            }
            else if (active || !(task->flags & TIMER_TASK_POOR)) {
                if (task->flags & TIMER_TASK_INTERVAL) {
                    timertask.call_();
                }
                else {
                    timertask.call_();
                }
            }
        }
    }

    if (!data->importants && !active) {
        //data->events->stop_watcher(std::move(data->timer));
        stop_(data, false);
    }

    if (data->flags & TIMERPOOL_FLAG_RUNNING)
        data->flags ^= TIMERPOOL_FLAG_RUNNING;

    reinit_timer_(data);
}

void manapi::timerpool::flush_stack_free(const std::shared_ptr<data_t> &data_) {

}

int64_t manapi::timerpool::calculate_repeat_(const std::shared_ptr<data_t> &data_) {
    if (data_->sorted_tasks.empty()) {
        return -1;
    }

    auto know = std::chrono::steady_clock::now();
    auto pnt = data_->sorted_tasks.begin()->first;

    if (pnt <= know) {
        return 0;
    }

    return static_cast<int64_t>((std::chrono::duration_cast<std::chrono::milliseconds>(pnt - know)).count());
}

bool manapi::timerpool::reinit_timer_(const std::shared_ptr<data_t> &data_) MANAPIHTTP_NOEXCEPT {
    if (data_->timer) {
        data_->timer->stop();

        auto delay = calculate_repeat_(data_);
        //manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "reinit_timer:delay=%llu now=%llu", delay, std::chrono::steady_clock::now().time_since_epoch());

        if (delay >= 0) {
            if (data_->timer->is_active()) {
                data_->timer->start(delay, 1);
                if (data_->timer->again()) {
                    manapi_log_error("set the timerpool again failed");
                    return false;
                }
            }
            else {
                data_->timer->start(delay, 1);
            }
        }
    }

    return true;
}

manapi::sys_error::status manapi::timerpool::init_timer_() MANAPIHTTP_NOEXCEPT {
    auto timer_res = this->data_->events->create_watcher_timer(
        [data = this->data_] (const std::shared_ptr<manapi::ev::timer> &w)
        -> void {
        timerpool::start_(data);
    });

    if (!timer_res)
        return timer_res.err();

    this->data_->timer = timer_res.unwrap();
    if (auto rhs = this->data_->timer->start(0, 1)) {
        this->data_->timer = nullptr;
        return manapi::sys_error::status_internal("timer::start", rhs);
    }
    return manapi::sys_error::status_ok();
}

void manapi::timerpool::clear() MANAPIHTTP_NOEXCEPT {
    if (!(this->data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
        this->data_->sorted_tasks.clear();
        this->data_->importants = 0;
        manapi::timerpool::reinit_timer_(this->data_);
    }
}

std::shared_ptr<manapi::threadpool> manapi::timerpool::taskpool() const MANAPIHTTP_NOEXCEPT {
    return this->data_->events->taskpool();
}

manapi::error::status manapi::timerpool::update_interval_state_(const std::shared_ptr<data_t> &data_, std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    try {
        data->flags |= TIMER_TASK_ACTIVE;
        data->point = std::chrono::steady_clock::now() + data->delay;

        auto res = data_->sorted_tasks.insert({data->point, std::move(data)});
        if (!res.second) {
            return error::status_already_exists("timerpool:insert failed");
        }

        if (res.first->second->flags & TIMER_TASK_IMPORTANT) {
            data_->importants++;
        }

        if (data_->sorted_tasks.begin()->second == data) {
            reinit_timer_(data_);
        }

        return error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return error::status_internal();
}

manapi::error::status_or<manapi::timer> manapi::timerpool::append_(std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task, bool interval, timer_types type) MANAPIHTTP_NOEXCEPT {
    try {
        manapi::timer timertask{};

        if (task) {
            auto res = manapi::timer::create(interval, type, (std::move(task)));
            if (!res)
                return std::move(res);
            timertask = res.unwrap();
        }

        if (async_task) {
            auto res = manapi::timer::create(interval, type, (std::move(async_task)));
            if (!res)
                return std::move(res);
            timertask = res.unwrap();
        }

        auto data = timertask.data_();

        data->delay = duration;
        data->point = std::chrono::steady_clock::now() + data->delay;

        auto res = this->again_timer(std::move(data));
        if (!res)
            return std::move(res);

        return std::move(timertask);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return error::status_internal();
}