/**
 * @file std/ManapiFunction.hpp
 * @brief Provides a function interface which works like std::move_only_function and supports
 * 56 byte instead of 8-16 byte.
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <functional>
#include <cstring>

#include "../ManapiUtils.hpp"

namespace manapi {
    template<typename>
    class static_function;

    template<std::size_t Size, bool StaticOnly, typename Result, typename... Arguments>
    class move_only_function_base {
        template <typename ReturnType, typename... Args>
        struct FunctorHolderBase
        {
            virtual ~FunctorHolderBase() {}

            virtual ReturnType operator()(Args&&...) = 0;

            virtual void move (void *data) MANAPIHTTP_NOEXCEPT = 0;
        };

        template <typename Functor, typename ReturnType, typename... Args>
        struct FunctorHolder final : FunctorHolderBase<Result, Arguments...>
        {
            FunctorHolder (Functor func) : f (std::move(func)) {}

            ReturnType operator()(Args&&... args) override
            {
                return f (std::forward<Arguments> (args)...);
            }

            void move (void *data) MANAPIHTTP_NOEXCEPT override {
                new (data) FunctorHolder (std::move(this->f));
            }

            Functor f;
        };

        union function_data {
            char stack[Size];
        };
    public:
        move_only_function_base () : functorHolderPtr(nullptr), data() {}

        move_only_function_base (std::nullptr_t) : functorHolderPtr(nullptr), data() {}

        template <typename Functor>
        move_only_function_base (Functor &&f) {
            if constexpr (sizeof (FunctorHolder<Functor, Result, Arguments...>) <= sizeof (this->data.stack)) {
                memset (&this->data.stack, '\0', sizeof (this->data.stack));
                this->functorHolderPtr = (decltype (this->functorHolderPtr)) std::addressof (this->data.stack);
                new (this->functorHolderPtr) FunctorHolder<Functor, Result, Arguments...> (std::forward<Functor>(f));
            }
            else {
                static_assert(!StaticOnly, "manapi func:params are too large for Static Allocation");
                this->functorHolderPtr = new FunctorHolder<Functor, Result, Arguments...> (std::forward<Functor>(f));
            }
        }

        ~move_only_function_base() {
            if (this->functorHolderPtr == (decltype (this->functorHolderPtr)) std::addressof (this->data.stack))
                this->functorHolderPtr->~FunctorHolderBase();
            else
                delete this->functorHolderPtr;
        }

        MANAPIHTTP_NODISCARD operator bool () const {
            return !!this->functorHolderPtr;
        }

        Result operator() (Arguments&&... args) {
            if (!this->functorHolderPtr)
                throw std::bad_function_call ();

            return (*this->functorHolderPtr) (std::forward<Arguments> (args)...);
        }

        move_only_function_base (move_only_function_base&& other) MANAPIHTTP_NOEXCEPT {
            if (other.functorHolderPtr == (decltype (other.functorHolderPtr)) std::addressof (other.data.stack)) {
                memset (&this->data.stack, '\0', sizeof (this->data.stack));
                this->functorHolderPtr = (decltype (this->functorHolderPtr))std::addressof(this->data.stack);
                other.functorHolderPtr->move(this->functorHolderPtr);
                other.functorHolderPtr->~FunctorHolderBase();
            }
            else {
                this->functorHolderPtr = other.functorHolderPtr;
            }
            other.functorHolderPtr = nullptr;
        }

        move_only_function_base& operator= (move_only_function_base&& other) MANAPIHTTP_NOEXCEPT {
            if (other.functorHolderPtr == (decltype (other.functorHolderPtr)) std::addressof (other.data.stack)) {
                memset (&this->data.stack, '\0', sizeof (this->data.stack));
                this->functorHolderPtr = (decltype (this->functorHolderPtr))std::addressof(this->data.stack);
                other.functorHolderPtr->move(this->functorHolderPtr);
                other.functorHolderPtr->~FunctorHolderBase();
            }
            else {
                this->functorHolderPtr = other.functorHolderPtr;
            }
            other.functorHolderPtr = nullptr;
            return *this;
        }
    protected:
        FunctorHolderBase<Result, Arguments...>* functorHolderPtr;
        function_data data;
    };

    template<typename Result, typename... Arguments>
    class static_function<Result(Arguments...)> : public move_only_function_base<64, true, Result, Arguments...> {
        using Base = move_only_function_base<64, true, Result, Arguments...>;
    public:
        static_function () = default;

        static_function (std::nullptr_t) : Base(nullptr) {}

        template <typename Functor>
        static_function (Functor &&f) : Base(std::forward<decltype(f)>(f)) {}

        static_function (static_function &&n) MANAPIHTTP_NOEXCEPT = default;

        static_function& operator= (static_function &&n) MANAPIHTTP_NOEXCEPT = default;

        MANAPIHTTP_NODISCARD operator bool () const {
            return !!this->functorHolderPtr;
        }
    };
}