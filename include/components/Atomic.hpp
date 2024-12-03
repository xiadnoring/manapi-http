#ifndef MANAPIHTTP_ATOMIC_HPP
#define MANAPIHTTP_ATOMIC_HPP

#include <condition_variable>

#include "../ManapiBeforeDelete.hpp"

namespace manapi::net {
    template <typename T>
    class Atomic {
    public:
        Atomic ();
        Atomic (T v);
        ~Atomic ();
        std::pair <const T &, manapi::net::utils::before_delete> get ();
        void update (const std::function<void(T &v)> &func);
        Atomic& operator=(const T &n);
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
    Atomic<T>::~Atomic() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
    }

    template<typename T>
    std::pair <const T &, manapi::net::utils::before_delete> Atomic<T>::get() {
        std::lock_guard<std::mutex> lk (gmx);

        {
            std::lock_guard<std::mutex> lkdeps (mdeps);
            ++deps;
        }
        manapi::net::utils::before_delete locker ([&] () -> void {
            {
                std::lock_guard<std::mutex> lkdeps (mdeps);
                --deps;
            }
            cv.notify_all();
        });

        return {value, std::move(locker)};
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
}

#endif //MANAPIHTTP_ATOMIC_HPP
