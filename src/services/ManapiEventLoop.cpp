#include "services/ManapiEventLoop.hpp"

std::map<size_t, std::shared_ptr<manapi::event_loop>> manapi::event_loop::events = {};
std::atomic<bool> manapi::event_loop::interrupted = false;
std::mutex manapi::event_loop::stop_mx;


template<typename T1, typename T2>
void manapi_ev_custom_watcher(EV_P_ T1 *w, int revents) {
    auto &data = *static_cast<manapi::event_loop::custom_watcher_data_t<T2> *> (w->data);
    data.cb(*data.w, revents);
}

void handler_interrupt (int sig) {
    manapi::event_loop::interrupt();
}

manapi::event_loop::event_loop(std::shared_ptr<threadpool<task>> taskpool) {
    this->mx = std::make_shared<async::mutex>(taskpool);
    this->adding_watcher_mx = std::make_shared<async::mutex>(taskpool);
    this->taskpool = std::move(taskpool);
    this->adding_watcher_data = {};
    this->loop_interrupted = false;
    this->status = false;
    this->adding_watcher_async = this->create_watcher_async([this] (ev::async &w, int revents) -> void {
        this->custom_watcher_fd_async(w, revents);
    });
    this->adding_watcher_async->priority = 2;
    this->adding_watcher_async_cb = [this] ()
        -> void { this->adding_watcher_async->send(); };

    this->adding_watcher_async->start();
}

manapi::event_loop::~event_loop() {
    this->stop()
        .get(this->taskpool);

    this->stop_watcher (this->adding_watcher_async);
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
    signal (SIGPIPE, SIG_IGN);
    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
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

manapi::future<size_t> manapi::event_loop::subscribe_finish(std::function<manapi::future<void>()> cb) {
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
        co_await async::invoke(it->second);
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
    if (this->adding_watcher_mx->try_to_lock()) {
        while (!this->adding_watcher_data.empty()) {
            auto data = std::move(this->adding_watcher_data.front());
            this->adding_watcher_data.pop_front();

            if (data.flag == 1) {
                switch (data.type) {
                    case EV_IO:
                        data.w_io->start();
                    break;
                    case EV_ASYNC:
                        data.w_async->start();
                        data.w_async->send();
                    break;
                    case EV_TIMER:
                        data.w_timer->start();
                    break;
                    default:
                        break;
                }
            }
            else if (data.flag == 2) {
                switch (data.type) {
                    case EV_TIMER:
                        data.w_timer->again();
                    break;
                    default:
                        break;
                }
            }
            else {
                switch (data.type) {
                    case EV_IO:
                        this->stop_watcher(std::move(data.w_io));
                    break;
                    case EV_ASYNC:
                        this->stop_watcher(std::move(data.w_async));
                    break;
                    case EV_TIMER:
                        this->stop_watcher(std::move(data.w_timer));
                    break;
                    default:
                        break;
                }
            }

            if (data.resolve) {
                data.resolve();
            }
        }

        this->adding_watcher_mx->unlock();
    }
    else {
        this->adding_watcher_async->send();
    }
}

manapi::future<std::shared_ptr<ev::io>> manapi::event_loop::watch_fd(int fd, int flags, const std::function<void(ev::io &w, int revents)> &callback, int priority) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    auto w = this->create_watcher_fd(fd, flags, callback, priority);

    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {
        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 1,
            .type = EV_IO,
            .w_io = w,
            .resolve = std::move(resolve)
        });

        lk.call();

        this->adding_watcher_async_cb();
        co_return;
    });

    co_return std::move(w);
}

manapi::future<> manapi::event_loop::unwatch_fd(std::shared_ptr<ev::io> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 0,
            .type = EV_IO,
            .w_io = std::move(w),
            .resolve = std::move(resolve)
        });

        lk.call();

        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<std::shared_ptr<ev::async>> manapi::event_loop::watch_async(const std::function<void(ev::async &w, int revents)> &callback) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    auto w = this->create_watcher_async(callback);
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {
        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 1,
            .type = EV_ASYNC,
            .w_async = w,
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
    co_return std::move(w);
}

