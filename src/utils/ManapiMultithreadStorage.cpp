#include "utils/ManapiMultithreadStorage.hpp"
#include "ManapiDebug.hpp"
#include "ManapiEventLoop.hpp"

struct manapi::multithread_storage::data_t {
    async::tmutex mx;
    std::set<std::shared_ptr<worker_t>> workers;
    std::shared_ptr<manapi::json> data;
    const std::shared_ptr<worker_t> *current;
    std::shared_ptr<void> src;
};

manapi::multithread_storage::worker_t::worker_t(std::shared_ptr<void> data) {
    this->data = std::move(data);
}

void * manapi::multithread_storage::worker_t::pointer() {
    return this->data.get();
}

manapi::multithread_storage::multithread_storage(void *ptr, std::function<void(void *n)> deleter) {
    std::shared_ptr<void> src (ptr, std::move(deleter));
    this->data_ = std::make_shared<data_t>();
    this->data_->data = std::make_shared<manapi::json>();
    this->data_->src = std::move(src);
}

manapi::multithread_storage::multithread_storage(manapi::json n, void *ptr, std::function<void(void *n)> deleter) : multithread_storage(ptr, std::move(deleter)) {
    *this->data_->data = std::move(n);
}

manapi::multithread_storage::~multithread_storage() = default;

manapi::multithread_storage::multithread_storage(const multithread_storage &n) = default;

manapi::multithread_storage & manapi::multithread_storage::operator=(const multithread_storage &n) = default;

manapi::future<std::shared_ptr<manapi::multithread_storage::worker_t>> manapi::multithread_storage::subscribe(subscribe_cb cb) {
    auto w = std::make_shared<worker_t>(this->data_->src);
    w->cb = std::move(cb);
    w->w = manapi::async::current()->eventloop()->create_watcher_async(
        [this, w] (const ev::shared_async &watcher) mutable  -> void {
            manapi::async::run<manapi::sbefore_delete>(this->data_->mx.lock_guard(),
                [this, w] (std::exception_ptr err, manapi::sbefore_delete *s) -> void {
                if (err)
                    return;

                auto res = this->call_sync_callback_(w.get());
                if (!res) {
                    manapi_log_error("%s: %s failed due to %.*s", "multhithread storage", "user callback",
                        res.msg().size(), res.msg().data());
                }
            });
    }).unwrap();

    auto lk = co_await this->data_->mx.lock_guard();
    this->data_->workers.insert(w);
    auto res = this->call_sync_callback_(w.get());
    if (!res) {
        manapi_log_error("%s: %s failed due to %.*s", "multhithread storage", "user callback",
            res.msg().size(), res.msg().data());
    }
    co_return std::move(w);
}

manapi::future<manapi::status> manapi::multithread_storage::edit(const std::shared_ptr<worker_t> &m, std::move_only_function<bool(manapi::json &data)> cb) {
    manapi::status status = status_ok();

    if (m) {
        auto lk = co_await this->data_->mx.lock_guard();
        bool notify = false;
        try {
            notify = cb (*this->data_->data);
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "mutlithread storage: edit cb failed", e.what());
            status = manapi::status_internal("mutlithread storage: edit cb failed");
        }

        if (notify)
            this->notify_(m);
    }

    co_return std::move(status);
}

manapi::future<manapi::status> manapi::multithread_storage::edit_async(const std::shared_ptr<worker_t> &m, std::move_only_function<manapi::future<bool>(manapi::json &data)> cb) {
    auto status = manapi::status_ok();
    if (m) {
        auto lk = co_await this->data_->mx.lock_guard();
        bool notify = false;
        try {
            notify = co_await cb (*this->data_->data);
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "mutlithread storage: edit cb failed", e.what());
            status = manapi::status_internal("mutlithread storage: edit cb failed");
        }
        if (notify) {
            this->notify_(m);
        }
    }

    co_return std::move(status);
}

manapi::future<> manapi::multithread_storage::unsubscribe(const std::shared_ptr<worker_t> &m) {
    if (!m)
        co_return;

    auto lk = co_await this->data_->mx.lock_guard();
    this->unsubscribe_(m);
    manapi::async::current()->eventloop()->stop_watcher(m->w);
}


manapi::future<manapi::sbefore_delete> manapi::multithread_storage::write_lock() {
    return this->data_->mx.lock_guard();
}

void * manapi::multithread_storage::pointer() MANAPIHTTP_NOEXCEPT {
    return this->data_->src.get();
}

void manapi::multithread_storage::notify_(const std::shared_ptr<worker_t> &m) MANAPIHTTP_NOEXCEPT {
    if (!this->data_)
        return;

    try {
        if (m->cb)
            m->cb(*this->data_->data);
    }
    catch (std::exception const &e) {
        manapi_log_error("multithread storage:Notify failed");
    }

    for (auto it = this->data_->workers.begin(); it != this->data_->workers.end(); ) {
        auto const next = std::next(it);
        this->data_->current = &*it;
        if (*it != m)
            this->call_callback_(it->get());
        it = next;
    }
}

void manapi::multithread_storage::unsubscribe_(const std::shared_ptr<worker_t> &w) MANAPIHTTP_NOEXCEPT {
    this->data_->workers.erase(w);
}

manapi::status manapi::multithread_storage::call_sync_callback_(worker_t *w) MANAPIHTTP_NOEXCEPT {
    try {
        if (w && w->cb)
            w->cb(*this->data_->data);
        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "mutlithread storage: subscribe cb failed", e.what());
        return status_internal("mutlithread storage: subscribe cb failed");
    }
}

void manapi::multithread_storage::call_callback_(worker_t *w) MANAPIHTTP_NOEXCEPT {
    try {
        if (w)
            w->w->send();
    }
    catch (std::exception const &e) {
        manapi_log_error("mutlithread storage: subscribe cb failed due to %s", e.what());
    }
}


