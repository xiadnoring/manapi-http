#include <csignal>

#include "services/ManapiEventLoop.hpp"

#include <memory>

#include "components/TimerObject.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

std::map<size_t, std::shared_ptr<manapi::event_loop>> manapi::event_loop::events = {};
std::atomic<bool> manapi::event_loop::interrupted = false;
std::mutex manapi::event_loop::stop_mx;


template<typename T1, typename T2>
void manapi::event_loop::_ev_custom_watcher(EV_P_ T1 *w, int revents) {
    auto &data = *static_cast<manapi::event_loop::custom_watcher_data_t<T2> *> (w->data);
    data.cb(*data.w, revents);
}

void handler_interrupt (int sig) {
    manapi::event_loop::interrupt();
}

manapi::event_loop::event_loop(std::shared_ptr<threadpool<task>> taskpool) {
    this->mx = std::make_shared<async::mutex>(taskpool);
    this->curl_watcher.curl_multi_mx = std::make_shared<async::mutex>(taskpool);
    this->async_watcher.adding_watcher_mx = std::make_shared<async::mutex>(taskpool);
    this->timer_watcher.adding_timer_mx = std::make_shared<async::mutex>(taskpool);
    this->callback_watcher.adding_mx = std::make_shared<async::mutex>(taskpool);
    this->taskpool = std::move(taskpool);
    this->async_watcher.adding_watcher_data = {};
    this->curl_watcher.curl_multi.reset(curl_multi_init());
    this->loop_interrupted = false;
    this->status = false;

    this->async_watcher.adding_watcher_async = this->create_watcher_async([this] (ev::async &w, int revents)
        -> void { this->custom_watcher_fd_async(w, revents); });
    this->async_watcher.adding_watcher_async->priority = priority::important;

    this->curl_watcher.adding_curl_multi_async = this->create_watcher_async([this] (ev::async &w, int revents)
        -> void { this->custom_watcher_curl_async(w, revents); });
    this->curl_watcher.adding_curl_multi_async->priority = priority::oncurl;

    this->timer_watcher.adding_timer_async = this->create_watcher_async([this] (ev::async &w, int revents)
        -> void { this->custom_watcher_timer_async(w, revents); });
    this->timer_watcher.adding_timer_async->priority = priority::important;

    this->callback_watcher.adding_async = this->create_watcher_async([this] (ev::async &w, int revents)
        -> void { this->custom_watcher_callback_async(w, revents); });

    this->async_watcher.adding_watcher_async_cb = [this] ()
        -> void { this->async_watcher.adding_watcher_async->send(); };
    this->curl_watcher.adding_curl_async_cb = [this] ()
        -> void { this->curl_watcher.adding_curl_multi_async->send(); };
    this->timer_watcher.adding_timer_async_cb = [this] ()
        -> void { this->timer_watcher.adding_timer_async->send(); };
    this->callback_watcher.adding_async_cb = [this] ()
        -> void { this->callback_watcher.adding_async->send(); };

    /* data cached */
    this->async_watcher.watcher_data_cached.resize(8000);
    this->timer_watcher.watcher_data_cached.resize(8000);
    this->callback_watcher.watcher_data_cached.resize(8000);
    this->curl_watcher.watcher_data_cached.resize(8000);

    this->callback_watcher.adding_async->start();
    this->async_watcher.adding_watcher_async->start();
    this->timer_watcher.adding_timer_async->start();
    this->curl_watcher.adding_curl_multi_async->start();

    /* curl fetch timeout */
    this->curl_watcher.timeout_watcher = create_watcher_timer(0.5, 0.0, [this] (ev::timer &w, int revents) -> void {
        this->curl_watcher.adding_curl_multi_async->send();

        w.repeat = 0.5;
        w.again();
    });
    this->curl_watcher.timeout_watcher->start();
}

