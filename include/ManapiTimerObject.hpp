#pragma once

#include <functional>
#include <optional>

#include "./ManapiUtils.hpp"
#include "./ManapiAsync.hpp"

namespace manapi {
    namespace async {
        class cthread;
    }

    class event_loop;
    class timerpool;

    class timer {
    public:
        typedef std::move_only_function<manapi::future<>(const manapi::timer &data)> async_cb_t;
        typedef std::move_only_function<void(const manapi::timer &data)> sync_cb_t;

        struct timer_data_t;

        timer ();

        timer (std::nullptr_t);

        timer (std::shared_ptr<timer_data_t> data);

        static manapi::error::status_or<timer> create (bool interval,sync_cb_t sync_cb) MANAPIHTTP_NOEXCEPT;

        static manapi::error::status_or<timer> create (bool interval,async_cb_t async_cb) MANAPIHTTP_NOEXCEPT;

        timer (const timer &n);

        timer (timer &&n) MANAPIHTTP_NOEXCEPT;

        timer &operator=(timer &&n) MANAPIHTTP_NOEXCEPT;

        timer &operator=(const timer &n);

        timer &operator=(std::nullptr_t);

        ~timer ();

        explicit operator bool () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD size_t id () const MANAPIHTTP_NOEXCEPT;

        void call_ () MANAPIHTTP_NOEXCEPT;

        void clear () MANAPIHTTP_NOEXCEPT;

        void clear_ () MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT;

        void callback_async (async_cb_t cb) MANAPIHTTP_NOEXCEPT;

        void callback_sync (sync_cb_t cb) MANAPIHTTP_NOEXCEPT;

        manapi::error::status again (std::size_t ms) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_async () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_sync () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool enabled () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::shared_ptr<timer_data_t> data_ () const MANAPIHTTP_NOEXCEPT;
    private:
        MANAPIHTTP_NODISCARD static size_t id_ (const std::shared_ptr<timer_data_t> &data);
        std::shared_ptr<timer_data_t> data;
    };
}
