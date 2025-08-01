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
            TIMER_TASK_ENABLED = 1,
            TIMER_TASK_INTERVAL = 2,
            TIMER_TASK_ACTIVE = 4
        };

        typedef std::move_only_function<manapi::future<>(const manapi::timer &data)> async_cb_t;
        typedef std::move_only_function<void(const manapi::timer &data)> sync_cb_t;

        struct timer_data_t {
            int flags{0};
            std::chrono::milliseconds delay;
            std::chrono::steady_clock::time_point point;
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

        void clear () MANAPIHTTP_NOEXCEPT;

        void clear_ () MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT;

        void callback_async (async_cb_t cb);

        void callback_sync (sync_cb_t cb);

        manapi::error::status again (std::size_t ms) MANAPIHTTP_NOEXCEPT;

        [[nodiscard]] bool is_async () const;

        [[nodiscard]] bool is_sync () const;

        [[nodiscard]] bool enabled () const;

        [[nodiscard]] std::shared_ptr<timer_data_t> data_ () const;
    private:
        [[nodiscard]] static size_t id_ (const std::shared_ptr<timer_data_t> &data);
        std::shared_ptr<timer_data_t> data;
    };
}
