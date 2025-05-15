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
        struct timer_data_t {
            int flags{0};
            std::unique_ptr<std::move_only_function<manapi::future<>(std::shared_ptr<timer_data_t> &data)>> async_cb{};
            std::unique_ptr<std::move_only_function<void(const std::shared_ptr<timer_data_t> &data)>> sync_cb{};
        };
    public:
        timer ();
        timer (nullptr_t);
        timer (std::shared_ptr<timer_data_t> data);
        timer (bool interval,std::move_only_function<void(manapi::timer t)> sync_cb);
        timer (bool interval,std::move_only_function<manapi::future<>(manapi::timer t)> async_cb);
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
        [[nodiscard]] bool is_async () const;
        [[nodiscard]] bool is_sync () const;
        [[nodiscard]] bool enabled () const;
    private:
        [[nodiscard]] static size_t id_ (const std::shared_ptr<timer_data_t> &data);
        std::shared_ptr<timer_data_t> data;
    };
}
