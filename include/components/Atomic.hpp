#ifndef MANAPIHTTP_ATOMIC_HPP
#define MANAPIHTTP_ATOMIC_HPP

#include <condition_variable>

#include "../ManapiUtils.hpp"
#include "../ManapiBeforeDelete.hpp"

namespace manapi::net {
    template <typename T>
    class AtomicReference {
    public:
        AtomicReference ();
        AtomicReference (const T &, size_t &deps, std::mutex &mdeps, std::condition_variable &cv);
        AtomicReference (AtomicReference &&n) noexcept;
        ~AtomicReference ();
        AtomicReference &operator=(AtomicReference &&n) noexcept;
        const T *operator->();
        const T &operator*();
    private:
        void _expect_nullptr ();
        const T* ref;
        size_t *deps;
        std::mutex *mdeps;
        std::condition_variable *cv;
    };
    template <typename T>
    class Atomic {
    public:
        Atomic ();

        template<typename T1 = T>
        Atomic (T1 v);

        template<typename T1>
        requires(std::is_same_v<T1, std::string>)
        Atomic (const char *n);

        ~Atomic ();

        AtomicReference <T> get ();

        void update (const std::function<void(T &v)> &func);

        Atomic& operator=(const T &n);

        template<typename T1 = T>
        requires(std::is_same_v<T1, std::string>)
        Atomic& operator=(const char *n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic &operator++();

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic &operator--();

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        bool operator>(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        bool operator<(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        bool operator>=(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        bool operator<=(const T &n);

        template<typename T1>
        bool operator==(const T1 &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        bool operator!=(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic operator-(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic operator+(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic &operator-=(const T &n);

        template<typename T1 = T>
        requires(std::is_integral_v<T1>)
        Atomic &operator+=(const T &n);

        AtomicReference<T> operator*();
    private:
        std::shared_ptr<std::unique_lock<std::mutex>> read_lock ();
        utils::before_delete readwrite_lock ();
        std::mutex gmx;             // global mutex
        std::mutex mx;              // default mutex
        std::mutex mdeps;           // deps mutex
        std::condition_variable cv; // deps cv
        size_t deps;                // deps count
        T value;                    // value
    };

    template<typename T>
    AtomicReference<T>::AtomicReference() {
        this->cv = nullptr;
        this->deps = nullptr;
        this->mdeps = nullptr;
        this->ref = nullptr;
    }

    template<typename T>
    AtomicReference<T>::AtomicReference(const T &n, size_t &deps, std::mutex &mdeps, std::condition_variable &cv) {
        this->ref = &n;
        this->deps = &deps;
        this->mdeps = &mdeps;
        this->cv = &cv;

        {
            std::lock_guard<std::mutex> lk (*this->mdeps);
            ++(*this->deps);
        }
        cv.notify_all();
    }

    template<typename T>
    AtomicReference<T>::AtomicReference(AtomicReference &&n) noexcept {
        this->operator=(std::forward<decltype(n)>(n));

        n.cv = nullptr;
        n.deps = nullptr;
        n.mdeps = nullptr;
        n.ref = nullptr;
    }

    template<typename T>
    AtomicReference<T>::~AtomicReference() {
        if (this->mdeps != nullptr && this->deps != nullptr && this->cv != nullptr) {
            {
                std::lock_guard<std::mutex> lk (*this->mdeps);
                --(*this->deps);
            }
            cv->notify_all();
        }
    }

    template<typename T>
    AtomicReference<T> & AtomicReference<T>::operator=(AtomicReference &&n) noexcept {
        this->cv = n.cv;
        this->deps = n.deps;
        this->mdeps = n.mdeps;
        this->ref = n.ref;

        n.cv = nullptr;
        n.deps = nullptr;
        n.mdeps = nullptr;
        n.ref = nullptr;
        return *this;
    }

    template<typename T>
    const T *AtomicReference<T>::operator->() {
        return this->ref;
    }

    template<typename T>
    const T &AtomicReference<T>::operator*() {
        return *this->ref;
    }

    template<typename T>
    void AtomicReference<T>::_expect_nullptr() {
        if (this->ref == nullptr) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_STORAGE_OBJECT_IS_NULL, "AtomicReference's storage is null");
        }
    }

    template<typename T>
    Atomic<T>::Atomic() {
        {
            std::lock_guard<std::mutex> lk (mdeps);
            deps = 0;
        }
    }

    template<typename T>
    template<typename T1>
    Atomic<T>::Atomic(T1 v) {
        {
            std::lock_guard<std::mutex> lk (mdeps);
            deps = 0;
        }
        value = v;
    }

    template<typename T>
    template<typename T1>
    requires(std::is_same_v<T1, std::string>)
    Atomic<T>::Atomic(const char *n) {
        {
            std::lock_guard<std::mutex> lk (mdeps);
            deps = 0;
        }
        operator=(n);
    }

    template<typename T>
    Atomic<T>::~Atomic() {
        auto lk = this->readwrite_lock();
    }

    template<typename T>
    AtomicReference <T> Atomic<T>::get() {
        std::lock_guard<std::mutex> lk (this->gmx);

        return std::move(AtomicReference<T> (this->value, this->deps, this->mdeps, this->cv));
    }

    template<typename T>
    void Atomic<T>::update(const std::function<void(T &v)> &func) {
        auto lk = this->readwrite_lock();
        func (this->value);
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator=(const T &n) {
        auto lk = this->readwrite_lock();
        this->value = n;
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_same_v<T1, std::string>)
    Atomic<T> & Atomic<T>::operator=(const char *n) {
        auto lk = this->readwrite_lock();
        this->value = n;
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> & Atomic<T>::operator++() {
        auto lk = this->readwrite_lock();
        ++this->value;
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> & Atomic<T>::operator--() {
        auto lk = this->readwrite_lock();
        --this->value;
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    bool Atomic<T>::operator>(const T &n) {
        auto lk = this->read_lock();
        return this->value > n;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    bool Atomic<T>::operator<(const T &n) {
        auto lk = this->read_lock();
        return this->value < n;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    bool Atomic<T>::operator>=(const T &n) {
        auto lk = this->read_lock();
        return this->value >= n;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    bool Atomic<T>::operator<=(const T &n) {
        auto lk = this->read_lock();
        return this->value <= n;
    }

    template<typename T>
    template<typename T1>
    bool Atomic<T>::operator==(const T1 &n) {
        auto lk = this->read_lock();
        return this->value == n;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    bool Atomic<T>::operator!=(const T &n) {
        return this->operator==(n) == false;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> Atomic<T>::operator-(const T &n) {
        auto lk = this->readwrite_lock();
        return {this->value - n};
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> Atomic<T>::operator+(const T &n) {
        auto lk = this->readwrite_lock();
        return {this->value + n};
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> & Atomic<T>::operator-=(const T &n) {
        auto lk = this->readwrite_lock();
        this->value -= n;
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_integral_v<T1>)
    Atomic<T> & Atomic<T>::operator+=(const T &n) {
        auto lk = this->readwrite_lock();
        this->value += n;
        return *this;
    }

    template<typename T>
    AtomicReference<T> Atomic<T>::operator*() {
        return this->get();
    }

    template<typename T>
    std::shared_ptr<std::unique_lock<std::mutex>> Atomic<T>::read_lock() {
        auto lk = std::make_shared<std::unique_lock<std::mutex>> (this->gmx, std::try_to_lock);
        if (!lk->owns_lock()) {
            lk->lock();
        }

        return lk;
    }

    template<typename T>
    utils::before_delete Atomic<T>::readwrite_lock() {
        auto lk = this->read_lock();
        auto lkdeps = std::make_shared <std::unique_lock<std::mutex>> (this->mdeps, std::try_to_lock);
        if (!lkdeps->owns_lock()) {
            lkdeps->lock();
        }

        this->cv.wait(*lkdeps, [this] () -> bool {
            return this->deps == 0;
        });

        return std::move(utils::before_delete{[lk = std::move(lk), lkdeps = std::move(lkdeps)] () -> void {}});
    }
}

#endif //MANAPIHTTP_ATOMIC_HPP
