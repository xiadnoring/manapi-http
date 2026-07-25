#include <thread>
#include <limits>

#include "ManapiTimerPool.hpp"
#include "ManapiEventStructures.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiEventLoop.hpp"
#include "./include/ManapiUtils.hpp"
#include "./include/ManapiEventStructuresInternal.hpp"
#include "./include/ManapiAsyncInternal.hpp"

enum timerpool_flags {
    TIMERPOOL_FLAG_ACTIVE = 1,
    TIMERPOOL_FLAG_RUNNING = 2
};

struct manapi::timerpool::data_t {
    sorted_storage sorted_tasks;
    int flags;
    std::shared_ptr<ev::timer> timer;
    std::size_t importants;
};

bool manapi::timerpool::sorted_tasks_compare_t::operator()(const sorted_storage_key &a, const sorted_storage_key &b) const MANAPIHTTP_NOEXCEPT {
    return a.first < b.first;
}

static manapi::status_or<manapi::timer> timerpool__append(manapi::timerpool *tpool, std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task, bool interval, manapi::timer_types type) MANAPIHTTP_NOEXCEPT {
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

        auto data = timertask.data();

        data->delay = duration;
        data->point = std::chrono::steady_clock::now() + data->delay;

        auto res = tpool->again_timer(std::move(data));
        if (!res)
            return std::move(res);

        return std::move(timertask);
    }
    catch (std::bad_alloc const &) {
        return manapi::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return manapi::status_internal();
}

