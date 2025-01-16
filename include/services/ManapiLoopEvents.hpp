#pragma once

#include <ev++.h>
#include <set>

#include "ManapiAsync.hpp"
#include "ManapiTask.hpp"
#include "ManapiThreadPool.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "async/ManapiAsyncPromise.hpp"
#include "components/Atomic.hpp"

namespace manapi {
    class loop_events {
    public:
        explicit loop_events(std::shared_ptr<threadpool<task>> taskpool);
        ~loop_events();
        manapi::future<> start (std::shared_ptr<loop_events> le);
        void sync_start (std::shared_ptr<loop_events> le);
        void setup_handle_interrupt ();

        manapi::future<> stop ();

        manapi::future<size_t> subscribe_finish (std::function<void()> cb);
        manapi::future<> unsubscribe_finish (const size_t &id);

        ev::loop_ref get_loop();

        std::shared_ptr<ev::io> create_watcher_fd (int fd, int flags, const std::function<void(ev::io &w, int revents)> &callback);
        std::shared_ptr<ev::async> create_watcher_async (const std::function<void(ev::async &w, int revents)> &callback);

        void stop_watcher_fd (ev::io &w);
        void stop_watcher_async (ev::async &w);

        void stop_watcher_fd (std::shared_ptr<ev::io> w);
        void stop_watcher_async (std::shared_ptr<ev::async> w);

        future<std::shared_ptr<ev::io>> watch_fd (int fd, int flags, const std::function<void(ev::io &w, int revents)> &callback);
        future<void> unwatch_fd (std::shared_ptr<ev::io> w);

        future<std::shared_ptr<ev::async>> watch_async (const std::function<void(ev::async &w, int revents)> &callback);
        future<void> unwatch_async (std::shared_ptr<ev::async> w);

        future<void> watch_fd (std::shared_ptr<ev::io> w);
        future<void> watch_async (std::shared_ptr<ev::async> w);

        static void interrupt ();
    protected:
        virtual void custom_watcher_fd_async (ev::async &w, int revents);
    private:
        template<class ev_>
        struct custom_watcher_data_t {
            std::shared_ptr<ev_> w;
            std::function<void(ev_ &w, int revents)> cb;
        };

        static std::atomic<bool> interrupted;
        static std::map <size_t, std::shared_ptr<loop_events>> events;
        static void custom_watcher_fd (EV_P_ ev_io *w, int revents);
        static void custom_watcher_async (EV_P_ ev_async *w, int revents);
        static std::mutex stop_mx;

        void _pool(manapi::before_delete lk2, std::shared_ptr<loop_events> le);
        void _call_and_free_on_finish_cb ();
        void stop_pool (async::promise<void>::resolve_t resolve);
        void _async_break_loop (ev::async &watcher, int revents);

        struct adding_watcher_data_t {
            bool flag;
            std::shared_ptr<ev::io> w_io{nullptr};
            std::shared_ptr<ev::async> w_async{nullptr};
        } adding_watcher_data{};
        async::mutex adding_watcher_mx;
        std::shared_ptr <ev::async> adding_watcher_async;
        bool status;
        async::mutex mx;
        ev::dynamic_loop loop;
        std::thread::id loop_thread_id{0};
        std::shared_ptr<threadpool<task>> taskpool;
        std::map <size_t, std::function<void()>> map_finish_cb;
        std::shared_ptr<ev::async> stop_watcher{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};
    };
}