manapi::event_loop::~event_loop() {
    this->stop()
        .get(this->taskpool);

    this->stop_watcher (this->timer_watcher.adding_timer_async);
    this->stop_watcher (this->curl_watcher.adding_curl_multi_async);
    this->stop_watcher (this->async_watcher.adding_watcher_async);
    this->stop_watcher(this->callback_watcher.adding_async);
}

manapi::future<> manapi::event_loop::start(std::shared_ptr<event_loop> le) {
    auto lk = co_await this->mx->lock_guard();
    if (std::exchange(this->status, true)) {
        co_return;
    }

    this->_pool(std::move(lk), std::move(le));
}

void manapi::event_loop::sync_start(std::shared_ptr<event_loop> le) {
    auto lk = this->mx->lock_guard().get(this->taskpool);
    if (std::exchange(this->status, true)) {
        return;
    }

    this->_pool(std::move(lk), std::move(le));
}

void manapi::event_loop::setup_handle_interrupt() {
#ifdef _WIN32
#else
    signal (SIGPIPE, SIG_IGN);
    signal (SIGKILL, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
#endif
    signal (SIGABRT, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
}

manapi::future<> manapi::event_loop::stop() {
    co_await this->_fix_event_pool_interrupt();
    co_await this->_call_and_free_on_finish_cb();

    auto lk = co_await this->mx->lock_guard();
    if (!std::exchange(this->status, false)) {
        co_return;
    }

    if (this->loop_interrupted) {
        /* in the libev */
        auto promise = async::promise<void> (this->taskpool,
        [this] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> future<> {
            this->stop_pool(resolve);
            co_return;
        });
        co_await promise;
        this->_stop_watcher->send();
    }
    else {
        auto promise = async::promise<void> (this->taskpool,
            [this] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> future<> {
            this->resolve_stop = resolve;
            this->_stop_watcher->send();
            co_return;
        });
        co_await promise;
    }
}

manapi::future<size_t> manapi::event_loop::subscribe_finish(std::move_only_function<manapi::future<void>()> cb) {
    auto lk = co_await this->mx->lock_guard();

    auto id = *reinterpret_cast<const size_t *> (&cb);

    if (!this->map_finish_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_SUBSCRIBE_FAILURE, "index {} exists", id);
    }

    co_return id;
}

manapi::future<void> manapi::event_loop::unsubscribe_finish(const size_t &id) {
    if (!id) { co_return; }
    auto lk = co_await this->mx->lock_guard();
    this->map_finish_cb.erase(id);
}

ev::loop_ref manapi::event_loop::get_loop() {
    return this->loop;
}

manapi::future<> manapi::event_loop::_call_and_free_on_finish_cb() {
    while (!this->map_finish_cb.empty()) {
        const auto it = this->map_finish_cb.begin();
        co_await async::invoke(std::move(it->second));
        //this->map_finish_cb.erase(it);
    }
}

void manapi::event_loop::stop_pool(async::promise<void>::resolve_t resolve) {
    {
        std::lock_guard <std::mutex> lk (event_loop::stop_mx);
        event_loop::events.erase(reinterpret_cast<size_t>(this));
    }

    resolve();
}

void manapi::event_loop::_async_break_loop(ev::async &watcher, int revents) {
    this->loop.break_loop(ev::ALL);

    if (this->resolve_stop) {
        /* if resolve caballback exists, break the loop otherwise */
        this->stop_pool(std::exchange(this->resolve_stop, nullptr));
    }
}

void manapi::event_loop::custom_watcher_fd_async(ev::async &w, int revents) {
    for (auto &row : this->async_watcher.watcher_data_cached) {
        if (row.status & WATCHER_STATUS_READY) {
            std::unique_ptr<manapi::adding_watcher_data_t>  data = std::move(row.data);

            // update status
            row.status.store(WATCHER_STATUS_WAIT);

            this->handle_async_watcher_data(std::move(data));
        }
    }

    if (this->async_watcher.adding_watcher_mx->try_to_lock()) {
        while (!this->async_watcher.adding_watcher_data.empty()) {
            std::unique_ptr<manapi::adding_watcher_data_t> data = std::move(this->async_watcher.adding_watcher_data.front());
            this->async_watcher.adding_watcher_data.pop_front();

            this->handle_async_watcher_data (std::move(data));
        }

        this->async_watcher.adding_watcher_mx->unlock();
    }
}

manapi::future<> manapi::event_loop::custom_callback(std::move_only_function<void()> cb) {

    co_await async::promise<void> (this->taskpool, [&](async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> manapi::future<> {
        auto data = std::make_unique<adding_custom_callback_data_t>(std::move(cb), std::move(resolve), std::move(reject));
        bool flg = true;

        for (auto &row : this->callback_watcher.watcher_data_cached) {
            if ((row.status.fetch_or(WATCHER_STATUS_PREPARE) & WATCHER_STATUS_PREPARE) == 0) {
                // row was activated
                row.data = std::move(data);
                row.status.fetch_or(WATCHER_STATUS_READY);

                flg = false;
                break;
            }
        }

        if (flg) {
            auto lk = co_await this->callback_watcher.adding_mx->lock_guard();
            this->callback_watcher.callback_data.push_back(std::move(data));
        }

        this->callback_watcher.adding_async_cb();
    });
}

void manapi::event_loop::custom_watcher_curl_async(ev::async &w, int revents) {
    while (!this->curl_watcher.curl_fds.empty()) {
        this->stop_watcher(std::move(this->curl_watcher.curl_fds.front()));
        this->curl_watcher.curl_fds.pop();
    }


    for (auto &row : this->curl_watcher.watcher_data_cached) {
        if (row.status & WATCHER_STATUS_READY) {
            std::unique_ptr<manapi::adding_curl_data_t> data = std::move(row.data);

            // update status
            row.status.store(WATCHER_STATUS_WAIT);

            this->handle_curl_watcher_data(std::move(data));
        }
    }

    if (this->curl_watcher.curl_multi_mx->try_to_lock()) {
        while (!this->curl_watcher.adding_curl_data.empty()) {
            this->handle_curl_watcher_data(std::move(this->curl_watcher.adding_curl_data.front()));
            this->curl_watcher.adding_curl_data.pop_front();
        }

        this->curl_watcher.curl_multi_mx->unlock();
    }

    int running_handles = 0;
    auto mcode = curl_multi_perform(this->curl_watcher.curl_multi.get(), &running_handles);

    if (mcode != CURLM_OK) {
        return;
    }

    fd_set fd_read;
    fd_set fd_write;
    fd_set fd_exc;

    FD_ZERO(&fd_read);
    FD_ZERO(&fd_write);
    FD_ZERO(&fd_exc);

    int maxfd = -1;

    if (running_handles) {
        mcode = curl_multi_fdset(this->curl_watcher.curl_multi.get(), &fd_read, &fd_write, &fd_exc, &maxfd);

        if (mcode != CURLM_OK) {
            return;
        }
    }

    int msgs_left;
    CURLMsg *msg;
    while (true) {
        msg = curl_multi_info_read(this->curl_watcher.curl_multi.get(), &msgs_left);

        if (!msg) {
            break;
        }

        if (msg->msg == CURLMSG_DONE) {
            curl_multi_remove_handle(this->curl_watcher.curl_multi.get(), msg->easy_handle);
            auto data = this->curl_watcher.curl_res.extract(msg->easy_handle);
            if(data.empty()) {
                continue;
            }
            this->taskpool->append_task([result = msg->data.result, resolve = std::move(data.mapped())] () mutable
                -> void { resolve(result); });
        }
    }

    for (int i = 0; i <= maxfd; i++) {
        int flag = 0;
        if (FD_ISSET(i, &fd_read)) {
            flag |= ev::READ;
        }

        if (FD_ISSET(i, &fd_write)) {
            flag |= ev::WRITE;
        }
        if (flag) {
            this->curl_watcher.curl_fds.push(this->create_watcher_fd(i, flag, [wloop = this->curl_watcher.adding_curl_multi_async] (ev::io &w, int revents)
                -> void {
                wloop->send();
            }));

            this->curl_watcher.curl_fds.front()->start();
        }
    }
}

void manapi::event_loop::custom_watcher_timer_async(ev::async &w, int revents) {
    for (auto &row : this->timer_watcher.watcher_data_cached) {
        if (row.status & WATCHER_STATUS_READY) {
            std::unique_ptr<manapi::adding_timer_data_t> data = std::move(row.data);

            // update status
            row.status.store(WATCHER_STATUS_WAIT);

            auto resolve = std::move(data->resolve);
            auto res = this->timer_watcher.external_cb(std::move(*data));
            this->taskpool->append_task([res = std::move(res), resolve = std::move(resolve)] () mutable
                -> void { resolve (std::move(res)); });
        }
    }

    if (this->timer_watcher.adding_timer_mx->try_to_lock()) {
        while (!this->timer_watcher.adding_timer_data.empty()) {
            std::unique_ptr<manapi::adding_timer_data_t> data = std::move(this->timer_watcher.adding_timer_data.front ());
            this->timer_watcher.adding_timer_data.pop_front();

            auto resolve = std::move(data->resolve);
            auto res = this->timer_watcher.external_cb(std::move(*data));
            this->taskpool->append_task([res = std::move(res), resolve = std::move(resolve)] () mutable
                -> void { resolve (std::move(res)); });
        }

        this->timer_watcher.adding_timer_mx->unlock();
    }
}

void manapi::event_loop::custom_watcher_callback_async(ev::async &w, int revents) {
    for (auto &row : this->callback_watcher.watcher_data_cached) {
        if (row.status & WATCHER_STATUS_READY) {
            std::unique_ptr<manapi::adding_custom_callback_data_t> data = std::move(row.data);

            // update status
            row.status.store(WATCHER_STATUS_WAIT);

            try {
                data->cb();
            }
            catch (...) {
                this->taskpool->append_task([reject = std::move(data->reject), err = std::current_exception()] () mutable
                    -> void { reject(std::move(err)); });

                continue;
            }

            this->taskpool->append_task(std::move(data->resolve));
        }
    }

    if (this->callback_watcher.adding_mx->try_to_lock()) {
        while (!this->callback_watcher.callback_data.empty()) {
            auto data = std::move(this->callback_watcher.callback_data.back());
            this->callback_watcher.callback_data.pop_back();

            try {
                data->cb();
            }
            catch (...) {
                this->taskpool->append_task([reject = std::move(data->reject), err = std::current_exception()] () mutable
                    -> void { reject(std::move(err)); });

                continue;
            }

            this->taskpool->append_task(std::move(data->resolve));
        }

        this->callback_watcher.adding_mx->unlock();
    }
}

void manapi::event_loop::handle_curl_watcher_data(std::unique_ptr<adding_curl_data_t> data) {
    CURLMcode mcode;

    if (data->flag==0) {
        /* add */
        mcode = curl_multi_add_handle(this->curl_watcher.curl_multi.get(), data->curl);

        if (mcode != CURLM_OK) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "Failed to add the curl handle. curl_multi_add_handle(...) = {}", static_cast<int>(mcode)));
            this->taskpool->append_task([reject = std::move(data->reject), err = std::move(err)] () mutable
                -> void { reject(std::move(err)); });

        }
        else {
            this->curl_watcher.curl_res.insert({data->curl, std::move(data->finish)});

            this->taskpool->append_task(std::move(data->resolve));
        }
    }
    else if (data->flag==1) {
        /* remove */
        auto curl_data = this->curl_watcher.curl_res.extract(data->curl);
        if (curl_data.empty()) {
            return;
        }

        mcode = curl_multi_remove_handle(this->curl_watcher.curl_multi.get(), data->curl);

        if (mcode != CURLM_OK) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "Failed to remove the curl handle. curl_multi_remove_handle(...) = {}", static_cast<int>(mcode)));
            this->taskpool->append_task([reject = std::move(data->reject), err = std::move(err)] () mutable
                -> void { reject(std::move(err)); });
        }
        else {
            this->taskpool->append_task([resolve1 = std::move(data->resolve), resolve2 = std::move(curl_data.mapped())] () mutable
                -> void { resolve1 (); resolve2(CURLE_OPERATION_TIMEDOUT); });
        }
    }
    else if (data->flag==2) {
        /* pause */
        const auto rhs = curl_easy_pause(data->curl, CURLPAUSE_ALL);
        if (CURLE_OK != rhs) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "curl_easy_pause(...) with CURLPAUSE_ALL failed -> {}", static_cast<int>(rhs)));
            this->taskpool->append_task([reject = std::move(data->reject), err = std::move(err)] () mutable
                -> void { reject(std::move(err)); });
        }
        else {
            this->taskpool->append_task(std::move(data->resolve));
        }
    }
    else if (data->flag==3) {
        /* unpause */
        const auto rhs = curl_easy_pause(data->curl, CURLPAUSE_CONT);
        if (CURLE_OK != rhs) {
            auto err = std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH,
                "curl_easy_pause(...) with CURLPAUSE_CONT failed -> {}", static_cast<int>(rhs)));
            this->taskpool->append_task([reject = std::move(data->reject), err = std::move(err)] mutable
                -> void { reject(std::move(err)); });
        }
        else {
            this->taskpool->append_task(std::move(data->resolve));
        }
    }
    else if (data->flag==4) {
        /* callback */
        data->finish (CURLE_OK);
    }
}

