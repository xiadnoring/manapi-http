#include <thread>
#include <limits>

#include "services/ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "services/ManapiTaskFunction.hpp"

enum timerpool_flags {
    TIMERPOOL_FLAG_ACTIVE = 1,
    TIMERPOOL_FLAG_RUNNING = 2
};

enum timertask_flags {
    TIMERTASK_FLAG_ACTIVE = 1,
    TIMERTASK_FLAG_INTERVAL = 2
};

manapi::timerpool::timerpool(std::shared_ptr<event_loop> events) {
    this->data_ = std::make_shared<data_t>(chain<size_t>(), sorted_storage(), storage(), std::move(events), 0, nullptr, 0);
}

manapi::timerpool::~timerpool() {
    this->stop();
}

manapi::timerpool::timerpool(timerpool &&n) noexcept {
    this->data_ = std::move(n.data_);
}

manapi::timerpool & manapi::timerpool::operator=(timerpool &&n) noexcept {
    this->data_ = std::move(n.data_);
    return *this;
}

manapi::timerpool::timerpool(const timerpool &n) {
    this->data_ = n.data_;
}

manapi::timerpool & manapi::timerpool::operator=(const timerpool &n) {
    this->data_ = n.data_;
    return *this;
}

// manapi::future<manapi::timer> manapi::timerpool::async_append_timer_sync(
//     size_t ms, std::move_only_function<void(manapi::timer t)> task) {
//     return this->data_->events->append_sync_timer(ms, std::move(task));
// }
//
// manapi::future<manapi::timer> manapi::timerpool::async_append_timer_async(
//     size_t ms, std::move_only_function<future<void>(manapi::timer t)> task) {
//     return this->data_->events->append_async_timer(ms, std::move(task));
// }

manapi::timer manapi::timerpool::append_timer_sync(size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), false);
}

manapi::timer manapi::timerpool::append_timer_async(size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task) {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, false);
}

// manapi::future<> manapi::timerpool::async_remove_timer(size_t id) {
//     return this->data_->events->remove_timer(id);
// }

void manapi::timerpool::remove_timer(size_t id) {
    if (this->data_->flags & TIMERPOOL_FLAG_RUNNING) {
        this->data_->prepare_remove.push_back(id);
        return;
    }

    this->erase_task_(this->data_, id);
}

// manapi::future<manapi::timer> manapi::timerpool::async_append_interval_sync( size_t ms, std::move_only_function<void(manapi::timer t)> task) {
//     return this->data_->events->append_sync_interval(ms, std::move(task));
// }
//
// manapi::future<manapi::timer> manapi::timerpool::async_append_interval_async( size_t ms, std::move_only_function<future<>(manapi::timer t)> task) {
//     return this->data_->events->append_async_interval(ms, std::move(task));
// }

manapi::timer manapi::timerpool::append_interval_async(size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task) {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, true);
}

manapi::timer manapi::timerpool::append_interval_sync(size_t ms, std::move_only_function<void(manapi::timer t)> task) {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), true);
}

void manapi::timerpool::update_interval_state(std::size_t id) {
    this->update_interval_state_(this->data_, id);
}

