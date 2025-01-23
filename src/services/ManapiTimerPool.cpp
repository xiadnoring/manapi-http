#include <thread>
#include <limits>

#include "services/ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "services/ManapiTaskFunction.hpp"

manapi::timerpool::timerpool(std::shared_ptr<event_loop> events, const double &delay) {
    this->cv = std::make_shared<async::condition_variable>(events->get_task_pool());
    this->mx = std::make_shared<async::mutex>(events->get_task_pool());
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
    const std::chrono::milliseconds &duration, std::function<void()> task) {
    co_return co_await this->_append(duration, nullptr, task, false);
}

manapi::future<size_t> manapi::timerpool::async_append_timer_async(
    const std::chrono::milliseconds &duration, std::function<future<void>()> task) {
    co_return co_await this->_append(duration, task, nullptr, false);
}

size_t manapi::timerpool::append_timer(const std::chrono::milliseconds &duration, const std::function<void()> &task) {
    return this->async_append_timer_sync(duration, task).get(this->taskpool);
}

manapi::future<> manapi::timerpool::async_remove_timer(size_t id) {
    if (id == 0) {
        co_return;
    }
    auto lk = co_await this->mx->lock_guard();
    this->_erase_task(id);
}

void manapi::timerpool::remove_timer(const size_t &id) {
    this->async_remove_timer(id).get(this->taskpool);
}

manapi::future<size_t> manapi::timerpool::async_append_interval_sync(
    const std::chrono::milliseconds &duration, std::function<void()> task) {
    co_return co_await this->_append(duration, nullptr, task, true);
}

manapi::future<size_t> manapi::timerpool::async_append_interval_async(
    const std::chrono::milliseconds &duration, std::function<future<>()> task) {
    co_return co_await this->_append(duration, task, nullptr, true);
}

size_t manapi::timerpool::append_interval(const std::chrono::milliseconds &duration, const std::function<void()> &task) {
    return this->async_append_interval_sync(duration, task).get(this->taskpool);
}

manapi::future<void> manapi::timerpool::start(std::shared_ptr<timerpool> tp) {
    auto lk = co_await this->smx->lock_guard();

    if (!this->_stop) {
        co_return;
    }

    this->_stop.store(false);

    this->finish_event = co_await this->events->subscribe_finish([tp] () -> future<> {
        co_await tp->stop();
    });

    this->timer = this->events->create_watcher_timer(0, 0, [this, tp] (ev::timer &w, int revents) -> void {
        this->deps.fetch_add(1);

        async::run(this->taskpool, this->_start(), [tp, &w] () -> void {
            w.repeat = tp->delay;

            tp->deps.fetch_sub(1);

            if (!tp->_stop) {
                async::run(tp->taskpool,
                    tp->events->again_timer(tp->timer));
            }
            else {
                async::run(tp->taskpool,
                    tp->cv->notify_all());
            }
        });
    });

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
    this->tasks.erase(sorted_task->second);
    sorted_task = this->sorted_tasks.erase(sorted_task);
    return sorted_task;
}

manapi::future<> manapi::timerpool::_start() {
    auto lk = co_await this->mx->lock_guard();

    auto now = std::chrono::steady_clock::now();

    for (auto sorted_task = this->sorted_tasks.begin(); sorted_task != this->sorted_tasks.end(); ) {
        auto task = this->tasks.find(sorted_task->second);

        if (now < sorted_task->first) {
            break;
        }

        if (task->second.enabled) {
            task->second.enabled = false;

            if (task->second.task) {
                this->_call_cb(task, task->second.task);
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

manapi::future<void> manapi::timerpool::_update_interval_state(const size_t &id) {
    auto lk = co_await this->mx->lock_guard();
    
    auto task = this->tasks.find(id);
    if (task == this->tasks.end()) {
        co_return;
    }

    this->sorted_tasks.erase({task->second.point, id});

    task->second.enabled = true;
    task->second.point = std::chrono::steady_clock::now() + task->second.delay;

    this->sorted_tasks.insert({task->second.point, id});
}

manapi::future<size_t> manapi::timerpool::_append(const std::chrono::milliseconds &duration, const std::function<future<>()> &async_task, const std::function<void()> &task, const bool &inteval) {
    auto lk = co_await this->mx->lock_guard();
    
    while (this->tasks.contains(this->index)) {
        this->index++;
        if (this->index == std::numeric_limits<size_t>::max()) {
            this->index = 1;
        }
    }
    const size_t id = this->index++;
    auto _task = this->tasks.insert({id, timer_task{
        duration,
        async_task ? std::make_shared<std::function<future<>()>>(async_task) : nullptr,
        task ? std::make_shared<std::function<void()>>(task) : nullptr,
        std::chrono::steady_clock::now() + duration,
        inteval,
        true
    }});

    if (!_task.second) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_BUG, "timerpool: the index was used: {}", id);
    }

    this->sorted_tasks.insert({_task.first->second.point, id});

    if (this->index == std::numeric_limits<size_t>::max()) {
        this->index = 1;
    }
    
    co_return id;
}

void manapi::timerpool::_call_cb(storage::iterator task,
    std::shared_ptr<std::function<void()>> cb) {
    if (task->second.interval) {
        this->deps.fetch_add(1);
        this->taskpool->append_task ([this, id = task->first, cb] () mutable -> void {
            async::run(this->taskpool, [cb, id, this] () -> future<void> {
                try {
                    (*cb)();
                    co_await this->_update_interval_state(id);
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
        this->taskpool->append_task(std::make_unique<net::function_task>([cb = std::move(cb)] () -> void {
            try {
                (*cb)();
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("Unexpected error: {}", e.what());
            }
        }));
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
                    co_await this->_update_interval_state(id);
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