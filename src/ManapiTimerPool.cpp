#include <thread>
#include <limits>

#include "ManapiTimerPool.hpp"
#include "ManapiUtils.hpp"
#include "ManapiTaskFunction.hpp"

manapi::net::utils::timerpool::timerpool(net::threadpool<net::task> &threadpool, const size_t &delay) {
    this->delay = delay;
    this->threadpool = &threadpool;
}

manapi::net::utils::timerpool::~timerpool() {
    stop();
}

size_t manapi::net::utils::timerpool::append_timer(const std::chrono::milliseconds &duration, const std::function<void()> &task) {
    std::lock_guard<std::mutex> lk (mx);
    while (tasks.contains(index)) { index++; if (index == ULLONG_MAX) { index = 0; } }
    const size_t id = index; index++;
    tasks[id] = {task, std::chrono::high_resolution_clock::now() + duration};
    if (index == ULLONG_MAX) { index = 1; }
    return id;
}

void manapi::net::utils::timerpool::remove_timer(const size_t &id) {
    if (id == 0) { return; }
    std::lock_guard<std::mutex> lk (mx);
    tasks.erase(id);
}

void manapi::net::utils::timerpool::start() {
    while (!is_stop) {
        {
            std::lock_guard<std::mutex> lk (mx);

            std::chrono::high_resolution_clock::now();
            auto now = std::chrono::high_resolution_clock::now();

            for (auto task = tasks.begin(); task != tasks.end();) {
                if (now >= task->second.point) {
                    const auto func = task->second.task;
                    try { threadpool->append_task(new net::function_task([func] () -> void { func (); })); }
                    catch (std::exception const &e) { MANAPI_LOG("Timer Task Exception: {}", e.what()); }
                    now = std::chrono::high_resolution_clock::now();
                    task = tasks.erase(task);
                }
                else { task++; }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

void manapi::net::utils::timerpool::stop() {
    is_stop = true;
}

void manapi::net::utils::timerpool::doit() {
    start();
}
