#ifndef MANAPIHTTP_ATOMIC_HPP
#define MANAPIHTTP_ATOMIC_HPP

#include <condition_variable>

#include <ManapiBeforeDelete.hpp>

namespace manapi::net {
    template <typename T>
    class Atomic {
    public:
        Atomic ();
        Atomic (T v);
        ~Atomic ();
        std::pair <const T &, manapi::net::utils::before_delete> get ();
        void update (const std::function<void(T &v)> &func);
        Atomic& operator=(T n);
        Atomic &operator++();
        Atomic &operator--();
    private:
        void _wait ();
        std::mutex gmx;
        std::mutex mx;
        std::mutex mdeps;
        std::condition_variable cv;
        size_t deps;
        T value;
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
    Atomic<T> & Atomic<T>::operator=(T n) {
        std::lock_guard <std::mutex> lk (gmx);
        _wait();
        value = n;
        return *this;
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator++() {
        return *this;
    }

    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator++() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        ++value;
        return *this;
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator--() {
        return *this;
    }

    template<>
    inline Atomic<ssize_t> &Atomic<ssize_t>::operator--() {
        std::lock_guard<std::mutex> lk (gmx);
        _wait();
        --value;
        return *this;
    }

    template<typename T>
    void Atomic<T>::_wait() {
        std::unique_lock<std::mutex> lk (mx);
        cv.wait(lk, [this] () -> bool {
            std::lock_guard<std::mutex> lkdeps (mdeps);
            return deps == 0;
        });
    }
}

#endif //MANAPIHTTP_ATOMIC_HPP
