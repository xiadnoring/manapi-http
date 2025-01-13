#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiBeforeDelete.hpp"

#include "../async/ManapiAsyncMutex.hpp"
#include "../async/ManapiAsyncConditionVariable.hpp"

namespace manapi {
    template <typename T>
    class AtomicAsyncReference {
    public:
        AtomicAsyncReference ();
        AtomicAsyncReference (const T &, std::shared_ptr<size_t> deps, std::shared_ptr<async::mutex> mdeps, std::shared_ptr<async::condition_variable> cv);
        AtomicAsyncReference (AtomicAsyncReference &&n) noexcept;
        ~AtomicAsyncReference ();
        AtomicAsyncReference &operator=(AtomicAsyncReference &&n) noexcept;
        const T *operator->();
        const T &operator*();
    private:
        void _expect_nullptr ();
        const T* ref;
        std::shared_ptr<size_t> deps;
        std::shared_ptr<async::mutex> mdeps;
        std::shared_ptr<async::condition_variable> cv;
    };
    template <typename T>
    class AtomicAsync {
    public:
        AtomicAsync (const std::shared_ptr<threadpool<task>> &taskpool);

        template<typename T1 = T>
        AtomicAsync (T1 v, const std::shared_ptr<threadpool<task>> &taskpool);

        template<typename T1>
        requires(std::is_same_v<T1, std::string>)
        AtomicAsync (const char *n, const std::shared_ptr<threadpool<task>> &taskpool);

        ~AtomicAsync ();

        manapi::future<void> set (T v);
        manapi::future<AtomicAsyncReference <T>> get ();

        manapi::future<void> update (const std::function<manapi::future<void>(T &v)> &func);

        manapi::future<AtomicAsyncReference<T>> operator*();
    private:
        manapi::future<std::shared_ptr<manapi::before_delete>> read_lock ();
        manapi::future<before_delete> readwrite_lock ();
        async::mutex gmx;             // global mutex
        async::mutex mx;              // default mutex
        std::shared_ptr<async::mutex> mdeps;           // deps mutex
        std::shared_ptr<async::condition_variable> cv; // deps cv
        std::shared_ptr<size_t> deps;                // deps count
        T value;                    // value
    };

    template<typename T>
    AtomicAsyncReference<T>::AtomicAsyncReference() {
        this->cv = nullptr;
        this->deps = nullptr;
        this->mdeps = nullptr;
        this->ref = nullptr;
    }

    template<typename T>
    AtomicAsyncReference<T>::AtomicAsyncReference(const T &n, std::shared_ptr<size_t> deps, std::shared_ptr<async::mutex> mdeps, std::shared_ptr<async::condition_variable> cv) {
        this->ref = &n;
        this->deps = std::move(deps);
        this->mdeps = std::move(mdeps);
        this->cv = std::move(cv);

        std::lock_guard<async::mutex> lk (*this->mdeps);
        ++(*this->deps);
        this->cv->notify_all();
    }

    template<typename T>
    AtomicAsyncReference<T>::AtomicAsyncReference(AtomicAsyncReference &&n) noexcept {
        this->operator=(std::forward<decltype(n)>(n));

        n.cv = nullptr;
        n.deps = nullptr;
        n.mdeps = nullptr;
        n.ref = nullptr;
    }

    template<typename T>
    AtomicAsyncReference<T>::~AtomicAsyncReference() {
        if (this->mdeps && this->deps && this->cv) {
            std::lock_guard<async::mutex> lk (*this->mdeps);
            --(*this->deps);
            this->cv->notify_all();
        }
    }

    template<typename T>
    AtomicAsyncReference<T> & AtomicAsyncReference<T>::operator=(AtomicAsyncReference &&n) noexcept {
        this->cv = std::move(n.cv);
        this->deps = std::move(n.deps);
        this->mdeps = std::move(n.mdeps);
        this->ref = std::exchange(n.ref, nullptr);

        return *this;
    }

    template<typename T>
    const T *AtomicAsyncReference<T>::operator->() {
        return this->ref;
    }

    template<typename T>
    const T &AtomicAsyncReference<T>::operator*() {
        return *this->ref;
    }

    template<typename T>
    void AtomicAsyncReference<T>::_expect_nullptr() {
        if (!this->ref) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_STORAGE_OBJECT_IS_NULL, "AtomicAsyncReference's storage is null");
        }
    }

    template<typename T>
    AtomicAsync<T>::AtomicAsync(const std::shared_ptr<threadpool<task>> &taskpool) {
        this->mdeps = std::make_shared<async::mutex>(taskpool);
        this->deps = std::make_shared<size_t>(0);
        this->cv = std::make_shared<async::condition_variable>(taskpool);
    }

    template<typename T>
    template<typename T1>
    AtomicAsync<T>::AtomicAsync(T1 v, const std::shared_ptr<threadpool<task>> &taskpool) {
        this->mdeps = std::make_shared<async::mutex>(taskpool);
        this->deps = std::make_shared<size_t>(0);
        this->cv = std::make_shared<async::condition_variable>(taskpool);

        this->value = v;
    }

    template<typename T>
    template<typename T1>
    requires(std::is_same_v<T1, std::string>)
    AtomicAsync<T>::AtomicAsync(const char *n, const std::shared_ptr<threadpool<task>> &taskpool) {
        this->mdeps = std::make_shared<async::mutex>(taskpool);
        this->deps = std::make_shared<size_t>(0);
        this->cv = std::make_shared<async::condition_variable>(taskpool);

        this->set(std::string{n});
    }

    template<typename T>
    AtomicAsync<T>::~AtomicAsync() {
        auto lk = this->readwrite_lock();
    }

    template<typename T>
    manapi::future<void> AtomicAsync<T>::set(T v) {
        auto lk = co_await this->readwrite_lock();
        this->value = std::move(v);
    }

    template<typename T>
    manapi::future<AtomicAsyncReference <T>> AtomicAsync<T>::get() {
        auto lk = co_await this->read_lock();
        co_return AtomicAsyncReference<T> (this->value, this->deps, this->mdeps, this->cv);
    }

    template<typename T>
    manapi::future<void> AtomicAsync<T>::update(const std::function<manapi::future<void>(T &v)> &func) {
        auto lk = co_await this->readwrite_lock();
        co_await func (this->value);
    }

    template<typename T>
    manapi::future<AtomicAsyncReference<T>> AtomicAsync<T>::operator*() {
        return this->get();
    }

    template<typename T>
    manapi::future<std::shared_ptr<manapi::before_delete>> AtomicAsync<T>::read_lock() {
        return this->gmx->lock_guard();
    }

    template<typename T>
    manapi::future<before_delete> AtomicAsync<T>::readwrite_lock() {
        auto lk = co_await this->read_lock();
        auto lkdeps = co_await this->mdeps->lock_guard();
        this->cv->wait(this->mdeps, [this] () -> bool {
            return (*this->deps) == 0;
        });

        co_return before_delete{[lk = std::move(lk), lkdeps = std::move(lkdeps)] () -> void {}};
    }
}