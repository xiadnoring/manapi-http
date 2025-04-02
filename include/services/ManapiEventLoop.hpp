#pragma once

#include "../ManapiUtils.hpp"
#include "../extensions/ev++.h"
#include <set>
#include <stack>
#include "../ManapiInt.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiTask.hpp"
#include "./ManapiThreadPool.hpp"
#include "../async/ManapiAsyncMutex.hpp"
#include "../async/ManapiAsyncPromise.hpp"
#include "../components/Atomic.hpp"
#include "../components/TimerObject.hpp"

#if MANAPIHTTP_CURL_DEPENDENCY
#   include <curl/curl.h>
#endif

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

    struct adding_watcher_data_t {
        int flag{0};
        int type{EV_IO};
        std::shared_ptr<ev::io> w_io{nullptr};
        std::shared_ptr<ev::async> w_async{nullptr};
        std::shared_ptr<ev::timer> w_timer{nullptr};
        async::promise<void>::resolve_t resolve{nullptr};
    };
#if MANAPIHTTP_CURL_DEPENDENCY
    struct adding_curl_data_t {
        int flag{0};
        CURL* curl{nullptr};
        std::move_only_function<void(CURLcode result)> finish{nullptr};
        async::promise<void>::resolve_t resolve{nullptr};
        async::promise<void>::reject_t reject{nullptr};
    };
