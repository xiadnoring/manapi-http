#pragma once

#include <functional>
#include <optional>

#include "../ManapiAsync.hpp"

namespace manapi {
    namespace async {
        class context;
    }

    class event_loop;
    class timerpool;

    class timer {
        struct timer_data_t {
            std::atomic<bool> enabled{true};
            std::optional<std::move_only_function<manapi::future<>(std::shared_ptr<timer_data_t> &data, std::shared_ptr<event_loop> eventloop,  std::shared_ptr<threadpool<task>> taskpool)>> async_cb{};
            std::optional<std::move_only_function<void(const std::shared_ptr<timer_data_t> &data, const std::shared_ptr<event_loop> &eventloop, const std::shared_ptr<threadpool<task>> &taskpool)>> sync_cb{};
        };
    public:
        timer ();
        timer (std::shared_ptr<timer_data_t> data);
        timer (std::move_only_function<void(manapi::timer t)> sync_cb);
        timer (std::move_only_function<manapi::future<>(manapi::timer t)> async_cb);
        timer (const timer &n);
        timer (timer &&n) noexcept;
        timer &operator=(timer &&n) noexcept;
        ~timer ();

        [[nodiscard]] size_t id () const;
        void _call (const std::shared_ptr<event_loop> &eventloop, const std::shared_ptr<threadpool<task>> &taskpool);
        void _clear ();
        manapi::future<> async_stop (const std::shared_ptr<manapi::event_loop> &events);
        manapi::future<> async_stop (const std::shared_ptr<manapi::async::context> &ctx);
        void sync_stop (const std::shared_ptr<manapi::timerpool> &timerpool);
        void sync_stop (const std::shared_ptr<manapi::async::context> &ctx);
        [[nodiscard]] bool is_async () const;
        [[nodiscard]] bool is_sync () const;
        [[nodiscard]] bool enabled () const;
    private:
        [[nodiscard]] static size_t _id (const std::shared_ptr<timer_data_t> &data);
        std::shared_ptr<timer_data_t> data;
    };
}
