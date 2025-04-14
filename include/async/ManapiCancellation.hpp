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
            std::unique_ptr<async::mutex> mx;
            std::unique_ptr<std::move_only_function<manapi::future<>()>> cancel_callback_;
            std::unique_ptr<std::move_only_function<void()>> ready_callback_;
            std::shared_ptr<async::context> ctx;
        };
    public:
        cancellation_action ();
        cancellation_action (nullptr_t);
        cancellation_action (std::shared_ptr<async::context> ctx);
        cancellation_action (cancellation_action &&n) noexcept;
        cancellation_action &operator=(cancellation_action &&n) noexcept;
        cancellation_action (const cancellation_action &n);
        cancellation_action &operator=(const cancellation_action &n);
        ~cancellation_action();
        explicit operator bool () const;
        cancellation_action &operator=(nullptr_t);
        void reset (std::shared_ptr<async::context> ctx);
        void handle_ready (std::move_only_function<void()> callback);
        void set_cancel_callback (std::move_only_function<manapi::future<>()> callback);
        manapi::future<> cancel ();
        void sync_cancel ();
        void ready ();
        void ask_cancel_callback ();
        void ask_timeout ();
        void timeout (ssize_t timeout);
        [[nodiscard]] bool contains_cancel_callback () const;
        [[nodiscard]] bool contains_timeout () const;
        [[nodiscard]] ssize_t timeout () const;
        void timeout_struct (manapi::timer timeout_struct_);
        manapi::timer timeout_struct ();
        void timeout_received ();
        void disable_cancellation ();
    private:
        static manapi::future<> stop_timeout_ (std::shared_ptr<data_t> data);
        static manapi::future<> cancel_ (std::shared_ptr<data_t> data);
        std::shared_ptr<data_t> data;
    };
}