void manapi::event_loop::handle_async_watcher_data(std::unique_ptr<adding_watcher_data_t> data) {
    if (data->flag == 1) {
        switch (data->type) {
            case EV_IO:
                data->w_io->start();
            break;
            case EV_ASYNC:
                data->w_async->start();
            data->w_async->send();
            break;
            case EV_TIMER:
                data->w_timer->start();
            break;
            default:
                break;
        }
    }
    else if (data->flag == 2) {
        switch (data->type) {
            case EV_TIMER:
                data->w_timer->again();
            break;
            default:
                break;
        }
    }
    else {
        switch (data->type) {
            case EV_IO:
                this->stop_watcher(std::move(data->w_io));
            break;
            case EV_ASYNC:
                this->stop_watcher(std::move(data->w_async));
            break;
            case EV_TIMER:
                this->stop_watcher(std::move(data->w_timer));
            break;
            default:
                break;
        }
    }

    if (data->resolve) {
        this->taskpool->append_task(std::move(data->resolve));
    }
}

manapi::future<> manapi::event_loop::_template_cmd_watcher(std::unique_ptr<adding_watcher_data_t> data) {
    co_await async::promise<void> (this->taskpool, [this, data = std::move(data)] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) mutable -> future<void> {
        data->resolve = std::move(resolve);

        bool flg = true;

        for (auto &row : this->async_watcher.watcher_data_cached) {
            if ((row.status.fetch_or(WATCHER_STATUS_PREPARE) & WATCHER_STATUS_PREPARE) == 0) {
                // row was activated
                row.data = std::move(data);
                row.status.fetch_or(WATCHER_STATUS_READY);

                flg = false;
                break;
            }
        }

        if (flg) {
            auto lk = co_await this->async_watcher.adding_watcher_mx->lock_guard();
            this->async_watcher.adding_watcher_data.push_back(std::move(data));
        }

        this->async_watcher.adding_watcher_async_cb();

        co_return;
    });
}

