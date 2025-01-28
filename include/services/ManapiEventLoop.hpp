#pragma once

#include "extensions/ev++.h"
#include <set>
#include <stack>
#include <curl/curl.h>
#include "ManapiInt.hpp"

#include "ManapiAsync.hpp"
#include "ManapiTask.hpp"
#include "ManapiThreadPool.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "async/ManapiAsyncPromise.hpp"
#include "components/Atomic.hpp"

namespace manapi {
    namespace priority {
        constexpr int important = 2;
        constexpr int necessary = 1;
        constexpr int irrevelant = 0;
        constexpr int lowcapacity = -1;
        constexpr int noneed = -2;

        constexpr int onaccept = important;
        constexpr int timeout_timer = noneed;
        constexpr int oncurl = irrevelant;
    }

    class event_loop {
        struct curl_multi_deleter {
            void operator()(CURLM *curl_multi) noexcept {
                curl_multi_cleanup(curl_multi);
            }
        };

        template<class ev_>
        struct custom_watcher_data_t {
            std::shared_ptr<ev_> w;
            std::function<void(ev_ &w, int revents)> cb;
        };

        struct adding_watcher_data_t {
            int flag{0};
            int type{EV_IO};
            std::shared_ptr<ev::io> w_io{nullptr};
            std::shared_ptr<ev::async> w_async{nullptr};
            std::shared_ptr<ev::timer> w_timer{nullptr};
            async::promise<void>::resolve_t resolve{nullptr};
        };
        struct adding_curl_data_t {
            int flag{0};
            CURL* curl{nullptr};
            std::function<void(CURLcode result)> finish{nullptr};
            async::promise<void>::resolve_t resolve{nullptr};
            async::promise<void>::reject_t reject{nullptr};
        };
        struct async_watcher_t {
            std::deque<adding_watcher_data_t> adding_watcher_data{};
            std::shared_ptr<async::mutex> adding_watcher_mx;
            std::shared_ptr <ev::async> adding_watcher_async;
            std::function<void()> adding_watcher_async_cb{nullptr};
        };
        struct curl_watcher_t {
            std::unique_ptr<CURLM, curl_multi_deleter> curl_multi{nullptr};
            std::shared_ptr<async::mutex> curl_multi_mx{nullptr};
            std::shared_ptr<ev::async> adding_curl_multi_async{nullptr};
            std::deque<adding_curl_data_t> adding_curl_data{};

            std::function<void()> adding_curl_async_cb{nullptr};
            std::queue<std::shared_ptr<ev::io>> curl_fds{};
            std::map<CURL*, std::function<void(CURLcode result)>> curl_res{};
            std::shared_ptr<ev::timer> timeout_watcher{nullptr};
        };
    public:
        explicit event_loop(std::shared_ptr<threadpool<task>> taskpool);
        ~event_loop();
        manapi::future<> start (std::shared_ptr<event_loop> le);
        void sync_start (std::shared_ptr<event_loop> le);
        void setup_handle_interrupt ();
        template<typename T1, typename T2>
        static void _ev_custom_watcher(EV_P_ T1 *w, int revents);

        manapi::future<> stop ();

        manapi::future<size_t> subscribe_finish (std::function<manapi::future<void>()> cb);
        manapi::future<> unsubscribe_finish (const size_t &id);

        ev::loop_ref get_loop();

        std::shared_ptr<ev::io> create_watcher_fd (int fd, int flags, std::function<void(ev::io &w, int revents)> callback, int priority = 0);
        std::shared_ptr<ev::async> create_watcher_async (std::function<void(ev::async &w, int revents)> callback);
        std::shared_ptr<ev::timer> create_watcher_timer (const float &after, const int &repeat, std::function<void(ev::timer &w, int revents)> callback);

        template<typename T>
        void stop_watcher (T &w);

        template<typename T>
        void stop_watcher (std::shared_ptr<T> w);

        future<std::shared_ptr<ev::io>> watch_fd (int fd, int flags, std::function<void(ev::io &w, int revents)> callback, int priority = 0);
        future<void> unwatch_fd (std::shared_ptr<ev::io> w);

        future<std::shared_ptr<ev::async>> watch_async (std::function<void(ev::async &w, int revents)> callback);
        future<void> unwatch_async (std::shared_ptr<ev::async> w);
        future<void> unwatch_timer (std::shared_ptr<ev::timer> w);

        future<void> watch_fd (std::shared_ptr<ev::io> w);
        future<void> watch_async (std::shared_ptr<ev::async> w);
        future<void> watch_timer (std::shared_ptr<ev::timer> w);

        future<void> again_timer (std::shared_ptr<ev::timer> w);

        [[nodiscard]] std::shared_ptr<threadpool<task>> get_task_pool () const;

        future<void> watch_curl (CURL *curl, std::function<void(CURLcode result)> cb);
        future<void> unwatch_curl (CURL *curl);
        future<void> unpause_watch_curl (CURL *curl);
        future<void> pause_watch_curl (CURL *curl);
        future<void> custom_cb_curl (CURL *curl, std::function<void(CURLcode result)> cb);

        static void interrupt ();
    protected:
        void custom_watcher_fd_async (ev::async &w, int revents);
        void custom_watcher_curl_async (ev::async &w, int revents);
    private:
        future<void> _template_cmd_curl (int flag, CURL *curl, std::function<void(CURLcode result)> cb = nullptr);
        static std::atomic<bool> interrupted;
        static std::map <size_t, std::shared_ptr<event_loop>> events;
        static std::mutex stop_mx;

        future<> _fix_event_pool_interrupt();
        void _pool(manapi::before_delete lk2, std::shared_ptr<event_loop> le);
        manapi::future<> _call_and_free_on_finish_cb ();
        void stop_pool (async::promise<void>::resolve_t resolve);
        void _async_break_loop (ev::async &watcher, int revents);

        bool status;
        std::shared_ptr<async::mutex> mx;
        ev::dynamic_loop loop;
#ifdef _WIN32
        uint32_t loop_thread_id{0};
#else
        std::thread::id loop_thread_id{0};
#endif
        std::shared_ptr<threadpool<task>> taskpool;
        std::map <size_t, std::function<manapi::future<void>()>> map_finish_cb;
        std::shared_ptr<ev::async> _stop_watcher{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};
        std::atomic<bool> loop_interrupted;
        async_watcher_t async_watcher{};
        curl_watcher_t curl_watcher{};
    };
}
