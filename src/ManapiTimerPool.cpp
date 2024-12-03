#include <thread>
#include <limits>

#include "ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "ManapiTaskFunction.hpp"

manapi::net::utils::timerpool::timerpool(net::threadpool<net::task> &threadpool, const size_t &delay) {
    this->delay = delay;
    this->deps = 0;
    this->threadpool = &threadpool;
}

manapi::net::utils::timerpool::~timerpool() {
    stop();
}

size_t manapi::net::utils::timerpool::append_timer(const std::chrono::milliseconds &duration, const std::function<void()> &task) {
    return this->_append(duration, task, false);
}

void manapi::net::utils::timerpool::remove_timer(const size_t &id) {
    if (id == 0) { return; }
    std::lock_guard<std::mutex> lk (mx);
    tasks.erase(id);
}

size_t manapi::net::utils::timerpool::append_interval(const std::chrono::milliseconds &duration,
    const std::function<void()> &task) {
    return this->_append(duration, task, true);
}

void manapi::net::utils::timerpool::start() {
    std::lock_guard<std::mutex> lk (state_mutex);

    ++deps;

    while (!is_stop) {
        {
            std::lock_guard<std::mutex> lk (mx);

            std::chrono::high_resolution_clock::now();
            auto now = std::chrono::high_resolution_clock::now();

            for (auto task = tasks.begin(); task != tasks.end();) {

                if (task->second.enabled && now >= task->second.point) {
                    const auto func = task->second.task;
                    task->second.enabled = false;
                    now = std::chrono::high_resolution_clock::now();
                    if (task->second.interval) {
                        try {
                            ++deps;
                            threadpool->append_task(std::make_unique<net::function_task>([func, id = task->first, this] () -> void {
                                func ();
                                _update_interval_state(id);
                                --deps;
                                cv.notify_all();
                            }));
                        }
                        catch (std::exception const &e) { MANAPIHTTP_LOG("Timer Task Exception: {}", e.what()); }
                    }
                    else {
                        try { threadpool->append_task(std::make_unique<net::function_task>([func] () -> void {
                            try { func (); }
                            catch (std::exception const &e) { MANAPIHTTP_LOG("Unexpected error: {}", e.what()); }
                        })); }
                        catch (std::exception const &e) { MANAPIHTTP_LOG("Timer Task Exception: {}", e.what()); }
                        task = tasks.erase(task);
                        continue;
                    }
                }

                task++;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    --deps;
    cv.notify_all();
}

void manapi::net::utils::timerpool::stop() {
    is_stop = true;

    std::unique_lock<std::mutex> lk (state_mutex);
    cv.wait(lk, [this] () -> bool { MANAPIHTTP_LOG("{}", deps.get().first); return deps == 0; });
}

void manapi::net::utils::timerpool::doit() {
    start();
}

void manapi::net::utils::timerpool::_update_interval_state(const size_t &id) {
    std::lock_guard<std::mutex> lk (mx);
    auto &task = tasks[id];
    task.enabled = true;
    task.point = std::chrono::system_clock::now() + task.delay;
}

size_t manapi::net::utils::timerpool::_append(const std::chrono::milliseconds &duration,
                                              const std::function<void()> &task, const bool &inteval) {
    std::lock_guard<std::mutex> lk (mx);
    while (tasks.contains(index)) { index++; if (index == ULLONG_MAX) { index = 0; } }
    const size_t id = index; index++;
    tasks[id] = {duration, task, std::chrono::high_resolution_clock::now() + duration, inteval, true};
    if (index == ULLONG_MAX) { index = 1; }
    return id;
}