manapi::future<> manapi::event_loop::_template_cmd_curl(int flag, CURL *curl, std::move_only_function<void(CURLcode result)> cb) {
    co_await async::promise<void> (this->taskpool, [&] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {
        bool flg = true;

        auto data = std::make_unique<adding_curl_data_t>(
            flag,
            curl,
            std::move(cb),
            std::move(resolve),
            std::move(reject)
        );

        for (auto &row : this->curl_watcher.watcher_data_cached) {
            if ((row.status.fetch_or(WATCHER_STATUS_PREPARE) & WATCHER_STATUS_PREPARE) == 0) {
                // row was activated
                row.data = std::move(data);
                row.status.fetch_or(WATCHER_STATUS_READY);

                flg = false;
                break;
            }
        }


        if (flg) {
            auto lk = co_await this->curl_watcher.curl_multi_mx->lock_guard();

            this->curl_watcher.adding_curl_data.push_back(std::move(data));
        }

        this->curl_watcher.adding_curl_async_cb();
        co_return;
    });
}

manapi::future<std::optional<manapi::timer>> manapi::event_loop::_template_cmd_timer(int flag, size_t data, std::move_only_function<manapi::future<>(manapi::timer t)> cb_async, std::move_only_function<void(manapi::timer t)> cb_sync) {

    co_return co_await async::promise<std::optional<manapi::timer>> (this->taskpool, [&] (async::promise<std::optional<manapi::timer>>::resolve_t resolve, async::promise<std::optional<manapi::timer>>::reject_t reject) -> future<void> {
        auto data1 = std::make_unique<adding_timer_data_t>(
            flag,
            data,
            std::move(cb_sync),
            std::move(cb_async),
            std::move(resolve),
            std::move(reject)
        );

        bool flg = true;

        for (auto &row : this->timer_watcher.watcher_data_cached) {
            if ((row.status.fetch_or(WATCHER_STATUS_PREPARE) & WATCHER_STATUS_PREPARE) == 0) {
                // row was activated
                row.data = std::move(data1);
                row.status.fetch_or(WATCHER_STATUS_READY);

                flg = false;
                break;
            }
        }


        if (flg) {
            auto lk = co_await this->timer_watcher.adding_timer_mx->lock_guard();
            this->timer_watcher.adding_timer_data.push_back(std::move(data1));
        }

        this->timer_watcher.adding_timer_async_cb();

        co_return;
    });
}

