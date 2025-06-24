#pragma once

#include <condition_variable>

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiTime.hpp"
#include "../ManapiDebug.hpp"
#include "../ManapiBeforeDelete.hpp"

namespace manapi {
    template <typename T>
    class AtomicReference {
    public:
        AtomicReference ();
        AtomicReference (std::shared_ptr<T> value);
        AtomicReference (AtomicReference &&n) noexcept;
        ~AtomicReference ();
        AtomicReference &operator=(AtomicReference &&n) noexcept;
        const T *operator->();
        const T &operator*();
    private:
        void _expect_nullptr ();
        std::shared_ptr<T> value;
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

        Atomic& operator=(T n);

        template<typename T1 = T>
        requires(std::is_same_v<T1, std::string>)
        Atomic& operator=(const char *n);

        AtomicReference<T> operator*();
    private:
        std::atomic<std::uintptr_t> value;
    };

    template<typename T>
    AtomicReference<T>::AtomicReference() {
        this->value = nullptr;
    }

    template<typename T>
    AtomicReference<T>::AtomicReference(std::shared_ptr<T> value) {
        this->value = std::move(value);
    }

    template<typename T>
    AtomicReference<T>::AtomicReference(AtomicReference &&n) noexcept {
        this->value = std::move(n.value);
    }

    template<typename T>
    AtomicReference<T>::~AtomicReference() = default;

    template<typename T>
    AtomicReference<T> & AtomicReference<T>::operator=(AtomicReference &&n) noexcept {
        this->value = std::move(n.value);
        return *this;
    }

    template<typename T>
    const T *AtomicReference<T>::operator->() {
        return this->value.get();
    }

    template<typename T>
    const T &AtomicReference<T>::operator*() {
        return *this->value;
    }

    template<typename T>
    void AtomicReference<T>::_expect_nullptr() {
        if (!this->value) {
            throw std::runtime_error("AtomicReference's storage is null");
        }
    }

    template<typename T>
    Atomic<T>::Atomic() {
        auto n = std::make_shared<T>();
        this->value.exchange(reinterpret_cast<uintptr_t>(new decltype(n)(std::move(n))));
    }

    template<typename T>
    template<typename T1>
    Atomic<T>::Atomic(T1 v) {
        auto n = std::make_shared<T>(std::move(v));
        this->value.exchange(reinterpret_cast<uintptr_t>(new decltype(n)(std::move(n))));
    }

    template<typename T>
    template<typename T1>
    requires(std::is_same_v<T1, std::string>)
    Atomic<T>::Atomic(const char *n) {
        this->operator=(n);
    }

    template<typename T>
    Atomic<T>::~Atomic() {
        delete reinterpret_cast<std::shared_ptr<T> *>(this->value.exchange(0));
    }

    template<typename T>
    AtomicReference <T> Atomic<T>::get() {
        return AtomicReference<T>(*reinterpret_cast<std::shared_ptr<T>*>(this->value.load()));
    }

    template<typename T>
    Atomic<T> & Atomic<T>::operator=(T n) {
        auto nv = std::make_shared<T>(std::move(n));
        auto v = this->value.exchange(reinterpret_cast<uintptr_t>(new decltype(nv)(std::move(nv))));
        delete reinterpret_cast<decltype(nv) *> (v);
        return *this;
    }

    template<typename T>
    template<typename T1> requires (std::is_same_v<T1, std::string>)
    Atomic<T> & Atomic<T>::operator=(const char *n) {
        auto nv = std::make_shared<T>(n);
        auto v = this->value.exchange(reinterpret_cast<uintptr_t>(new decltype(nv)(std::move(nv))));
        delete reinterpret_cast<decltype(nv) *> (v);
        return *this;
    }

    template<typename T>
    AtomicReference<T> Atomic<T>::operator*() {
        return this->get();
    }
}