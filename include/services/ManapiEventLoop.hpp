#pragma once

#include "../ManapiUtils.hpp"
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
#include "../components/ManapiEventStructures.hpp"


#if MANAPIHTTP_CURL_DEPENDENCY
#   include <curl/curl.h>
#endif

namespace manapi::ev {
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

        manapi::future<size_t> subscribe_finish (std::move_only_function<manapi::future<void>()> cb);
        manapi::future<> unsubscribe_finish (std::size_t id);

        manapi::future<std::size_t> subscribe_clean_up (std::move_only_function<void()> cb);
        manapi::future<> unsubscribe_clean_up (std::size_t id);

        manapi::ev::loop_ref loop();

        std::shared_ptr<ev::tcp> create_watcher_tcp_accept (ev::tcp_accept_cb callback);
        std::shared_ptr<ev::tcp> create_watcher_tcp_connection (ev::tcp_connection_cb read);
        std::shared_ptr<ev::udp> create_watcher_udp (ev::udp_cb recv);
        std::shared_ptr<ev::io> create_watcher_fd (fd_t fd, ev::io_cb callback);
        std::shared_ptr<ev::io> create_watcher_socket (socket_t sock, ev::io_cb callback);
        std::shared_ptr<ev::async> create_watcher_async (ev::async_cb callback);
        std::shared_ptr<ev::timer> create_watcher_timer (ev::timer_cb callback);
        std::shared_ptr<ev::prepare> create_watcher_prepare (ev::prepare_cb callback);
        std::shared_ptr<ev::fs> create_watcher_fs (ev::fs_cb callback);

        template<typename T>
        void stop_watcher (T *w) { perror("not implemented"); }

        template<typename T>
        void stop_watcher (std::shared_ptr<T> w) {
            if (!w) { return; }
            this->stop_watcher(w.get());
        }

        void stop_watcher_tcp_accept (std::shared_ptr<ev::tcp> s);
        void stop_watcher_tcp_connection (std::shared_ptr<ev::tcp> s);