manapi::future<> manapi::event_loop::unwatch_async(std::shared_ptr<ev::async> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 0,
            .type = EV_ASYNC,
            .w_async = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<> manapi::event_loop::unwatch_timer(std::shared_ptr<ev::timer> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 0,
            .type = EV_TIMER,
            .w_timer = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<> manapi::event_loop::watch_fd(std::shared_ptr<ev::io> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 1,
            .type = EV_IO,
            .w_io = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<> manapi::event_loop::watch_async(std::shared_ptr<ev::async> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 1,
            .type = EV_ASYNC,
            .w_async = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<> manapi::event_loop::watch_timer(std::shared_ptr<ev::timer> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 1,
            .type = EV_TIMER,
            .w_timer = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

manapi::future<> manapi::event_loop::again_timer(std::shared_ptr<ev::timer> w) {
    auto lk = co_await this->adding_watcher_mx->lock_guard();
    co_await async::promise<void> (this->taskpool, [&lk, this, &w] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<void> {

        this->adding_watcher_data.push_back(adding_watcher_data_t {
            .flag = 2,
            .type = EV_TIMER,
            .w_timer = std::move(w),
            .resolve = std::move(resolve)
        });
        lk.call();
        this->adding_watcher_async_cb();
        co_return;
    });
}

std::shared_ptr<manapi::threadpool<manapi::task>> manapi::event_loop::get_task_pool() const {
    return this->taskpool;
}

void manapi::event_loop::interrupt() {
    event_loop::interrupted.store(true);
    std::unique_lock <std::mutex> lk (event_loop::stop_mx);

    while(!manapi::event_loop::events.empty()) {
        auto it = *manapi::event_loop::events.begin();
        lk.unlock();
        it.second->loop_interrupted = it.second->loop_thread_id == std::this_thread::get_id();
        it.second->stop()
            .get(it.second->taskpool);
        lk.lock();
    }
}

std::shared_ptr<ev::io> manapi::event_loop::create_watcher_fd(int fd, int flags,const std::function<void(ev::io &w, int revents)> &callback, int priority) {
    auto w = std::make_shared<ev::io>(this->loop);
    ev_io_init(w.get(), (manapi_ev_custom_watcher <ev_io, ev::io>), fd, flags);
    w->priority = priority;
    w->data = new custom_watcher_data_t<ev::io> {.w = w, .cb = callback};
    return std::move(w);
}

std::shared_ptr<ev::async> manapi::event_loop::create_watcher_async(const std::function<void(ev::async &w, int revents)> &callback) {
    auto w = std::make_shared<ev::async>(this->loop);
    ev_async_init(w.get(), (manapi_ev_custom_watcher<ev_async, ev::async>));
    w->data = new custom_watcher_data_t<ev::async> {.w = w, .cb = callback};
    return std::move(w);
}

std::shared_ptr<ev::timer> manapi::event_loop::create_watcher_timer(const float &duration, const int &repeat, const std::function<void(ev::timer &w, int revents)> &callback) {
    auto w = std::make_shared<ev::timer>(this->loop);
    ev_timer_init(w.get(), (manapi_ev_custom_watcher <ev_timer, ev::timer>), duration, repeat);
    w->data = new custom_watcher_data_t<ev::timer> {.w = w, .cb = callback};
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
    this->stop_watcher(*w);
}

manapi::future<> manapi::event_loop::_fix_event_pool_interrupt() {
    if (this->loop_interrupted) {
        auto lk = co_await this->adding_watcher_mx->lock_guard();

        this->adding_watcher_async_cb = [this] ()
            -> void { this->custom_watcher_fd_async(*this->adding_watcher_async, 0); };
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

    this->loop_thread_id = std::this_thread::get_id();

    this->_stop_watcher = std::make_shared<ev::async>(this->loop);
    this->_stop_watcher->set<event_loop, &event_loop::_async_break_loop> (this);
    this->_stop_watcher->start();

    auto init_watcher = this->create_watcher_async([&lk2] (ev::async &w, int revents) -> void {
        w.stop();
        lk2.call();
    });

    init_watcher->start();
    init_watcher->send();

    this->loop.run(ev::AUTO);

    /* if init_watcher(...) was not called */
    lk2.call();
    this->stop_watcher(init_watcher);

    this->_stop_watcher->stop();
    this->adding_watcher_async->stop();
}

template void manapi::event_loop::event_loop::stop_watcher<ev::io>(ev::io &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::async>(ev::async &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::timer>(ev::timer &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::check>(ev::check &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::child>(ev::child &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::embed>(ev::embed &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::fork>(ev::fork &w);
template void manapi::event_loop::event_loop::stop_watcher<ev::idle>(ev::idle &w);

template void manapi::event_loop::event_loop::stop_watcher<ev::io>(std::shared_ptr<ev::io> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::async>(std::shared_ptr<ev::async> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::timer>(std::shared_ptr<ev::timer> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::check>(std::shared_ptr<ev::check> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::child>(std::shared_ptr<ev::child> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::embed>(std::shared_ptr<ev::embed> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::fork>(std::shared_ptr<ev::fork> w);
template void manapi::event_loop::event_loop::stop_watcher<ev::idle>(std::shared_ptr<ev::idle> w);