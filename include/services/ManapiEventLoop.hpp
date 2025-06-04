#pragma once

#include <set>
#include <stack>

#include "../ManapiUtils.hpp"
#if MANAPIHTTP_CURL_DEPENDENCY
#   include <curl/curl.h>
#endif

#include "../ManapiInt.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiTask.hpp"
#include "./ManapiThreadPool.hpp"
#include "../async/ManapiAsyncMutex.hpp"
#include "../async/ManapiAsyncPromise.hpp"
#include "../components/Atomic.hpp"
#include "../components/TimerObject.hpp"
#include "../components/ManapiEventStructures.hpp"



#ifdef _WIN32
#   pragma comment(lib, "crypt32")
#   pragma comment(lib, "ws2_32.lib")
#   pragma comment(lib, "wldap32.lib")
#endif

namespace manapi::ev {
    typedef std::move_only_function<void(std::shared_ptr<ev::tcp> &, size_t suggested_size, ev::buff_t* buf)> tcp_alloc_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::udp> &, size_t suggested_size, ev::buff_t* buf)> udp_alloc_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::async> &)> async_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::tcp> &w, int status)> tcp_accept_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::timer> &)> timer_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::prepare> &)> prepare_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::check> &)> check_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::idle> &)> idle_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::io> &, int status, int revents)> io_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::tcp> &, ssize_t nread, const uv_buf_t *buf)> tcp_connection_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::udp> &, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags)> udp_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::udp_send> &, int status)> udp_send_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::write> &, int status)> write_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::fs> &)> fs_cb;
    typedef std::move_only_function<void(std::shared_ptr<ev::random> &w, int status, void *buff, std::size_t size)> random_cb;

    template<typename T>
    using close_cb_t = std::move_only_function<void(const std::shared_ptr<T> &)>;
}

namespace manapi::ev::internal {
#if MANAPIHTTP_CURL_DEPENDENCY
    struct curl_watcher_data_cached_t;
#endif
    struct async_watcher_t;
#if MANAPIHTTP_CURL_DEPENDENCY
    struct curl_res_value_t;
    struct curl_watcher_t;
    struct adding_curl_data_t;
#endif
    struct timer_watcher_t;
    struct fs_watcher_t;
    struct io_watcher_t;
    struct custom_callback_t;
    struct timerloop_t;

    struct adding_timerloop_data_t {
        int flag{0};
        size_t data{0};
        std::move_only_function<void(manapi::timer t)> sync_cb{nullptr};
        std::move_only_function<manapi::future<>(manapi::timer t)> async_cb{nullptr};
        manapi::async::promise<std::optional<manapi::timer>>::resolve_t resolve{nullptr};
        manapi::async::promise<std::optional<manapi::timer>>::reject_t reject{nullptr};
    };
}

namespace manapi {
    class event_loop {
    public:
        explicit event_loop(std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::logger> logger);
        ~event_loop();
        manapi::future<> start (std::shared_ptr<event_loop> le);
        void sync_start (std::shared_ptr<event_loop> le);
        void setup_handle_interrupt ();

        manapi::future<> stop ();

        void wait ();

        size_t subscribe_finish (std::move_only_function<manapi::future<void>()> cb);
        void unsubscribe_finish (std::size_t id);

        std::size_t subscribe_clean_up (std::move_only_function<void()> cb);
        void unsubscribe_clean_up (std::size_t id);

        manapi::ev::loop_ref loop();

        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::tcp> create_watcher_tcp_accept (ev::tcp_accept_cb callback);
        /**
         *
         * @param read Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::tcp> create_watcher_tcp_connection (ev::tcp_connection_cb read, ev::tcp_alloc_cb alloc_cb);
        /**
         *
         * @param recv Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::udp> create_watcher_udp (ev::udp_cb recv, ev::udp_alloc_cb alloc_cb);
        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::io> create_watcher_fd (int fd, ev::io_cb callback);

        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::idle> create_watcher_idle (ev::idle_cb callback);

        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::io> create_watcher_socket (socket_t sock, ev::io_cb callback);
        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::async> create_watcher_async (ev::async_cb callback);
        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::timer> create_watcher_timer (ev::timer_cb callback);
        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::prepare> create_watcher_prepare (ev::prepare_cb callback);
        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::fs> create_watcher_fs (ev::fs_cb callback);

        /**
         *
         * @param callback Callback
         * @return
         * @throws manapi::exception with ERR_WATCHER_BIND code
         */
        std::shared_ptr<ev::random> create_watcher_random (ev::random_cb callback, char *buff, std::size_t size);

