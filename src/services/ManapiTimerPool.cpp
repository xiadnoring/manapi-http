#include <thread>
#include <limits>

#include "services/ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "services/ManapiTaskFunction.hpp"

manapi::timerpool::timerpool(std::shared_ptr<event_loop> events, const double &delay) {
    this->cv = std::make_shared<async::condition_variable>(events->taskpool());
    this->smx = std::make_shared<async::mutex>(events->taskpool());
    this->taskpool = events->taskpool();
    this->events = std::move(events);
    this->delay = delay;
    this->deps.store(0);
    this->_stop.store(true);
    this->timer = nullptr;

    this->events->timer_callback([this] (manapi::ev::internal::adding_timerloop_data_t *data)
        -> std::optional<manapi::timer> { return this->_cb_event(data); });
}

manapi::timerpool::~timerpool() {
    this->stop();
}

manapi::future<manapi::timer> manapi::timerpool::async_append_timer_sync(
    size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->events->append_sync_timer(ms, std::move(task));
}

manapi::future<manapi::timer> manapi::timerpool::async_append_timer_async(
    size_t ms, std::move_only_function<future<void>(manapi::timer t)> task) {
    return this->events->append_async_timer(ms, std::move(task));
}

manapi::timer manapi::timerpool::append_timer_sync(size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->_append(std::chrono::milliseconds(ms), nullptr, std::move(task), false);
}

manapi::timer manapi::timerpool::append_timer_async(size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task) {
    return this->_append(std::chrono::milliseconds(ms), std::move(task), nullptr, false);
}

manapi::future<> manapi::timerpool::async_remove_timer(size_t id) {
    return this->events->remove_timer(id);
}

void manapi::timerpool::remove_timer(size_t id) {
    if (this->timer_loop_running) {
        this->prepare_remove.push_back(id);
        return;
    }

    this->_erase_task(id);
}

manapi::future<manapi::timer> manapi::timerpool::async_append_interval_sync( size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->events->append_sync_interval(ms, std::move(task));
}

manapi::future<manapi::timer> manapi::timerpool::async_append_interval_async( size_t ms, std::move_only_function<future<>(manapi::timer t)> task) {
    return this->events->append_async_interval(ms, std::move(task));
}

manapi::timer manapi::timerpool::append_interval_async(size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task) {
    return this->_append(std::chrono::milliseconds(ms), std::move(task), nullptr, true);
}

manapi::timer manapi::timerpool::append_interval_sync(size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->_append(std::chrono::milliseconds(ms), nullptr, std::move(task), true);
}

manapi::future<void> manapi::timerpool::start(std::shared_ptr<timerpool> tp) {
    auto lk = co_await this->smx->lock_guard();

    if (!this->_stop) {
        co_return;
    }

    this->_stop.store(false);

    this->finish_event = co_await this->events->subscribe_finish([tp] ()
        -> future<> { co_await tp->stop_(true); });


    this->timer = co_await this->events->watch_timer(0, 0, [this, tp] (std::shared_ptr<ev::timer> &w)
        -> void { this->_start(); w->repeat(this->delay); w->again(); });

    this->deps.fetch_add(1);
}

manapi::future<void> manapi::timerpool::stop () {
    return this->stop_(false);
}

manapi::future<void> manapi::timerpool::stop_(bool evloop) {
    auto lk = co_await this->smx->lock_guard();

    if (this->_stop) {
        co_return;
    }

    this->_stop.store(true);



    if (!evloop) {
        co_await this->events->unsubscribe_finish(std::exchange(this->finish_event, 0));
        co_await this->events->unwatch_timer(std::move(this->timer));
    }

    this->deps.fetch_sub(1);
    co_await this->cv->wait([this] ()
        -> bool { return this->deps == 0; });
}

void manapi::timerpool::doit() {

}

void manapi::timerpool::_erase_task(const size_t &id) {
    auto task = this->tasks.find(id);
    if (task == this->tasks.end()) {
        return;
    }
    this->_erase_task(task);
}

manapi::timerpool::storage::iterator manapi::timerpool::_erase_task(storage::iterator task) {
    this->sorted_tasks.erase({task->second.point, task->first});
    task = this->tasks.erase(task);
    this->flush_stack_free();
    return task;
}

manapi::timerpool::sorted_storage::iterator manapi::timerpool::_erase_task(sorted_storage::iterator sorted_task) {
    auto it = this->tasks.find(sorted_task->second);
    if (it != this->tasks.end()) {
        this->tasks.erase(it);
    }
    sorted_task = this->sorted_tasks.erase(sorted_task);
    return sorted_task;
}

