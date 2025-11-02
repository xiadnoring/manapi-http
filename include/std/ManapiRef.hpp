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
                this->src = n.src;
                n.src = nullptr;
            }
            return *this;
        }

        reference (const reference &n) {
            this->src = n.src;
            ++this->src->refcnt;
        }

        reference& operator= (const reference &n) {
            if (this->src != n.src) {
                this->src = n.src;
                ++this->src->refcnt;
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
            this->reset();
            this->src = n;
            if (this->src) {
                ++this->src->refcnt;
            }
        }

        T *operator-> () MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        T *get () MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        MANAPIHTTP_NODISCARD const T *get () const MANAPIHTTP_NOEXCEPT {
            return this->src;
        }

        MANAPIHTTP_NODISCARD const T *operator->() const MANAPIHTTP_NOEXCEPT {
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

        manapi::error::status ref () MANAPIHTTP_NOEXCEPT {
            if (this->src) {
                ++this->src->refcnt;
                return manapi::error::status_ok();
            }
            return manapi::error::status_internal("ref:Object doesn't exist");
        }

        void unref () MANAPIHTTP_NOEXCEPT {
            if (this->src) {
                if (!--this->src->refcnt) {
                    delete this->src;
                    this->src = nullptr;
                }
            }
        }
    private:
        T *src;
    };
}