#endif
    struct adding_timer_data_t {
        int flag{0};
        size_t data{0};
        std::move_only_function<void(manapi::timer t)> sync_cb{nullptr};
        std::move_only_function<manapi::future<>(manapi::timer t)> async_cb{nullptr};
        async::promise<std::optional<manapi::timer>>::resolve_t resolve{nullptr};
        async::promise<std::optional<manapi::timer>>::reject_t reject{nullptr};
    };
    struct adding_custom_callback_data_t {
        std::move_only_function<void(event_loop *ev)> cb;
        async::promise<void, std::false_type>::resolve_t resolve{nullptr};
        async::promise<void, std::false_type>::reject_t reject{nullptr};
    };

    class event_loop {
#if MANAPIHTTP_CURL_DEPENDENCY
        struct curl_multi_deleter {
            void operator()(CURLM *curl_multi) noexcept {
                curl_multi_cleanup(curl_multi);
            }
        };
#endif
        template<class ev_>
        struct custom_watcher_data_t {
            std::shared_ptr<ev_> w;
            std::move_only_function<void(ev_ &w, int revents)> cb;
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

        manapi::future<size_t> subscribe_finish (std::move_only_function<manapi::future<void>()> cb);
        manapi::future<> unsubscribe_finish (std::size_t id);

        manapi::future<std::size_t> subscribe_clean_up (std::move_only_function<void()> cb);
        manapi::future<> unsubscribe_clean_up (std::size_t id);

        ev::loop_ref get_loop();

        std::shared_ptr<ev::io> create_watcher_fd (int fd, int flags, std::move_only_function<void(ev::io &w, int revents)> callback, int priority = 0);
        std::shared_ptr<ev::async> create_watcher_async (std::move_only_function<void(ev::async &w, int revents)> callback);
        std::shared_ptr<ev::timer> create_watcher_timer (const float &after, const int &repeat, std::move_only_function<void(ev::timer &w, int revents)> callback);

        template<typename T>
        void stop_watcher (T &w);

        template<typename T>
        void stop_watcher (std::shared_ptr<T> w);

        future<std::shared_ptr<ev::io>> watch_fd (int fd, int flags, std::move_only_function<void(ev::io &w, int revents)> callback, int priority = 0);
        future<void> unwatch_fd (std::shared_ptr<ev::io> w);

        future<std::shared_ptr<ev::async>> watch_async (std::move_only_function<void(ev::async &w, int revents)> callback);
        future<void> unwatch_async (std::shared_ptr<ev::async> w);
        future<void> unwatch_timer (std::shared_ptr<ev::timer> w);

        future<void> watch_fd (std::shared_ptr<ev::io> w);
        future<void> watch_async (std::shared_ptr<ev::async> w);
        future<void> watch_timer (std::shared_ptr<ev::timer> w);

        future<void> again_timer (std::shared_ptr<ev::timer> w);

        [[nodiscard]] std::shared_ptr<threadpool<task>> get_task_pool () const;
#if MANAPIHTTP_CURL_DEPENDENCY
        future<void> watch_curl (CURL *curl, std::move_only_function<void(CURLcode result)> cb);
        future<void> unwatch_curl (CURL *curl);
        future<void> unpause_watch_curl (CURL *curl);
        future<void> pause_watch_curl (CURL *curl);
        future<void> custom_cb_curl (CURL *curl, std::move_only_function<void(CURLcode result)> cb);
#endif
        future<void> custom_callback (std::move_only_function<void(event_loop *ev)> cb);

        future<manapi::timer> append_async_timer (size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb);
        future<manapi::timer> append_sync_timer (size_t time, std::move_only_function<void(manapi::timer t)> cb);
        future<manapi::timer> append_async_interval (size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb);
        future<manapi::timer> append_sync_interval (size_t time, std::move_only_function<void(manapi::timer t)> cb);
        future<void> update_state_interval (size_t id);
        future<void> remove_timer (size_t id);

        void set_timer_callback (std::move_only_function<std::optional<manapi::timer>(adding_timer_data_t data)> cb);

        static void interrupt ();
    protected:
        enum watcher_status {
            WATCHER_STATUS_WAIT = 0x0,
            WATCHER_STATUS_PREPARE = 0b01,
            WATCHER_STATUS_READY = 0b10
        };
        struct async_watcher_data_cached_t {
            std::atomic<int> status{0};
            std::unique_ptr<adding_watcher_data_t> data{nullptr};
        };
        struct timer_watcher_data_cached_t {
            std::atomic<int> status{0};
            std::unique_ptr<adding_timer_data_t> data{nullptr};
        };

        struct callback_watcher_data_cached_t {
            std::atomic<int> status{0};
            std::unique_ptr<adding_custom_callback_data_t> data{nullptr};
        };
#if MANAPIHTTP_CURL_DEPENDENCY
        struct curl_watcher_data_cached_t {
            std::atomic<int> status{0};
            std::unique_ptr<adding_curl_data_t> data{nullptr};
        };
#endif
        struct async_watcher_t {
            manapi::chain<std::unique_ptr<adding_watcher_data_t>> adding_watcher_data{};
            std::shared_ptr<async::mutex> adding_watcher_mx;
            std::shared_ptr <ev::async> adding_watcher_async;
            std::move_only_function<void()> adding_watcher_async_cb{nullptr};
            std::deque<async_watcher_data_cached_t> watcher_data_cached{};
        };
#if MANAPIHTTP_CURL_DEPENDENCY
        struct curl_watcher_t {
            std::unique_ptr<CURLM, curl_multi_deleter> curl_multi{nullptr};
            std::shared_ptr<async::mutex> curl_multi_mx{nullptr};
            std::shared_ptr<ev::async> adding_curl_multi_async{nullptr};
            manapi::chain<std::unique_ptr<adding_curl_data_t>> adding_curl_data{};

            std::move_only_function<void()> adding_curl_async_cb{nullptr};
            std::queue<std::shared_ptr<ev::io>> curl_fds{};
            std::map<CURL*, std::move_only_function<void(CURLcode result)>> curl_res{};
            std::shared_ptr<ev::timer> timeout_watcher{nullptr};
            std::deque<curl_watcher_data_cached_t> watcher_data_cached{};
        };
#endif
        struct timer_watcher_t {
            manapi::chain<std::unique_ptr<adding_timer_data_t>> adding_timer_data{};
            std::shared_ptr<async::mutex> adding_timer_mx;
            std::shared_ptr <ev::async> adding_timer_async;
            std::move_only_function<void()> adding_timer_async_cb{nullptr};
            std::move_only_function<std::optional<manapi::timer>(adding_timer_data_t data)> external_cb;
            std::deque<timer_watcher_data_cached_t> watcher_data_cached{};
        };
        struct custom_callback_t {
            manapi::chain<std::unique_ptr<adding_custom_callback_data_t>> callback_data{};
            std::shared_ptr<async::mutex> adding_mx;
            std::shared_ptr <ev::async> adding_async;
            std::move_only_function<void()> adding_async_cb{nullptr};
            std::deque<callback_watcher_data_cached_t> watcher_data_cached{};
        };

        void custom_watcher_fd_async (ev::async &w, int revents);
#if MANAPIHTTP_CURL_DEPENDENCY
        void custom_watcher_curl_async (ev::async &w, int revents);
#endif
        void custom_watcher_timer_async (ev::async &w, int revents);
        void custom_watcher_callback_async (ev::async &w, int revents);
    private:
        void handle_tasks_do_event (ev::prepare &w, int revents);
#if MANAPIHTTP_CURL_DEPENDENCY
        void handle_curl_watcher_data (std::unique_ptr<adding_curl_data_t> data);
#endif
        void handle_async_watcher_data (std::unique_ptr<adding_watcher_data_t> data);
        future<void> _template_cmd_watcher (std::unique_ptr<adding_watcher_data_t> data);
#if MANAPIHTTP_CURL_DEPENDENCY
        future<void> _template_cmd_curl (int flag, CURL *curl, std::move_only_function<void(CURLcode result)> cb = nullptr);
#endif
        future<std::optional<manapi::timer>> _template_cmd_timer (int flag, size_t data, std::move_only_function<manapi::future<>(manapi::timer t)> cb_async, std::move_only_function<void(manapi::timer t)> cb_sync);
        static std::atomic<bool> interrupted;
        static std::map <size_t, std::shared_ptr<event_loop>> events;
        static std::mutex stop_mx;

        void _pool(manapi::before_delete lk2, std::shared_ptr<event_loop> le);
        manapi::future<> _call_on_finish_cb ();
        void _free_on_finish_cb ();
        void stop_pool (async::promise<void>::resolve_t resolve);
        void _async_break_loop (ev::async &watcher, int revents);

        bool status;
        std::shared_ptr<async::mutex> mx;
        std::shared_ptr<async::mutex> map_finish_cb_mx;
        std::shared_ptr<async::mutex> map_clean_up_cb_mx;
        ev::dynamic_loop loop;
#ifdef _WIN32
        uint32_t loop_thread_id{0};
#else
        std::thread::id loop_thread_id{0};
#endif
        std::shared_ptr<threadpool<task>> taskpool;
        std::map <size_t, std::move_only_function<manapi::future<void>()>> map_finish_cb;
        std::map <size_t, std::move_only_function<void()>> map_clean_up_cb;
        std::shared_ptr<ev::async> interrupted_watcher_{nullptr};
        std::shared_ptr<ev::async> stop_watcher_{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};
        std::atomic<bool> loop_interrupte1d;
        async_watcher_t async_watcher{};
#if MANAPIHTTP_CURL_DEPENDENCY
        curl_watcher_t curl_watcher{};
#endif
        timer_watcher_t timer_watcher{};
        custom_callback_t callback_watcher{};
        ev::prepare prepare_watcher;
    };
}
