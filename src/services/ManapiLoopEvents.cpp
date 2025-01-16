#include "services/ManapiLoopEvents.hpp"

std::map<size_t, std::shared_ptr<manapi::loop_events>> manapi::loop_events::events = {};
std::atomic<bool> manapi::loop_events::interrupted = false;
std::mutex manapi::loop_events::stop_mx;

void handler_interrupt (int sig) {
    manapi::loop_events::interrupt();
}

manapi::loop_events::loop_events(std::shared_ptr<threadpool<task>> taskpool) : mx (taskpool), adding_watcher_mx(taskpool) {
    this->taskpool = std::move(taskpool);
    this->status = false;
    this->adding_watcher_async = this->create_watcher_async([this] (ev::async &w, int revents) -> void {
        this->custom_watcher_fd_async(w, revents);
    });

    this->adding_watcher_async->start();
}

manapi::loop_events::~loop_events() {
    this->stop()
        .get(this->taskpool);

    this->stop_watcher_async(this->adding_watcher_async);
}

manapi::future<> manapi::loop_events::start(std::shared_ptr<loop_events> le) {
    auto lk = co_await this->mx.lock_guard();
    if (std::exchange(this->status, true)) {
        co_return;
    }

    this->_pool(std::move(lk), std::move(le));
}

void manapi::loop_events::sync_start(std::shared_ptr<loop_events> le) {
    auto lk = this->mx.lock_guard().get(this->taskpool);
    if (std::exchange(this->status, true)) {
        return;
    }

    this->_pool(std::move(lk), std::move(le));
}

void manapi::loop_events::setup_handle_interrupt() {
    signal (SIGPIPE, SIG_IGN);
    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
}

manapi::future<> manapi::loop_events::stop() {
    auto lk = co_await this->mx.lock_guard();
    if (!std::exchange(this->status, false)) {
        co_return;
    }

    if (this->loop_thread_id == std::this_thread::get_id()) {
        /* in the libev */
        auto promise = async::promise<void> (this->taskpool,
        [this] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> void {
            this->stop_pool(resolve);
        });
        co_await promise;
        this->stop_watcher->send();
    }
    else {
        auto promise = async::promise<void> (this->taskpool,
            [this] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> void {
            this->resolve_stop = resolve;
            this->stop_watcher->send();
        });
        co_await promise;
    }
}

manapi::future<size_t> manapi::loop_events::subscribe_finish(std::function<void()> cb) {
    auto lk = co_await this->mx.lock_guard();
    auto id = *reinterpret_cast<const size_t *> (&cb);

    if (!this->map_finish_cb.insert({id, std::move(cb)}).second) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_SUBSCRIBE_FAILURE, "index {} exists", id);
    }

    co_return id;
}

manapi::future<void> manapi::loop_events::unsubscribe_finish(const size_t &id) {
    auto lk = co_await this->mx.lock_guard();
    this->map_finish_cb.erase(id);
}

ev::loop_ref manapi::loop_events::get_loop() {
    return this->loop;
}

void manapi::loop_events::_call_and_free_on_finish_cb() {
    while (!this->map_finish_cb.empty()) {
        const auto it = this->map_finish_cb.begin();
        it->second();
        this->map_finish_cb.erase(it);
    }
}

void manapi::loop_events::stop_pool(async::promise<void>::resolve_t resolve) {
    this->_call_and_free_on_finish_cb();

    {
        std::lock_guard <std::mutex> lk (loop_events::stop_mx);
        loop_events::events.erase(reinterpret_cast<size_t>(this));
    }

    resolve();
}

void manapi::loop_events::_async_break_loop(ev::async &watcher, int revents) {
    this->loop.break_loop(ev::ALL);

    if (this->resolve_stop) {
        /* if resolve caballback exists, break the loop otherwise */
        this->stop_pool(std::exchange(this->resolve_stop, nullptr));
    }
}

void manapi::loop_events::custom_watcher_fd_async(ev::async &w, int revents) {
    if (this->adding_watcher_data.flag) {
        if (this->adding_watcher_data.w_io) {
            this->adding_watcher_data.w_io->start();
        }
        else if (this->adding_watcher_data.w_async) {
            this->adding_watcher_data.w_async->start();
            this->adding_watcher_data.w_async->send();
        }
    }
    else {
        if (this->adding_watcher_data.w_io) {
            this->stop_watcher_fd(this->adding_watcher_data.w_io);
        }
        else if (this->adding_watcher_data.w_async) {
            this->stop_watcher_async(this->adding_watcher_data.w_async);
        }
    }

    this->adding_watcher_data.w_io.reset();
    this->adding_watcher_data.w_async.reset();

    this->adding_watcher_mx.unlock();
}

manapi::future<std::shared_ptr<ev::io>> manapi::loop_events::watch_fd(int fd, int flags, const std::function<void(ev::io &w, int revents)> &callback) {
    co_await this->adding_watcher_mx.lock();
    auto w = this->create_watcher_fd(fd, flags, callback);
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .w_io = w,
    };
    this->adding_watcher_async->send();
    co_return std::move(w);
}

