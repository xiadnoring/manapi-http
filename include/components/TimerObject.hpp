#pragma once

#include <functional>
#include <optional>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"

namespace manapi {
    namespace async {
        class cthread;
    }

    class event_loop;
    class timerpool;

    class timer {
    public:
        enum timer_tasks_flags {
            TIMER_TASK_ENABLED = 0b1,
            TIMER_TASK_INTERVAL = 0b10
        };

        typedef std::move_only_function<manapi::future<>(const manapi::timer &data)> async_cb_t;
        typedef std::move_only_function<void(const manapi::timer &data)> sync_cb_t;

        struct timer_data_t {
            int flags{0};
            std::unique_ptr<async_cb_t> async_cb{};
            std::unique_ptr<sync_cb_t> sync_cb{};
        };

        timer ();

        timer (nullptr_t);

        timer (std::shared_ptr<timer_data_t> data);

        timer (bool interval,sync_cb_t sync_cb);

        timer (bool interval,async_cb_t async_cb);

        timer (const timer &n);

        timer (timer &&n) noexcept;

        timer &operator=(timer &&n) noexcept;

        timer &operator=(const timer &n);

        timer &operator=(nullptr_t);

        ~timer ();

        explicit operator bool () const;

        [[nodiscard]] size_t id () const;

        void call_ ();

        void clear_ ();

        void stop ();

        void callback_async (async_cb_t cb);

        void callback_sync (sync_cb_t cb);

        void again (std::size_t ms);

        [[nodiscard]] bool is_async () const;

        [[nodiscard]] bool is_sync () const;

        [[nodiscard]] bool enabled () const;

        [[nodiscard]] std::shared_ptr<timer_data_t> data_ () const;
    private:
        [[nodiscard]] static size_t id_ (const std::shared_ptr<timer_data_t> &data);
        std::shared_ptr<timer_data_t> data;
    };
}
