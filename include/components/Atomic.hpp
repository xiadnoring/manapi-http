#ifndef MANAPIHTTP_ATOMIC_HPP
#define MANAPIHTTP_ATOMIC_HPP

#include <condition_variable>

#include "ManapiUtils.hpp"
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
        Atomic (T v);
        Atomic (const char *n);
        ~Atomic ();
        AtomicReference <T> get ();
        void update (const std::function<void(T &v)> &func);
        Atomic& operator=(const T &n);
        Atomic& operator=(const char *n);
        Atomic &operator++();
        Atomic &operator--();
        bool operator>(const T &n);
        bool operator<(const T &n);
        bool operator>=(const T &n);
        bool operator<=(const T &n);
        bool operator==(const T &n);
        bool operator!=(const T &n);
        Atomic operator-(const T &n);
        Atomic operator+(const T &n);
        Atomic &operator-=(const T &n);
        Atomic &operator+=(const T &n);
        AtomicReference<T> operator*();
    private:
        void _wait ();
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
            std::lock_guard<std::mutex> lk (*this->mdeps);
            --(*this->deps);
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
    Atomic<T>::Atomic(T v) {
        {
            std::lock_guard<std::mutex> lk (mdeps);
            deps = 0;
        }
        value = v;
    }

    template<typename T>
    Atomic<T>::Atomic(const char *n) {
        {
            std::lock_guard<std::mutex> lk (mdeps);
            deps = 0;
        }
        operator=(n);
    }

    template<typename T>
    Atomic<T>::~Atomic() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
    }

    template<typename T>
    AtomicReference <T> Atomic<T>::get() {
        std::lock_guard<std::mutex> lk (gmx);

        return std::move(AtomicReference<T> (value, deps, mdeps, cv));
    }

    template<typename T>
    void Atomic<T>::update(const std::function<void(T &v)> &func) {
        std::lock_guard <std::mutex> lk (gmx);
        _wait();
        func (value);
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator=(const T &n) {
        std::lock_guard <std::mutex> lk (gmx);
        _wait();
        value = n;
        return *this;
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator=(const char *n) {
        return *this;
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator++() {
        return *this;
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator--() {
        return *this;
    }

    template<typename T>
    bool Atomic<T>::operator>=(const T &n) {
        return false;
    }

    template<typename T>
    bool Atomic<T>::operator<=(const T &n) {
        return false;
    }

    template<typename T>
    bool Atomic<T>::operator==(const T &n) {
        return false;
    }

    template<typename T>
    bool Atomic<T>::operator!=(const T &n) {
        return false;
    }

    template<typename T>
    Atomic<T> Atomic<T>::operator-(const T &n) {
        return *this;
    }

    template<typename T>
    Atomic<T> Atomic<T>::operator+(const T &n) {
        return *this;
    }

    template<typename T>
    Atomic<T> &Atomic<T>::operator-=(const T &n) {
        return *this;
    }

    template<typename T>
    Atomic<T> &Atomic<T>::operator+=(const T &n) {
        return *this;
    }

    template<typename T>
    bool Atomic<T>::operator<(const T &n) {
        return false;
    }

    template<typename T>
    bool Atomic<T>::operator>(const T &n) {
        return false;
    }

    template<typename T>
    void Atomic<T>::_wait() {
        std::unique_lock<std::mutex> lk (mx);
        cv.wait(lk, [this] () -> bool {
            std::lock_guard<std::mutex> lkdeps (mdeps);
            return deps == 0;
        });
    }

    // size_t

    template<>
    inline Atomic<size_t> &Atomic<size_t>::operator++() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        ++value;
        return *this;
    }

    template<>
    inline Atomic<size_t> &Atomic<size_t>::operator-=(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        value -= n;
        return *this;
    }

    template<>
    inline Atomic<size_t> &Atomic<size_t>::operator+=(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        value += n;
        return *this;
    }

    template<>
    inline Atomic<size_t> Atomic<size_t>::operator-(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return (value - n);
    }

    template<>
    inline Atomic<size_t> Atomic<size_t>::operator+(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return (value + n);
    }

    template<>
    inline bool Atomic<size_t>::operator!=(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value != n;
    }

    template<>
    inline bool Atomic<size_t>::operator==(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value == n;
    }

    template<>
    inline bool Atomic<size_t>::operator>(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value > n;
    }

    template<>
    inline bool Atomic<size_t>::operator<(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value < n;
    }

    template<>
    inline bool Atomic<size_t>::operator<=(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value <= n;
    }

    template<>
    inline bool Atomic<size_t>::operator>=(const size_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value >= n;
    }

    template<>
    inline Atomic<size_t> &Atomic<size_t>::operator--() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        --value;
        return *this;
    }

    // ssize_t


    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator++() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        ++value;
        return *this;
    }

    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator-=(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        value -= n;
        return *this;
    }

    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator+=(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        value += n;
        return *this;
    }

    template<typename T>
    AtomicReference<T> Atomic<T>::operator*() {
        std::lock_guard<std::mutex> lk (gmx);
        return std::move(AtomicReference<T> (value, deps, mdeps, cv));
    }

    template<>
    inline Atomic<ssize_t> Atomic<ssize_t>::operator-(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return (value - n);
    }

    template<>
    inline Atomic<ssize_t> Atomic<ssize_t>::operator+(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return (value + n);
    }

    template<>
    inline bool Atomic<ssize_t>::operator!=(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value != n;
    }

    template<>
    inline bool Atomic<ssize_t>::operator==(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value == n;
    }

    template<>
    inline bool Atomic<ssize_t>::operator>(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value > n;
    }

    template<>
    inline bool Atomic<ssize_t>::operator<(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value < n;
    }

    template<>
    inline bool Atomic<ssize_t>::operator<=(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value <= n;
    }

    template<>
    inline bool Atomic<ssize_t>::operator>=(const ssize_t &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value >= n;
    }

    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator--() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        --value;
        return *this;
    }

    // bool

    template<>
    inline bool Atomic<bool>::operator!=(const bool &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value != n;
    }

    template<>
    inline bool Atomic<bool>::operator==(const bool &n) {
        std::lock_guard<std::mutex> lk (gmx);
        return value == n;
    }

    // string
    template<>
    inline Atomic<std::string> & Atomic<std::string>::operator=(const char *n) {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        value = std::move(std::string(n));
        return *this;
    }

    template<>
    inline bool Atomic<std::string>::operator==(const std::string &n) {
        return value == n;
    }
}

#endif //MANAPIHTTP_ATOMIC_HPP