void manapi::timerpool::_start() {
    this->timer_loop_running = true;
    auto now = std::chrono::steady_clock::now();

    for (auto sorted_task = this->sorted_tasks.begin(); sorted_task != this->sorted_tasks.end(); ) {
        auto task = this->tasks.find(sorted_task->second);

        if (now < sorted_task->first) {
            break;
        }

        if (task->second.active) {
            task->second.active = false;

            auto timertask = task->second.timer;
            timertask._call(this->events, this->taskpool);
            /* if timertask is executed asynchronously, sorted_task hasn't been changed */
            if (task->second.interval) {
                ++sorted_task;
                this->_update_interval_state(task->first);
                continue;
            }
            else {
                timertask._clear();
                sorted_task = this->_erase_task(sorted_task);
                continue;
            }
        }

        ++sorted_task;
    }

    this->timer_loop_running = false;

    while (!this->prepare_remove.empty()) {
        auto id = this->prepare_remove.front();
        this->prepare_remove.pop_front();

        this->_erase_task(id);
    }
}

void manapi::timerpool::flush_stack_free() {
    if (this->tasks.empty()) {
        this->tasks = {};
    }
}

void manapi::timerpool::clear() {
    if (this->_stop && this->deps == 0) {
        this->sorted_tasks.clear();
        this->tasks.clear();
    }
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::timerpool::get_task_pool() const {
    return this->taskpool;
}

std::optional<manapi::timer> manapi::timerpool::_cb_event(void *data1) {
    auto data = static_cast<manapi::ev::internal::adding_timerloop_data_t *> (data1);
    switch (data->flag) {
        case 0: {
            if (data->async_cb) {
                return this->append_timer_async(data->data, std::move(data->async_cb));
            }
            if (data->sync_cb) {
                return this->append_timer_sync(data->data, std::move(data->sync_cb));
            }
            break;
        }
        case 1: {
            if (data->async_cb) {
                return this->append_interval_async(data->data, std::move(data->async_cb));
            }
            if (data->sync_cb) {
                return this->append_interval_sync(data->data, std::move(data->sync_cb));
            }
            break;
        }
        case 2: {
            this->remove_timer(data->data);
            break;
        }
        case 3: {
            this->_update_interval_state(data->data);
            break;
        }
        default:
            break;
    }

    return {};
}

void manapi::timerpool::_update_interval_state(const size_t &id) {
    auto task = this->tasks.find(id);
    if (task == this->tasks.end()) {
        return;
    }

    this->sorted_tasks.erase({task->second.point, id});

    task->second.active = true;
    task->second.point = std::chrono::steady_clock::now() + task->second.delay;

    this->sorted_tasks.insert({task->second.point, id});
}

manapi::timer manapi::timerpool::_append(std::chrono::milliseconds duration, std::move_only_function<future<>(manapi::timer t)> async_task, std::move_only_function<void(manapi::timer t)> task, bool interval) {
    manapi::timer timertask{};

    if (task) {
        timertask = manapi::timer(static_cast<std::move_only_function<void(manapi::timer t)>>(std::move(task)));
    }

    if (async_task) {
        timertask = manapi::timer(static_cast <std::move_only_function<manapi::future<>(manapi::timer t)>>(std::move(async_task)));
    }

    auto _task = this->tasks.insert({timertask.id(), timer_task{
        duration,
        std::chrono::steady_clock::now() + duration,
        timertask,
        interval,
        true
    }});

    if (!_task.second) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_BUG, "timerpool: the following index exists: {}", timertask.id());
    }

    this->sorted_tasks.insert({_task.first->second.point, timertask.id()});

    return std::move(timertask);
}

void manapi::timerpool::_async_call_cb(std::map<size_t, timer_task>::iterator task,
    std::shared_ptr<std::move_only_function<future<void>()>> cb) {
    if (task->second.interval) {
        this->deps.fetch_add(1);
        this->taskpool->append_task ([this, id = task->first, cb = std::move(cb)] () mutable -> void {
            async::run(this->taskpool, [cb = std::move(cb), id, this] () -> future<void> {
                try {
                    co_await (*cb)();
                    co_await this->events->update_state_interval(id);
                    this->deps.fetch_sub(1);
                    co_await this->cv->notify_all();
                }
                catch (std::exception const &e) {
                    //MANAPIHTTP_LOG("Timer Task Exception: {}", e.what());
                }
            });
        });
    }
    else {
        this->taskpool->append_task ([threadpool = this->taskpool, cb] () mutable -> void {
            async::run(threadpool, [cb] () -> future<void> {
                try {
                    co_await (*cb)();
                }
                catch (std::exception const &e) {
                    //MANAPIHTTP_LOG("Timer Task Exception: {}", e.what());
                }
            });
        });
    }
}