void manapi::timerpool::start() {
    if ((this->data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
        return;
    }

    this->data_->flags |= TIMERPOOL_FLAG_ACTIVE;

    this->data_->finish_event = this->data_->events->subscribe_finish([data = this->data_] ()
        -> future<> { timerpool::stop_(data, true); co_return; });


    this->data_->timer = this->data_->events->create_watcher_timer([data = this->data_] (std::shared_ptr<ev::timer> &w)
        -> void {
        timerpool::start_(data);
    });

    this->data_->timer->start(0, 1);

    reinit_timer_(this->data_);
}

void  manapi::timerpool::stop () {
    return this->stop_(this->data_, false);
}

void manapi::timerpool::stop_(std::shared_ptr<data_t> data, bool evloop) {
    if (!(data->flags & TIMERPOOL_FLAG_ACTIVE)) {
        return;
    }

    data->flags ^= TIMERPOOL_FLAG_ACTIVE;

    data->events->unsubscribe_finish(std::exchange(data->finish_event, 0));
    data->events->stop_watcher(std::move(data->timer));
}

void manapi::timerpool::doit() {

}

void manapi::timerpool::erase_task_(const std::shared_ptr<data_t> &data_,const size_t &id) {
    auto task = data_->tasks.find(id);
    if (task == data_->tasks.end()) {
        return;
    }
    erase_task_(data_, task);
}

manapi::timerpool::storage::iterator manapi::timerpool::erase_task_(const std::shared_ptr<data_t> &data_,storage::iterator task) {
    if (!data_->sorted_tasks.empty()) {
        if (data_->sorted_tasks.begin()->second == task->first) {
            data_->sorted_tasks.erase({task->second.point, task->first});
            reinit_timer_(data_);
        }
        else {
            data_->sorted_tasks.erase({task->second.point, task->first});
        }
    }

    task = data_->tasks.erase(task);
    flush_stack_free(data_);
    return task;
}

manapi::timerpool::sorted_storage::iterator manapi::timerpool::erase_task_(const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) {
    auto it = data_->tasks.find(sorted_task->second);
    if (it != data_->tasks.end()) {
        data_->tasks.erase(it);
    }

    if (!data_->sorted_tasks.empty()) {
        if (data_->sorted_tasks.begin() == sorted_task) {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
            reinit_timer_(data_);
        }
        else {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
        }
    }

    return sorted_task;
}

void manapi::timerpool::start_(const std::shared_ptr<data_t> &data) {
    auto now = std::chrono::steady_clock::now();
    data->flags |= TIMERPOOL_FLAG_RUNNING;

    for (auto sorted_task = data->sorted_tasks.begin(); sorted_task != data->sorted_tasks.end(); ) {
        auto task = data->tasks.find(sorted_task->second);

        if (now < sorted_task->first) {
            break;
        }

        if (task->second.flags & TIMERTASK_FLAG_ACTIVE) {
            task->second.flags ^= TIMERTASK_FLAG_ACTIVE;

            auto timertask = task->second.timer;
            timertask.call_();
            /* if timertask is executed asynchronously, sorted_task hasn't been changed */
            if (task->second.flags & TIMERTASK_FLAG_INTERVAL) {
                ++sorted_task;
                update_interval_state_(data, task->first);
                continue;
            }
            else {
                timertask.clear_();
                sorted_task = erase_task_(data, sorted_task);
                continue;
            }
        }

        ++sorted_task;
    }

    data->flags ^= TIMERPOOL_FLAG_RUNNING;

    while (!data->prepare_remove.empty()) {
        auto id = data->prepare_remove.front();
        data->prepare_remove.pop_front();

        erase_task_(data, id);
    }
}

void manapi::timerpool::flush_stack_free(const std::shared_ptr<data_t> &data_) {
    if (data_->tasks.empty()) {
        data_->tasks = {};
    }
}

uint64_t manapi::timerpool::calculate_repeat_(const std::shared_ptr<data_t> &data_) {
    if (data_->sorted_tasks.empty()) {
        return 0;
    }

    return std::min(static_cast<uint64_t>(1),
        static_cast<uint64_t>((data_->sorted_tasks.begin()->first - std::chrono::steady_clock::now()).count()));
}

bool manapi::timerpool::reinit_timer_(const std::shared_ptr<data_t> &data_) {
    if (data_->timer) {
        auto const delay = calculate_repeat_(data_);
        if (delay) {
            if (data_->timer->is_active()) {
                data_->timer->repeat(delay);
                if (data_->timer->again()) {
                    data_->events->taskpool()->logger()->debug(manapi::logger::default_service, "timerpool again failed");
                    return false;
                }
            }
            else {
                data_->timer->start(delay, 1);
            }
        }
        else {
            data_->timer->stop();
        }
    }

    return true;
}

void manapi::timerpool::clear() {
    if (!(this->data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
        this->data_->sorted_tasks.clear();
        this->data_->tasks.clear();
    }
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::timerpool::taskpool() const {
    return this->data_->events->taskpool();
}

// std::optional<manapi::timer> manapi::timerpool::_cb_event(void *data1) {
//     auto data = static_cast<manapi::ev::internal::adding_timerloop_data_t *> (data1);
//     switch (data->flag) {
//         case 0: {
//             if (data->async_cb) {
//                 return this->append_timer_async(data->data, std::move(data->async_cb));
//             }
//             if (data->sync_cb) {
//                 return this->append_timer_sync(data->data, std::move(data->sync_cb));
//             }
//             break;
//         }
//         case 1: {
//             if (data->async_cb) {
//                 return this->append_interval_async(data->data, std::move(data->async_cb));
//             }
//             if (data->sync_cb) {
//                 return this->append_interval_sync(data->data, std::move(data->sync_cb));
//             }
//             break;
//         }
//         case 2: {
//             this->remove_timer(data->data);
//             break;
//         }
//         case 3: {
//             update_interval_state_(this->data_, data->data);
//             break;
//         }
//         default:
//             break;
//     }
//
//     return {};
// }

void manapi::timerpool::update_interval_state_(const std::shared_ptr<data_t> &data_, const size_t &id) {
    auto task = data_->tasks.find(id);
    if (task == data_->tasks.end()) {
        return;
    }

    data_->sorted_tasks.erase({task->second.point, id});

    task->second.flags |= TIMERTASK_FLAG_ACTIVE;
    task->second.point = std::chrono::steady_clock::now() + task->second.delay;

    data_->sorted_tasks.insert({task->second.point, id});
}

manapi::timer manapi::timerpool::append_(std::chrono::milliseconds duration, std::move_only_function<future<>(manapi::timer t)> async_task, std::move_only_function<void(manapi::timer t)> task, bool interval) {
    manapi::timer timertask{};

    if (task) {
        timertask = manapi::timer(static_cast<std::move_only_function<void(manapi::timer t)>>(std::move(task)));
    }

    if (async_task) {
        timertask = manapi::timer(static_cast <std::move_only_function<manapi::future<>(manapi::timer t)>>(std::move(async_task)));
    }

    auto _task = this->data_->tasks.insert({timertask.id(), timer_task{
        duration,
        std::chrono::steady_clock::now() + duration,
        timertask,
        interval ? TIMERTASK_FLAG_INTERVAL : 0
    }});

    if (!_task.second) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_BUG, "timerpool: the following index exists: {}", timertask.id());
    }

    auto res = this->data_->sorted_tasks.insert({_task.first->second.point, timertask.id()});

    if (res.second && res.first == this->data_->sorted_tasks.begin()) {
        reinit_timer_(this->data_);
    }

    return std::move(timertask);
}