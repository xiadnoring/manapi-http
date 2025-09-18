#pragma once

#include <memory>
#include <functional>

#include "../ManapiErrors.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::async {
    class cancellation_action {
        struct data_t;
    public:
        cancellation_action (std::nullptr_t);

        cancellation_action ();

        static cancellation_action unit (cancellation_action cancellation);

        cancellation_action sub () const;

        cancellation_action (cancellation_action &&n) noexcept;

        cancellation_action &operator=(cancellation_action &&n) noexcept;

        cancellation_action (const cancellation_action &n);

        cancellation_action &operator=(const cancellation_action &n);

        ~cancellation_action();

        explicit operator bool () const;

        /**
         * Reset it
         *
         * @return self
         */
        cancellation_action &operator=(nullptr_t);

        /**
         * Reuse of the cancellation object
         *
         * @param ctx Async context
         */
        void reset ();

        /**
         * Set a callback that will be called while canceling
         *
         * @param callback Callback that will be called while canceling
         * @note It must be called only in @code event loop thread@endcode
         */
        void cancel_callback (std::move_only_function<void()> callback) MANAPIHTTP_NOEXCEPT;

        /**
         * Set an other cancellation that will be cancelled while canceling
         *
         * @param cancellation Other cancellation
         * @note It must be called only in @code event loop thread@endcode
         */
        void cancel_callback (cancellation_action cancellation);

        /**
         * Send a canellation signal
         *
         * @throw manapi::exception with error code @code ERR_CANCELLATION_FAILED@endcode in case of failure while sending a signal
         */
        void cancel () MANAPIHTTP_NOEXCEPT;

        /**
         * Request a callback to cancel your action
         */
        void ask_cancel_callback () MANAPIHTTP_NOEXCEPT;

        /**
         * Set a timeout in milliseconds
         *
         * @param timeout Timeout in milliseconds
         * @return Ok on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status timeout (size_t timeout) MANAPIHTTP_NOEXCEPT;

        /**
         * It will return a message stating that it asks
         * a callback for cancellation
         *
         * @return a message stating that it asks a callback for cancellation
         */
        MANAPIHTTP_NODISCARD bool contains_cancel_callback () const;

        /**
         * Return current timeout in milliseconds
         *
         * @return timeout in milliseconds
         */
        MANAPIHTTP_NODISCARD size_t timeout () const;


        /**
         * Disable cancellation without calling the callback to cancel
         */
        void disable ();
    private:
        void send_async_() MANAPIHTTP_NOEXCEPT;
        static void stop_timeout_ (std::shared_ptr<data_t> data) MANAPIHTTP_NOEXCEPT;
        static void cancel_ (std::shared_ptr<data_t> data) MANAPIHTTP_NOEXCEPT;
        std::shared_ptr<data_t> data;
    };
}