        /**
         *
         * @param conn TCP connection
         * @param callback Callback
         * @param bufs
         * @param nbuf
         * @throws manapi::exception with ERR_WATCHER_BIND code
         * @return
         */
        std::shared_ptr<ev::write> create_watcher_write (ev::tcp *conn, ev::write_cb callback, const ev::buff_t *bufs, uint32_t nbuf);

        void stop_watcher_ptr (ev::io *w);
        void stop_watcher_ptr (ev::async *w);
        void stop_watcher_ptr (ev::idle *w);
        void stop_watcher_ptr (ev::udp *w);
        void stop_watcher_ptr (ev::check *w);
        void stop_watcher_ptr (ev::timer *w);
        void stop_watcher_ptr (ev::prepare *w);
        void stop_watcher_ptr (ev::write *w);
        void stop_watcher_ptr (ev::random *w);
        void stop_watcher_ptr (ev::udp_send *w);
        void stop_watcher_ptr (ev::fs *w);
        
        template<typename T>
        void stop_watcher (std::shared_ptr<T> w) {
            this->stop_watcher_ptr(w.get());
        }

        void stop_callback (const std::shared_ptr<ev::tcp> &s, ev::close_cb_t<ev::tcp> cb);
        
        void stop_callback (const std::shared_ptr<ev::udp> &s, ev::close_cb_t<ev::udp> cb);

        void stop_watcher_tcp_accept (std::shared_ptr<ev::tcp> s);
        void stop_watcher_tcp_connection (std::shared_ptr<ev::tcp> s);

        [[nodiscard]] const std::shared_ptr<threadpool<task>> &taskpool () const;
#if MANAPIHTTP_CURL_DEPENDENCY
        void watch_curl (std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb);
        void unwatch_curl (std::shared_ptr<CURL> curl);
        void unpause_watch_curl (std::shared_ptr<CURL> curl);
        void pause_watch_curl (std::shared_ptr<CURL> curl);
#endif
        future<void> custom_callback (std::move_only_function<void(event_loop *ev)> cb);

        static void interrupt ();
    protected:
#if MANAPIHTTP_CURL_DEPENDENCY


        void handle_curl_exec_connections ();

        void handle_curl_check_connections ();
#endif

        void custom_watcher_callback_async (std::shared_ptr<ev::async>  &w);
    private:
#if MANAPIHTTP_CURL_DEPENDENCY
        static std::shared_ptr<ev::io> handle_curl_watcher_gen(event_loop *data, socket_t fd);

        static curl_socket_t handle_curl_open_socket (void *cbp, curlsocktype type, curl_sockaddr *addr);

        static_assert(ev::READ == CURL_POLL_IN && ev::WRITE == CURL_POLL_OUT, "need for review");

        static int handle_curl_socket (CURL *curl, curl_socket_t fd, int revents, void *userp, void *);

        static int handle_curl_close_socket (void *cbp, curl_socket_t socket);

        void handle_curl_watcher_data(std::unique_ptr<ev::internal::adding_curl_data_t> data);
#endif
        future<std::optional<manapi::timer>> template_cmd_timer_ (int flag, size_t data, std::move_only_function<manapi::future<>(manapi::timer t)> cb_async, std::move_only_function<void(manapi::timer t)> cb_sync);

        static std::atomic<bool> interrupted;

        static std::map <size_t, std::shared_ptr<event_loop>> events;

        static std::mutex stop_mx;

        void pool_(manapi::sbefore_delete lk2, std::shared_ptr<event_loop> le);

        manapi::future<> _call_on_finish_cb ();

        void free_on_finish_cb_ ();

        void stop_pool (async::promise<void>::resolve_t resolve);

        void async_break_loop_ ();

        void try_tasks_ (ev::shared_idle &w);

        bool status;

        std::shared_ptr<threadpool<task>> etaskpool_;
        std::shared_ptr<async::mutex> mx;
        std::unique_ptr<uv_loop_t> loop_;
        std::shared_ptr<threadpool<task>> taskpool_;
        std::map <size_t, std::move_only_function<manapi::future<void>()>> map_finish_cb;
        std::map <size_t, std::move_only_function<void()>> map_clean_up_cb;
        std::shared_ptr<ev::async> interrupted_watcher_{nullptr};
        std::shared_ptr<ev::async> stop_watcher_{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};

#if MANAPIHTTP_CURL_DEPENDENCY
        std::unique_ptr<ev::internal::curl_watcher_t> curl_watcher;
#endif

        std::unique_ptr<ev::internal::custom_callback_t> callback_watcher_{};
        std::shared_ptr<ev::idle> idle_tasks_;
        std::shared_ptr<manapi::logger> logger_;
    };
}
