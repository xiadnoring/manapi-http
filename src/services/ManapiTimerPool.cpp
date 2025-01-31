#include <thread>
#include <limits>

#include "services/ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "services/ManapiTaskFunction.hpp"

manapi::timerpool::timerpool(std::shared_ptr<event_loop> events, const double &delay) {
    this->cv = std::make_shared<async::condition_variable>(events->get_task_pool());
    this->smx = std::make_shared<async::mutex>(events->get_task_pool());
    this->taskpool = events->get_task_pool();
    this->events = std::move(events);
    this->delay = delay;
    this->deps.store(0);
    this->_stop.store(true);
    this->timer = nullptr;
}

manapi::timerpool::~timerpool() {
    this->stop();
}

manapi::future<size_t> manapi::timerpool::async_append_timer_sync(
    size_t ms, std::function<void()> task) {
    return this->events->append_sync_timer(ms, std::move(task));
}

manapi::future<size_t> manapi::timerpool::async_append_timer_async(
    size_t ms, std::function<future<void>()> task) {
    return this->events->append_async_timer(ms, std::move(task));
}

size_t manapi::timerpool::append_timer_sync(size_t ms, std::function<void()> task) {
    return this->_append(std::chrono::milliseconds(ms), nullptr, std::move(task), false);
}

size_t manapi::timerpool::append_timer_async(size_t ms, std::function<manapi::future<>()> task) {
    return this->_append(std::chrono::milliseconds(ms), std::move(task), nullptr, false);
}

manapi::future<> manapi::timerpool::async_remove_timer(size_t id) {
    return this->events->remove_timer(id);
}

void manapi::timerpool::remove_timer(size_t id) {
    this->_erase_task(id);
}

manapi::future<size_t> manapi::timerpool::async_append_interval_sync( size_t ms, std::function<void()> task) {
    return this->events->append_sync_interval(ms, std::move(task));
}

manapi::future<size_t> manapi::timerpool::async_append_interval_async( size_t ms, std::function<future<>()> task) {
    return this->events->append_async_interval(ms, std::move(task));
}

size_t manapi::timerpool::append_interval_async(size_t ms, std::function<manapi::future<>()> task) {
    return this->_append(std::chrono::milliseconds(ms), std::move(task), nullptr, true);
}

size_t manapi::timerpool::append_interval_sync(size_t ms, std::function<void()> task) {
    return this->_append(std::chrono::milliseconds(ms), nullptr, std::move(task), true);
}

manapi::future<void> manapi::timerpool::start(std::shared_ptr<timerpool> tp) {
    auto lk = co_await this->smx->lock_guard();

    if (!this->_stop) {
        co_return;
    }

    this->events->set_timer_callback([this] (adding_timer_data_t &&data)
        -> size_t { return this->_cb_event(std::forward<decltype(data)>(data)); });

    this->_stop.store(false);

    this->finish_event = co_await this->events->subscribe_finish([tp] () -> future<> {
        co_await tp->stop();
    });

    this->timer = this->events->create_watcher_timer(0, 0, [this, tp] (ev::timer &w, int revents)
        -> void { this->_start(); w.repeat = this->delay; w.again(); });

    this->deps.fetch_add(1);

    co_await this->events->watch_timer(this->timer);
}

manapi::future<void> manapi::timerpool::stop() {
    auto lk = co_await this->smx->lock_guard();

    if (this->_stop) {
        co_return;
    }

    this->_stop.store(true);


    co_await this->events->unsubscribe_finish(std::exchange(this->finish_event, 0));
    co_await this->events->unwatch_timer(std::move(this->timer));

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
    if (task->second.token) {
        *task->second.token = 0;
    }
    this->sorted_tasks.erase({task->second.point, task->first});
    task = this->tasks.erase(task);
    this->flush_stack_free();
    return task;
}

manapi::timerpool::sorted_storage::iterator manapi::timerpool::_erase_task(sorted_storage::iterator sorted_task) {
    auto it = this->tasks.find(sorted_task->second);
    if (it != this->tasks.end()) {
        if (it->second.token) {
            *it->second.token = 0;
        }
        this->tasks.erase(it);
    }
    sorted_task = this->sorted_tasks.erase(sorted_task);
    return sorted_task;
}

