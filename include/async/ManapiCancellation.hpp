#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class cancellation_action {
        struct data_t {
            std::atomic<int> status_;
            int ask;
            ssize_t timeout_; /* ms */
            manapi::timer timeout_struct_;
            std::unique_ptr<std::move_only_function<void(async::shared_cthread ctx)>> cancel_sync_callback_;
            std::unique_ptr<std::move_only_function<void()>> ready_callback_;
            async::shared_cthread ctx;
            std::shared_ptr<ev::async> watcher;
            std::unique_ptr<manapi::async::cancellation_action> want_to_unit;
            std::unique_ptr<std::vector<cancellation_action>> associated;
        };
    public:
        cancellation_action ();

        cancellation_action (nullptr_t);

        cancellation_action (async::shared_cthread ctx);

        cancellation_action (async::shared_cthread ctx, cancellation_action cancellation);

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
        void reset (async::shared_cthread ctx);

        /**
         * Set a callback which will be called after preparation
         *
         * @param callback Callback that will be called after preparation
         */
        void handle_ready (std::move_only_function<void()> callback);

        /**
         * Set a callback that will be called while canceling
         *
         * @param callback Callback that will be called while canceling
         * @note It must be called only in @code event loop thread@endcode
         */
        void cancel_callback (std::move_only_function<void(async::shared_cthread ctx)> callback);

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
        void cancel ();

        /**
         * Request a callback to cancel your action
         */
        void ask_cancel_callback ();

        /**
         * Set a timeout in milliseconds
         *
         * @param timeout Timeout in milliseconds
         */
        void timeout (ssize_t timeout);

        /**
         * It will return a message stating that it asks
         * a callback for cancellation
         *
         * @return a message stating that it asks a callback for cancellation
         */
        [[nodiscard]] bool contains_cancel_callback () const;

        /**
         * It will return a message stating that it asks
         * a timeout
         *
         * @return a message stating that it asks a timeout
         */
        [[nodiscard]] bool contains_timeout () const;

        /**
         * Return current timeout in milliseconds
         *
         * @return timeout in milliseconds
         */
        [[nodiscard]] ssize_t timeout () const;


        /**
         * Disable cancellation without calling the callback to cancel
         */
        void disable_cancellation ();
    private:
        /**
         * Send a ready status message
         * It will call the ready callback in the @code event loop thread @endcode
         */
        void ready ();
        void send_async_();
        static void stop_timeout_ (std::shared_ptr<data_t> data);
        static void cancel_ (std::shared_ptr<data_t> data);
        std::shared_ptr<data_t> data;
    };
}