        future<std::shared_ptr<ev::fs>> fs_open (std::string path, int flags, int mode, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_write (ev::file file, const void *data, ssize_t size, ev::fs_cb callback, int64_t offset = -1);
        future<std::shared_ptr<ev::fs>> fs_read (ev::file file, void *data, ssize_t size, ev::fs_cb callback, int64_t offset = -1);
        future<std::shared_ptr<ev::fs>> fs_close (ev::file file, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_fstat (ev::file file, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_unlink (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_mkdir (std::string path, int mode, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_mkdtemp (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_mkstemp (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_rmdir (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_opendir (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_closedir (ev::dir_t *dir, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_readdir (ev::dir_t *dir, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_scandir (std::string path, int flags, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_scandir_next (ev::dirent_t *dirent, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_stat (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_lstat (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_statfs (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_rename (std::string path, std::string new_path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_fsync (ev::file file, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_fdatasync (ev::file file, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_ftruncate (ev::file file, int64_t offset, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_copyfile (std::string path, std::string new_path, int flags, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_sendfile (ev::file outfd, ev::file infd, int64_t offset, size_t length, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_access (std::string path, int mode, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_chmod (std::string path, int mode, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_fchmod (ev::file, int mode, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_utime (std::string path, double atime, double mtime, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_futime (ev::file, double atime, double mtime, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_lutime (std::string path, double atime, double mtime, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_link (std::string path, std::string new_path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_symlink (std::string path, std::string new_path, int flags, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_readlink (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_realpath (std::string path, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_chown (std::string path, ev::uid_t uid, ev::gid_t gid, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_fchown (ev::file file, ev::uid_t uid, ev::gid_t gid, ev::fs_cb callback);
        future<std::shared_ptr<ev::fs>> fs_lchown (std::string path, ev::uid_t uid, ev::gid_t gid, ev::fs_cb callback);

        future<std::shared_ptr<ev::io>> watch_poll (fd_t fd, int flags, ev::io_cb callback);
        future<void> unwatch_poll (std::shared_ptr<ev::io> w);
        future<std::shared_ptr<ev::io>> watch_poll_socket (socket_t sock, int flags, ev::io_cb callback);
        future<std::shared_ptr<ev::async>> watch_async (ev::async_cb callback);
        future<void> unwatch_async (std::shared_ptr<ev::async> w);
        future<std::shared_ptr<ev::timer>> watch_timer (uint64_t delay, uint64_t repeat, ev::timer_cb cb);
        future<void> unwatch_timer (std::shared_ptr<ev::timer> w);
        future<void> again_timer (uint64_t repeat, std::shared_ptr<ev::timer> w);

        [[nodiscard]] std::shared_ptr<threadpool<task>> taskpool () const;
#if MANAPIHTTP_CURL_DEPENDENCY
        future<void> watch_curl (std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb);
        future<void> unwatch_curl (std::shared_ptr<CURL> curl);
        future<void> unpause_watch_curl (std::shared_ptr<CURL> curl);
        future<void> pause_watch_curl (std::shared_ptr<CURL> curl);
        future<void> custom_cb_curl (std::shared_ptr<CURL> curl, std::move_only_function<void(CURLcode result)> cb);
#endif
        future<void> custom_callback (std::move_only_function<void(event_loop *ev)> cb);

        future<manapi::timer> append_async_timer (size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb);
        future<manapi::timer> append_sync_timer (size_t time, std::move_only_function<void(manapi::timer t)> cb);
        future<manapi::timer> append_async_interval (size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb);
        future<manapi::timer> append_sync_interval (size_t time, std::move_only_function<void(manapi::timer t)> cb);
        future<void> update_state_interval (size_t id);
        future<void> remove_timer (size_t id);

        void callback_io_watcher (std::shared_ptr<ev::io> w, ev::io_cb cb);

        void timer_callback (std::move_only_function<std::optional<manapi::timer>(ev::internal::adding_timerloop_data_t *data)> cb);

        static void interrupt ();
    protected:
        void custom_watcher_fs_async (std::shared_ptr<ev::async> &w);
        void custom_watcher_async_async (std::shared_ptr<ev::async>  &w);
        void custom_watcher_timer_async (std::shared_ptr<ev::async>  &w);
        void custom_watcher_poll_async (std::shared_ptr<ev::async>  &w);
#if MANAPIHTTP_CURL_DEPENDENCY
        void custom_watcher_curl_async (std::shared_ptr<ev::async>  &w);
        void handle_curl_exec_connections ();
        void handle_curl_check_connections ();
#endif
        void custom_watcher_timerloop_async (std::shared_ptr<ev::async> &w);
        void custom_watcher_callback_async (std::shared_ptr<ev::async>  &w);
    private:
        void handle_tasks_do_event (std::shared_ptr<ev::prepare> &w);
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

        void pool_(manapi::before_delete lk2, std::shared_ptr<event_loop> le);
        manapi::future<> _call_on_finish_cb ();
        void free_on_finish_cb_ ();
        void stop_pool (async::promise<void>::resolve_t resolve);
        void async_break_loop_ (std::shared_ptr<ev::async> watcher);

        bool status;
        std::shared_ptr<async::mutex> mx;
        std::shared_ptr<async::mutex> map_finish_cb_mx;
        std::shared_ptr<async::mutex> map_clean_up_cb_mx;
        uv_loop_t loop_;
        std::shared_ptr<threadpool<task>> taskpool_;
        std::map <size_t, std::move_only_function<manapi::future<void>()>> map_finish_cb;
        std::map <size_t, std::move_only_function<void()>> map_clean_up_cb;
        std::shared_ptr<ev::async> interrupted_watcher_{nullptr};
        std::shared_ptr<ev::async> stop_watcher_{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};
        std::atomic<bool> loop_interrupted;

        std::unique_ptr<ev::internal::fs_watcher_t> fs_watcher;
        std::unique_ptr<ev::internal::io_watcher_t> io_watcher;
        std::unique_ptr<ev::internal::async_watcher_t> async_watcher;
        std::unique_ptr<ev::internal::timer_watcher_t> timer_watcher;
#if MANAPIHTTP_CURL_DEPENDENCY
        std::unique_ptr<ev::internal::curl_watcher_t> curl_watcher;
#endif
        std::unique_ptr<ev::internal::timerloop_t> timerloop;
        std::unique_ptr<ev::internal::custom_callback_t> callback_watcher_{};
        std::shared_ptr<ev::prepare> do_tasks_watcher;
        std::shared_ptr<manapi::logger> logger_;
    };
}