static int64_t timerpool_calculate_repeat(const std::shared_ptr<manapi::timerpool::data_t> &data_) {
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

static bool timerpool__reinit_timer(const std::shared_ptr<manapi::timerpool::data_t> &data_) MANAPIHTTP_NOEXCEPT {
    if (data_->timer) {
        auto const delay = ::timerpool_calculate_repeat(data_);

        if (data_->timer->is_active()) {
            data_->timer->stop();
        }

        if (delay >= 0) {
            data_->timer->start(static_cast<uint64_t>(delay), 1);
        }
    }

    return true;
}

static void timerpool__stop(std::shared_ptr<manapi::timerpool::data_t> data) MANAPIHTTP_NOEXCEPT {
    if (data->flags & TIMERPOOL_FLAG_ACTIVE) {
        data->flags ^= TIMERPOOL_FLAG_ACTIVE;
    }
    if (!data->importants) {
        if (data->timer) {
            //::uv_print_all_handles(manapi::async::current()->eventloop()->loop(), stdout);
            //int const res1 = ::uv_loop_alive(manapi::async::current()->eventloop()->loop());
            //manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "timerpool:uv_loop_alive1 returned %d", res1);
            //assert(!data->timer->stop());
            ::uv_unref((uv_handle_t *)data->timer.get());
            int const res = ::uv_loop_alive(manapi::async::current()->eventloop()->loop());
            //::uv_print_all_handles(manapi::async::current()->eventloop()->loop(), stdout);
            //assert(!data->timer->start(0, 1));
            ::uv_ref ((uv_handle_t *)data->timer.get());
            //manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "timerpool:uv_loop_alive returned %d", res);
            if (!res) {
                manapi::async::eventloop()->stop_watcher(std::move(data->timer));
            }
        }
        else {
            manapi_log_trace(manapi::debug::LOG_TRACE_HARD, "timerpool:data->timer is NULL");
        }
    }
    else {
        manapi_log_trace(manapi::debug::LOG_TRACE_HARD, "timerpool:data->importants is %d", data->importants);
    }
}


static void timerpool__start(const std::shared_ptr<manapi::timerpool::data_t> &data) MANAPIHTTP_NOEXCEPT {
    if (data->flags & TIMERPOOL_FLAG_RUNNING) {
        /* it is already running */
        return;
    }

    auto now = std::chrono::steady_clock::now();
    data->flags |= TIMERPOOL_FLAG_RUNNING;

    const bool active = data->flags & TIMERPOOL_FLAG_ACTIVE;

    while (!data->sorted_tasks.empty()) {
        auto sorted_task = data->sorted_tasks.begin();

        if (now < sorted_task->first) {
            break;
        }

        if (sorted_task->second->flags & manapi::TIMER_TASK_ACTIVE) {
            auto exdata = data->sorted_tasks.extract(sorted_task);
            auto &task = exdata.value().second;
            const bool important = task->flags & manapi::TIMER_TASK_IMPORTANT;

            task->flags ^= manapi::TIMER_TASK_ACTIVE;
            if (important) {
                if (!(task->flags & manapi::TIMER_TASK_IS_ASYNC)) {
                    data->importants--;
                }

                manapi::internal::timer__call(task);
            }
            else if (active || !(task->flags & manapi::TIMER_TASK_POOR)) {
                manapi::internal::timer__call(task);
            }
        }
    }

    if (!data->importants && !active) {
        //data->events->stop_watcher(std::move(data->timer));
        ::timerpool__stop(data);
    }

    if (data->flags & TIMERPOOL_FLAG_RUNNING)
        data->flags ^= TIMERPOOL_FLAG_RUNNING;

    ::timerpool__reinit_timer(data);
}

static manapi::ev::status timerpool__init_timer(const std::shared_ptr<manapi::timerpool::data_t> &data) MANAPIHTTP_NOEXCEPT {
    if (!data->timer) {
        auto timer_res = manapi::async::eventloop()->create_watcher_timer(
            [data] (const std::shared_ptr<manapi::ev::timer> &w)
            -> void {
            ::timerpool__start(data);
        });

        if (!timer_res)
            return timer_res.err();

        data->timer = timer_res.unwrap();
        if (auto rhs = data->timer->start(0, 1)) {
            data->timer = nullptr;
            return manapi::ev::status_internal("timer::start", rhs);
        }
    }
    return manapi::ev::status_ok();
}

static void timerpool__erase_task(const std::shared_ptr<manapi::timerpool::data_t> &data, manapi::timerpool::sorted_storage::iterator sorted_task) MANAPIHTTP_NOEXCEPT {
    if (!data->sorted_tasks.empty()) {
        if (sorted_task->second->flags & manapi::TIMER_TASK_IMPORTANT) {
            data->importants--;
        }

        if (data->sorted_tasks.begin() == sorted_task) {
            sorted_task = data->sorted_tasks.erase(sorted_task);
            timerpool__reinit_timer(data);
        }
        else {
            sorted_task = data->sorted_tasks.erase(sorted_task);
        }


        if (!data->importants && !(data->flags & TIMERPOOL_FLAG_ACTIVE)) {
            //data_->events->stop_watcher(std::move(data_->timer));
            ::timerpool__stop(data);
        }
    }
}

manapi::timerpool::timerpool() = default;

manapi::status_or<std::shared_ptr<manapi::timerpool>> manapi::timerpool::create() MANAPIHTTP_NOEXCEPT {
    try {
        auto d = std::make_shared<timerpool>();
        d->m_data = std::make_shared<data_t>(sorted_storage(), 0, nullptr);
        return std::move(d);
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::timerpool::~timerpool() {
    this->stop();
}

manapi::timerpool::timerpool(timerpool &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
}

manapi::timerpool & manapi::timerpool::operator=(timerpool &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
    return *this;
}

manapi::timerpool::timerpool(const timerpool &n) {
    this->m_data = n.m_data;
}

manapi::timerpool & manapi::timerpool::operator=(const timerpool &n) {
    if (this != &n) {
        this->m_data = n.m_data;
    }
    return *this;
}

manapi::status_or<manapi::timer> manapi::timerpool::append_timer_sync(size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_timer_sync(ms, TIMER_DEFAULT, std::move(task));
}

manapi::status_or<manapi::timer> manapi::timerpool::append_timer_sync(size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return ::timerpool__append(this, std::chrono::milliseconds(ms), nullptr, std::move(task), false, type);
}

manapi::status_or<manapi::timer> manapi::timerpool::append_timer_async(size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_timer_async(ms, TIMER_DEFAULT, std::move(task));
}

manapi::status_or<manapi::timer> manapi::timerpool::append_timer_async(size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return ::timerpool__append(this, std::chrono::milliseconds(ms), std::move(task), nullptr, false, type);
}

void manapi::timerpool::remove_timer(std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    auto const it = this->m_data->sorted_tasks.find(decltype(this->m_data->sorted_tasks)::key_type{data->point, std::move(data)});
    if (it != this->m_data->sorted_tasks.end()) {
        ::timerpool__erase_task(this->m_data, it);
    }
}

manapi::status_or<manapi::timer> manapi::timerpool::append_interval_async(size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_interval_async(ms, TIMER_DEFAULT, std::move(task));
}

manapi::status_or<manapi::timer> manapi::timerpool::append_interval_sync(size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return this->append_interval_sync(ms, TIMER_DEFAULT, std::move(task));
}

manapi::status_or<manapi::timer> manapi::timerpool::append_interval_async(size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT {
    return ::timerpool__append(this, std::chrono::milliseconds(ms), std::move(task), nullptr, true, type);
}

manapi::status_or<manapi::timer> manapi::timerpool::append_interval_sync(size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT {
    return ::timerpool__append(this, std::chrono::milliseconds(ms), nullptr, std::move(task), true, type);
}

manapi::status manapi::internal::timer__update_interval_state(const std::shared_ptr<timer::timer_data_t> &data) MANAPIHTTP_NOEXCEPT {
    try {
        auto &tp_data = manapi::async::etimerpool()->data();
        data->flags |= TIMER_TASK_ACTIVE;
        data->point = std::chrono::steady_clock::now() + data->delay;

        auto res = tp_data->sorted_tasks.insert({data->point, std::move(data)});
        if (!res.second) {
            return status_already_exists("timerpool:insert failed");
        }

        if (res.first->second->flags & TIMER_TASK_IMPORTANT) {
            tp_data->importants++;
        }

        if (tp_data->sorted_tasks.begin() == res.first) {
            ::timerpool__reinit_timer(tp_data);
        }

        return status_ok();
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return status_internal();
}

void manapi::internal::timer__unref_important() MANAPIHTTP_NOEXCEPT {
    auto &tp_data = manapi::async::etimerpool()->data();
    tp_data->importants--;

    if (!tp_data->importants && !(tp_data->flags & TIMERPOOL_FLAG_ACTIVE)) {
        ::timerpool__stop(tp_data);
    }
}

manapi::status manapi::timerpool::again_timer(std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT {
    try {
        auto const point = data->point;
        data->flags |= TIMER_TASK_ACTIVE;
        auto const res = this->m_data->sorted_tasks.insert({point, std::move(data)});

        if (!res.second)
            return status_internal("timerpool:insert failed");

        if (res.first == this->m_data->sorted_tasks.begin())
            ::timerpool__reinit_timer(this->m_data);

        if (res.first->second->flags & TIMER_TASK_IMPORTANT) {
            this->m_data->importants++;
            if (!this->m_data->timer) {
                if (manapi::async::context_exists()) {
                    auto result = ::timerpool__init_timer(this->m_data);
                    if (!result) {
                        manapi::status status;
                        auto s_data = result.data();
                        s_data.errnum(ERR_INTERNAL);
                        status.data(std::move(s_data));
                        return std::move(status);
                    }
                }
            }
        }

        return status_ok();
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return status_internal();
}

manapi::ev::status manapi::timerpool::start() MANAPIHTTP_NOEXCEPT {
    if ((this->m_data->flags & TIMERPOOL_FLAG_ACTIVE)) {
        return manapi::status_ok();
    }

    try {
        auto result = ::timerpool__init_timer(this->m_data);
        if (!result) {
            return std::move(result);
        }

        ::timerpool__reinit_timer(this->m_data);

        this->m_data->flags |= TIMERPOOL_FLAG_ACTIVE;

        return manapi::status_ok();
    }
    catch (std::exception const &) {
        return manapi::ev::status_resource_exhausted();
    }
}

void manapi::timerpool::stop () MANAPIHTTP_NOEXCEPT {
    if (this->m_data)
        return ::timerpool__stop(this->m_data);
}

void manapi::timerpool::run_once() MANAPIHTTP_NOEXCEPT {
    if (this->m_data)
        ::timerpool__start(this->m_data);
}

void manapi::timerpool::clear() MANAPIHTTP_NOEXCEPT {
    if (!(this->m_data->flags & TIMERPOOL_FLAG_ACTIVE)) {
        this->m_data->sorted_tasks.clear();
        this->m_data->importants = 0;
        ::timerpool__reinit_timer(this->m_data);
    }
}

const std::shared_ptr<manapi::timerpool::data_t> & manapi::timerpool::data() {
    return this->m_data;
}
