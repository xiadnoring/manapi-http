#pragma once

#include <utility>

#include "./../ManapiUtils.hpp"

namespace manapi {
    namespace reference_impl {
        template <typename T, typename = void> struct HasRefCnt : std::false_type {};

        template <typename T> struct HasRefCnt<T, decltype(void(std::declval<T>().refcnt))> : std::true_type {};
    }

    template<typename T, std::enable_if_t<reference_impl::HasRefCnt<T>::value, std::nullptr_t> = nullptr>
    class reference {
    public:
        reference () {
            this->src = nullptr;
        }

        reference (T *n) {
            this->src = n;
            if (n) {
                ++n->refcnt;
            }
        }

        ~reference() {
            this->reset();
        }

        reference (reference &&n) MANAPIHTTP_NOEXCEPT {
            this->src = n.src;
            n.src = nullptr;
        }

        reference& operator= (reference &&n) MANAPIHTTP_NOEXCEPT {
            if (this != &n) {
                if (n.src) {
                    n.src->refcnt++;
                    this->reset();
                    this->src = n.src;
                    n.reset();
                }
                else {
                    this->reset();
                }
            }
            return *this;
        }

        reference (const reference &n) : src(nullptr) {
            this->reset(n.src);
        }

        reference& operator= (const reference &n) {
            if (this != &n) {
                this->reset(n.src);
            }
            return *this;
        }

        void reset () MANAPIHTTP_NOEXCEPT {
            if (this->src) {
                if (!--this->src->refcnt) {
                    delete this->src;
                }
                this->src = nullptr;
            }
        }

        void reset (T *n) MANAPIHTTP_NOEXCEPT {
            if (n) {
                n->refcnt++;
                this->reset();
                this->src = n;
            }
            else {
                this->reset();
            }
        }

        T *operator-> () MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        T *get () MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        MANAPIHTTP_NODISCARD T *get () const MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        MANAPIHTTP_NODISCARD T *operator->() const MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        T &operator *() MANAPIHTTP_NOEXCEPT {
            return *this->src;
        }

        MANAPIHTTP_NODISCARD const T &operator*() const MANAPIHTTP_NOEXCEPT {
            return *this->src;
        }

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT {
            return !!this->src;
        }

        manapi::status ref () const MANAPIHTTP_NOEXCEPT {
            if (this->src) {
                ++this->src->refcnt;
                return manapi::status_ok();
            }
            return manapi::status_internal("ref:Object doesn't exist");
        }

        void unref () const MANAPIHTTP_NOEXCEPT {
            if (this->src) {
                if (!--this->src->refcnt) {
                    delete this->src;
                    this->src = nullptr;
                }
            }
        }
    private:
        mutable T *src;
    };
}