#ifndef MANAPITIMERPOOL_HPP
#define MANAPITIMERPOOL_HPP

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "ManapiThreadPool.hpp"
#include "ManapiTask.hpp"

namespace manapi::net::utils {
    struct timer_task {
        std::function <void()> task;
        std::chrono::time_point<std::chrono::high_resolution_clock> point;
    };
    class timerpool : public net::task {
    public:
        explicit timerpool(net::threadpool<net::task> &threadpool, const size_t &delay = 50);
        ~timerpool();
        size_t append_timer (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        void remove_timer (const size_t &id);
        void start ();
        void stop ();
        void doit ();
    protected:
        std::unordered_map <size_t, timer_task> tasks;
        net::threadpool<net::task> *threadpool;
        std::mutex mx;
        size_t index = 1;
        bool is_stop = false;
        size_t delay{};
    private:
    };
}

#endif //MANAPITIMERPOOL_HPP