void manapi::timerpool::_start() {
    auto now = std::chrono::steady_clock::now();

    for (auto sorted_task = this->sorted_tasks.begin(); sorted_task != this->sorted_tasks.end(); ) {
        auto task = this->tasks.find(sorted_task->second);

        if (now < sorted_task->first) {
            break;
        }

        if (task->second.enabled) {
            task->second.enabled = false;

            if (task->second.task) {
                auto next_sorted_task = std::next(sorted_task);
                auto cb = task->second.task;
                this->_call_cb(task, cb);

                if (cb.use_count()==1) {
                    /* task was destroyed */
                    sorted_task = next_sorted_task;
                    continue;
                }

                if (task->second.interval) {
                    this->_update_interval_state(task->first);
                }
            }
            if (task->second.async_task) {
                this->_async_call_cb(task, task->second.async_task);
            }
            if (!task->second.interval) {
                sorted_task = this->_erase_task(sorted_task);
                continue;
            }
        }

        ++sorted_task;
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
        this->index = 1;
    }
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::timerpool::get_task_pool() const {
    return this->taskpool;
}

size_t manapi::timerpool::_cb_event(adding_timer_data_t data) {
    switch (data.flag) {
        case 0: {
            if (data.async_cb) {
                return this->append_timer_async(data.data, std::move(data.async_cb));
            }
            if (data.sync_cb) {
                return this->append_timer_sync(data.data, std::move(data.sync_cb));
            }
            break;
        }
        case 1: {
            if (data.async_cb) {
                return this->append_interval_async(data.data, std::move(data.async_cb));
            }
            if (data.sync_cb) {
                return this->append_interval_sync(data.data, std::move(data.sync_cb));
            }
            break;
        }
        case 2: {
            this->remove_timer(data.data);
            break;
        }
        case 3: {
            this->_update_interval_state(data.data);
            break;
        }
        default:
            break;
    }

    return 0;
}

void manapi::timerpool::_update_interval_state(const size_t &id) {
    auto task = this->tasks.find(id);
    if (task == this->tasks.end()) {
        return;
    }

    this->sorted_tasks.erase({task->second.point, id});

    task->second.enabled = true;
    task->second.point = std::chrono::steady_clock::now() + task->second.delay;

    this->sorted_tasks.insert({task->second.point, id});
}

size_t manapi::timerpool::_append(std::chrono::milliseconds duration, std::function<future<>()> async_task, std::function<void()> task, bool interval) {
    while (this->tasks.contains(this->index)) {
        this->index++;
        if (this->index == std::numeric_limits<size_t>::max()) {
            this->index = 1;
        }
    }
    const size_t id = this->index++;
    auto _task = this->tasks.insert({id, timer_task{
        duration,
        async_task ? std::make_shared<std::function<future<>()>>(std::move(async_task)) : nullptr,
        task ? std::make_shared<std::function<void()>>(std::move(task)) : nullptr,
        std::chrono::steady_clock::now() + duration,
        interval,
        true
    }});

    if (!_task.second) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_BUG, "timerpool: the following index exists: {}", id);
    }

    this->sorted_tasks.insert({_task.first->second.point, id});

    if (this->index == std::numeric_limits<size_t>::max()) {
        this->index = 1;
    }
    
    return id;
}

void manapi::timerpool::_call_cb(storage::iterator task,
    std::shared_ptr<std::function<void()>> cb) {
    if (task->second.interval) {

        try {
            (*cb)();
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("Timer Task Exception: {}", e.what());
        }
    }
    else {
        try {
            (*cb)();
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("Unexpected error: {}", e.what());
        }
    }
}

void manapi::timerpool::_async_call_cb(std::unordered_map<size_t, timer_task>::iterator task,
    std::shared_ptr<std::function<future<void>()>> cb) {
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
                    MANAPIHTTP_LOG("Timer Task Exception: {}", e.what());
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
                    MANAPIHTTP_LOG("Timer Task Exception: {}", e.what());
                }
            });
        });
    }
}