manapi::future<std::shared_ptr<ev::io>> manapi::event_loop::watch_fd(int fd, int flags, std::move_only_function<void(ev::io &w, int revents)> callback, int priority) {
    auto w = this->create_watcher_fd(fd, flags, std::move(callback), priority);

    auto data = std::make_unique<adding_watcher_data_t>(
        1,
        EV_IO,
        w
    );

    co_await _template_cmd_watcher(std::move(data));

    co_return std::move(w);
}

manapi::future<> manapi::event_loop::unwatch_fd(std::shared_ptr<ev::io> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        0,
         EV_IO,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<std::shared_ptr<ev::async>> manapi::event_loop::watch_async(std::move_only_function<void(ev::async &w, int revents)> callback) {
    auto w = this->create_watcher_async(std::move(callback));
    auto data = std::make_unique<adding_watcher_data_t>(
        1,
        EV_ASYNC,
        nullptr,
        w
    );
    co_await _template_cmd_watcher(std::move(data));
    co_return std::move(w);
}

manapi::future<> manapi::event_loop::unwatch_async(std::shared_ptr<ev::async> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
         0,
         EV_ASYNC,
            nullptr,
         std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<> manapi::event_loop::unwatch_timer(std::shared_ptr<ev::timer> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        .0,
        EV_TIMER,
        nullptr,
        nullptr,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<> manapi::event_loop::watch_fd(std::shared_ptr<ev::io> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        1,
        EV_IO,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<> manapi::event_loop::watch_async(std::shared_ptr<ev::async> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        1,
        EV_ASYNC,
        nullptr,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<> manapi::event_loop::watch_timer(std::shared_ptr<ev::timer> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        1,
        EV_TIMER,
        nullptr,
        nullptr,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

manapi::future<> manapi::event_loop::again_timer(std::shared_ptr<ev::timer> w) {
    auto data = std::make_unique<adding_watcher_data_t>(
        2,
        EV_TIMER,
        nullptr,
        nullptr,
        std::move(w)
    );
    return _template_cmd_watcher(std::move(data));
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::event_loop::get_task_pool() const {
    return this->taskpool;
}

manapi::future<> manapi::event_loop::watch_curl(CURL *curl, std::move_only_function<void(CURLcode result)> cb) {
    return this->_template_cmd_curl(0, curl, std::move(cb));
}

manapi::future<> manapi::event_loop::unwatch_curl(CURL *curl) {
    return this->_template_cmd_curl(1, curl);
}

manapi::future<> manapi::event_loop::pause_watch_curl(CURL *curl) {
    return this->_template_cmd_curl(2, curl);
}

manapi::future<> manapi::event_loop::unpause_watch_curl(CURL *curl) {
    return this->_template_cmd_curl(3, curl);
}

manapi::future<> manapi::event_loop::custom_cb_curl(CURL *curl, std::move_only_function<void(CURLcode result)> cb) {
    return this->_template_cmd_curl(4, curl, std::move(cb));
}

manapi::future<manapi::timer> manapi::event_loop::append_async_timer(size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb) {
    co_return std::move((co_await this->_template_cmd_timer (0, time, std::move(cb), nullptr)).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_sync_timer(size_t time, std::move_only_function<void(manapi::timer t)> cb) {
    co_return std::move((co_await this->_template_cmd_timer (0, time, nullptr, std::move(cb))).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_async_interval(size_t time, std::move_only_function<manapi::future<>(manapi::timer t)> cb) {
    co_return std::move((co_await this->_template_cmd_timer (1, time, std::move(cb), nullptr)).value());
}

manapi::future<manapi::timer> manapi::event_loop::append_sync_interval(size_t time, std::move_only_function<void(manapi::timer t)> cb) {
    co_return std::move((co_await this->_template_cmd_timer (1, time, nullptr, std::move(cb))).value());
}

manapi::future<void> manapi::event_loop::update_state_interval(size_t id) {
    co_await this->_template_cmd_timer(3, id, nullptr, nullptr);
}

manapi::future<> manapi::event_loop::remove_timer(size_t id) {
    co_await this->_template_cmd_timer (2, id, nullptr, nullptr);
}

void manapi::event_loop::set_timer_callback(std::move_only_function<std::optional<manapi::timer>(adding_timer_data_t data)> cb) {
    this->timer_watcher.external_cb = std::move(cb);
}

void manapi::event_loop::interrupt() {
    event_loop::interrupted.store(true);
    std::unique_lock <std::mutex> lk (event_loop::stop_mx);

    while(!manapi::event_loop::events.empty()) {
        auto it = *manapi::event_loop::events.begin();
        lk.unlock();
#ifdef _WIN32
        it.second->loop_interrupted = it.second->loop_thread_id == ::GetCurrentThreadId();
#else
        it.second->loop_interrupted = it.second->loop_thread_id == std::this_thread::get_id();
#endif
        it.second->stop()
            .get(it.second->taskpool);
        lk.lock();
    }
}

std::shared_ptr<ev::io> manapi::event_loop::create_watcher_fd(int fd, int flags, std::move_only_function<void(ev::io &w, int revents)> callback, int priority) {
    auto w = std::make_shared<ev::io>(this->loop);
    ev_io_init(w.get(), (_ev_custom_watcher <ev_io, ev::io>), fd, flags);
    w->priority = priority;
    w->data = new custom_watcher_data_t<ev::io> {.w = w, .cb = std::move(callback)};
    return std::move(w);
}

std::shared_ptr<ev::async> manapi::event_loop::create_watcher_async(std::move_only_function<void(ev::async &w, int revents)> callback) {
    auto w = std::make_shared<ev::async>(this->loop);
    ev_async_init(w.get(), (_ev_custom_watcher<ev_async, ev::async>));
    w->data = new custom_watcher_data_t<ev::async> {.w = w, .cb = std::move(callback)};
    return std::move(w);
}

std::shared_ptr<ev::timer> manapi::event_loop::create_watcher_timer(const float &duration, const int &repeat, std::move_only_function<void(ev::timer &w, int revents)> callback) {
    auto w = std::make_shared<ev::timer>(this->loop);
    ev_timer_init(w.get(), (_ev_custom_watcher <ev_timer, ev::timer>), duration, repeat);
    w->data = new custom_watcher_data_t<ev::timer> {.w = w, .cb = std::move(callback)};
    return std::move(w);
}

template<typename T>
void manapi::event_loop::event_loop::stop_watcher(T &w) {
    auto data = static_cast<custom_watcher_data_t<T> *>(std::exchange(w.data, nullptr));

    if (data) {
        data->w.reset();
        delete data;
    }

    w.stop();
}

template<typename T>
void manapi::event_loop::event_loop::stop_watcher(std::shared_ptr<T> w) {
    if (!w) {
        return;
    }
    this->stop_watcher(*w);
}

manapi::future<> manapi::event_loop::_fix_event_pool_interrupt() {
    if (this->loop_interrupted) {
        auto lk = co_await this->async_watcher.adding_watcher_mx->lock_guard();

        this->async_watcher.adding_watcher_async_cb = [this] ()
            -> void { this->custom_watcher_fd_async(*this->async_watcher.adding_watcher_async, 0); };
        this->curl_watcher.adding_curl_async_cb = [this] ()
        -> void { this->custom_watcher_curl_async(*this->curl_watcher.adding_curl_multi_async, 0); };
        this->timer_watcher.adding_timer_async_cb = [this] ()
            -> void { this->custom_watcher_timer_async(*this->timer_watcher.adding_timer_async, 0); };
        this->callback_watcher.adding_async_cb = [this] ()
            -> void { this->custom_watcher_callback_async(*this->callback_watcher.adding_async, 0); };
    }
}

void manapi::event_loop::_pool(manapi::before_delete lk2, std::shared_ptr<event_loop> le) {
    {
        std::lock_guard<std::mutex> lk (event_loop::stop_mx);

        if (event_loop::interrupted) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INTERRUPTED, "Failed to create a events loop");
        }

        event_loop::events.insert({reinterpret_cast<size_t> (this), std::move(le)});
    }
#ifdef _WIN32
    this->loop_thread_id = ::GetCurrentThreadId();
#else
    this->loop_thread_id = std::this_thread::get_id();
#endif

    this->_stop_watcher = std::make_shared<ev::async>(this->loop);
    this->_stop_watcher->set<event_loop, &event_loop::_async_break_loop> (this);
    this->_stop_watcher->start();

    auto init_watcher = this->create_watcher_async([&lk2] (ev::async &w, int revents)
        -> void { w.stop(); lk2.call(); });

    init_watcher->start();
    init_watcher->send();

    this->loop.run(ev::AUTO);

    /* if init_watcher(...) was not called */
    lk2.call();
    this->stop_watcher(init_watcher);

    this->_stop_watcher->stop();
    this->async_watcher.adding_watcher_async->stop();
}

template void manapi::event_loop::event_loop::stop_watcher<ev::io>(ev::io &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::async>(ev::async &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::timer>(ev::timer &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::check>(ev::check &w);

template void manapi::event_loop::event_loop::stop_watcher<ev::embed>(ev::embed &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::fork>(ev::fork &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::idle>(ev::idle &w);

template void manapi::event_loop::event_loop::stop_watcher<ev::io>(std::shared_ptr<ev::io> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::async>(std::shared_ptr<ev::async> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::timer>(std::shared_ptr<ev::timer> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::check>(std::shared_ptr<ev::check> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::embed>(std::shared_ptr<ev::embed> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::fork>(std::shared_ptr<ev::fork> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::idle>(std::shared_ptr<ev::idle> w);

#ifdef _WIN32
#else
template void manapi::event_loop::event_loop::stop_watcher<ev::child>(ev::child &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::child>(std::shared_ptr<ev::child> w);
#endif