manapi::future<> manapi::loop_events::unwatch_fd(std::shared_ptr<ev::io> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = false,
        .w_io = std::move(w),
    };
    this->adding_watcher_async->send();
}

manapi::future<std::shared_ptr<ev::async>> manapi::loop_events::watch_async(const std::function<void(ev::async &w, int revents)> &callback) {
    co_await this->adding_watcher_mx.lock();
    auto w = this->create_watcher_async(callback);
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .w_async = w
    };
    this->adding_watcher_async->send();
    co_return std::move(w);
}

manapi::future<> manapi::loop_events::unwatch_async(std::shared_ptr<ev::async> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = false,
        .w_async = std::move(w)
    };
    this->adding_watcher_async->send();
}

manapi::future<> manapi::loop_events::watch_fd(std::shared_ptr<ev::io> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .w_io = std::move(w)
    };
    this->adding_watcher_async->send();
}

manapi::future<> manapi::loop_events::watch_async(std::shared_ptr<ev::async> w) {
    co_await this->adding_watcher_mx.lock();
    this->adding_watcher_data = adding_watcher_data_t {
        .flag = true,
        .w_async = std::move(w)
    };
    this->adding_watcher_async->send();
}

void manapi::loop_events::interrupt() {
    loop_events::interrupted.store(true);
    std::unique_lock <std::mutex> lk (loop_events::stop_mx);

    while(!manapi::loop_events::events.empty()) {
        auto it = *manapi::loop_events::events.begin();
        lk.unlock();
        it.second->stop()
            .get(it.second->taskpool);
        lk.lock();
    }
}

std::shared_ptr<ev::io> manapi::loop_events::create_watcher_fd(int fd, int flags,const std::function<void(ev::io &w, int revents)> &callback) {
    auto w = std::make_shared<ev::io>(this->loop);
    ev_io_init(w.get(), loop_events::custom_watcher_fd, fd, flags);
    w->data = new custom_watcher_data_t<ev::io> {.w = w, .cb = callback};
    return std::move(w);
}

std::shared_ptr<ev::async> manapi::loop_events::create_watcher_async(const std::function<void(ev::async &w, int revents)> &callback) {
    auto w = std::make_shared<ev::async>(this->loop);
    ev_async_init(w.get(), loop_events::custom_watcher_async);
    w->data = new custom_watcher_data_t<ev::async> {.w = w, .cb = callback};
    return std::move(w);
}

void manapi::loop_events::stop_watcher_fd(ev::io &w) {
    auto data = static_cast<custom_watcher_data_t<ev::io> *>(std::exchange(w.data, nullptr));
    data->w.reset();
    delete data;
    w.stop();
}

void manapi::loop_events::stop_watcher_async(ev::async &w) {
    auto data = static_cast<custom_watcher_data_t<ev::async> *>(std::exchange(w.data, nullptr));
    data->w.reset();
    delete data;
    w.stop();
}

void manapi::loop_events::stop_watcher_fd(std::shared_ptr<ev::io> w) {
    this->stop_watcher_fd(*w);
}

void manapi::loop_events::stop_watcher_async(std::shared_ptr<ev::async> w) {
    this->stop_watcher_async(*w);
}

void manapi::loop_events::custom_watcher_fd(struct ev_loop *loop, ev_io *w, int revents) {
    auto &data = *static_cast<custom_watcher_data_t<ev::io> *> (w->data);
    data.cb(*data.w, revents);
}

void manapi::loop_events::custom_watcher_async(struct ev_loop *loop, ev_async *w, int revents) {
    auto &data = *static_cast<custom_watcher_data_t<ev::async> *> (w->data);
    data.cb(*data.w, revents);
}

void manapi::loop_events::_pool(manapi::before_delete lk2, std::shared_ptr<loop_events> le) {
    {
        std::lock_guard<std::mutex> lk (loop_events::stop_mx);

        if (loop_events::interrupted) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_INTERRUPTED, "Failed to create a events loop");
        }

        loop_events::events.insert({reinterpret_cast<size_t> (this), std::move(le)});
    }

    this->loop_thread_id = std::this_thread::get_id();

    this->stop_watcher = std::make_shared<ev::async>(this->loop);
    this->stop_watcher->set<loop_events, &loop_events::_async_break_loop> (this);
    this->stop_watcher->start();

    auto init_watcher = this->create_watcher_async([&lk2] (ev::async &w, int revents) -> void {
        w.stop();
        lk2.call();
    });

    init_watcher->start();
    init_watcher->send();

    this->loop.run(ev::AUTO);

    /* if init_watcher(...) was not called */
    lk2.call();
    this->stop_watcher_async(init_watcher);

    this->stop_watcher->stop();
    this->adding_watcher_async->stop();
}
