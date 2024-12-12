#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "ManapiThreadPool.hpp"
#include "ManapiTask.hpp"

namespace manapi::net::utils {
    struct timer_task {
        std::chrono::milliseconds delay;
        std::function <void()> task;
        std::chrono::time_point<std::chrono::high_resolution_clock> point;
        bool interval;
        bool enabled;
    };
    class timerpool : public net::task {
    public:
        explicit timerpool(net::threadpool<net::task> &threadpool, const size_t &delay = 50);
        ~timerpool();
        size_t append_timer (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        void remove_timer (const size_t &id);
        size_t append_interval (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        void start ();
        void stop ();
        void doit ();
    protected:
        // wait while deps being exists
        Atomic <size_t> deps;
        std::mutex state_mutex;
        std::condition_variable cv;

        void _update_interval_state (const size_t& id);
        size_t _append (const std::chrono::milliseconds &duration, const std::function<void()> &task, const bool &inteval);
        std::unordered_map <size_t, timer_task> tasks;
        net::threadpool<net::task> *threadpool;
        std::mutex mx;
        size_t index = 1;
        bool is_stop = false;
        size_t delay{};
    private:
    };
}