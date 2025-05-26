#include <thread>
#include <limits>

#include "services/ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "services/ManapiTaskFunction.hpp"

enum timerpool_flags {
    TIMERPOOL_FLAG_ACTIVE = 1,
    TIMERPOOL_FLAG_RUNNING = 2
};

manapi::timerpool::timerpool(std::shared_ptr<event_loop> events) {
    this->data_ = std::make_shared<data_t>(sorted_storage(), std::move(events), 0, nullptr);
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

manapi::timer manapi::timerpool::append_timer_sync(size_t ms, manapi::timer::sync_cb_t task) {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), false);
}

manapi::timer manapi::timerpool::append_timer_async(size_t ms, manapi::timer::async_cb_t task) {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, false);
}

// manapi::future<> manapi::timerpool::async_remove_timer(size_t id) {
//     return this->data_->events->remove_timer(id);
// }

void manapi::timerpool::remove_timer(std::shared_ptr<timer::timer_data_t> data) {
    // if (this->data_->flags & TIMERPOOL_FLAG_RUNNING) {
    //     this->data_->prepare_remove.push_back(id);
    //     return;
    // }

    auto const it = this->data_->sorted_tasks.find(decltype(this->data_->sorted_tasks)::key_type{data->point, std::move(data)});
    if (it != this->data_->sorted_tasks.end()) {
        this->erase_task_(this->data_, it);
    }

}

// manapi::future<manapi::timer> manapi::timerpool::async_append_interval_sync( size_t ms, std::move_only_function<void(manapi::timer t)> task) {
//     return this->data_->events->append_sync_interval(ms, std::move(task));
// }
//
// manapi::future<manapi::timer> manapi::timerpool::async_append_interval_async( size_t ms, std::move_only_function<future<>(manapi::timer t)> task) {
//     return this->data_->events->append_async_interval(ms, std::move(task));
// }

manapi::timer manapi::timerpool::append_interval_async(size_t ms, manapi::timer::async_cb_t task) {
    return this->append_(std::chrono::milliseconds(ms), std::move(task), nullptr, true);
}

manapi::timer manapi::timerpool::append_interval_sync(size_t ms, manapi::timer::sync_cb_t task) {
    return this->append_(std::chrono::milliseconds(ms), nullptr, std::move(task), true);
}

void manapi::timerpool::update_interval_state(std::shared_ptr<timer::timer_data_t> data) {
    this->update_interval_state_(this->data_, std::move(data));
}

void manapi::timerpool::again_timer(std::shared_ptr<manapi::timer::timer_data_t> data) {
    auto const point = data->point;
    data->flags |= timer::TIMER_TASK_ACTIVE;
    auto const res = this->data_->sorted_tasks.insert({point, std::move(data)});


    if (res.second && res.first == this->data_->sorted_tasks.begin()) {
        reinit_timer_(this->data_);
    }
}

void manapi::timerpool::start() {
    if ((this->data_->flags & TIMERPOOL_FLAG_ACTIVE)) {
        return;
    }

    this->data_->flags |= TIMERPOOL_FLAG_ACTIVE;


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

    data->events->stop_watcher(std::move(data->timer));
}

void manapi::timerpool::doit() {

}

void manapi::timerpool::run_once() {
    timerpool::start_(this->data_);
}


void manapi::timerpool::erase_task_(const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) {
    if (!data_->sorted_tasks.empty()) {
        if (data_->sorted_tasks.begin() == sorted_task) {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
            reinit_timer_(data_);
        }
        else {
            sorted_task = data_->sorted_tasks.erase(sorted_task);
        }
    }
}

void manapi::timerpool::start_(const std::shared_ptr<data_t> &data) {
    auto now = std::chrono::steady_clock::now();
    data->flags |= TIMERPOOL_FLAG_RUNNING;

    while (!data->sorted_tasks.empty()) {
        auto sorted_task = data->sorted_tasks.begin();

        if (now < sorted_task->first) {
            break;
        }

        auto &task = sorted_task->second;


        if (task->flags & timer::TIMER_TASK_ACTIVE) {

            manapi::timer timertask  (task);
            sorted_task = data->sorted_tasks.erase(sorted_task);

            task->flags ^= timer::TIMER_TASK_ACTIVE;

            if (task->flags & timer::TIMER_TASK_INTERVAL) {
                timertask.call_();
            }
            else {
                timertask.call_();
            }
        }
    }

    data->flags ^= TIMERPOOL_FLAG_RUNNING;
    reinit_timer_(data);

    // while (!data->prepare_remove.empty()) {
    //     auto id = data->prepare_remove.front();
    //     data->prepare_remove.pop_front();
    //
    //     erase_task_(data, id);
    // }
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

bool manapi::timerpool::reinit_timer_(const std::shared_ptr<data_t> &data_) {
    if (data_->timer) {
        auto const delay = calculate_repeat_(data_);
        if (delay >= 0) {
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

void manapi::timerpool::update_interval_state_(const std::shared_ptr<data_t> &data_, std::shared_ptr<manapi::timer::timer_data_t> data) {
    data->flags |= timer::TIMER_TASK_ACTIVE;
    data->point = std::chrono::steady_clock::now() + data->delay;

    data_->sorted_tasks.insert({data->point, std::move(data)});

    if (data_->sorted_tasks.begin()->second == data) {
        reinit_timer_(data_);
    }
}

manapi::timer manapi::timerpool::append_(std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task, bool interval) {
    manapi::timer timertask{};

    if (task) {
        timertask = manapi::timer(interval, (std::move(task)));
    }

    if (async_task) {
        timertask = manapi::timer(interval, (std::move(async_task)));
    }

    auto data = timertask.data_();

    data->delay = duration;
    data->point = std::chrono::steady_clock::now() + data->delay;

    this->again_timer(std::move(data));

    return std::move(timertask);
}