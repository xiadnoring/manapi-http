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
    template <typename>
    class move_only_function;

    template<typename Result, typename... Arguments>
    class move_only_function<Result(Arguments...)> {
        template <typename ReturnType, typename... Args>
        struct FunctorHolderBase
        {
            virtual ~FunctorHolderBase() {};

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
            char stack[64];
        };
    public:
        move_only_function () : functorHolderPtr(nullptr), data() {}

        move_only_function (std::nullptr_t) : functorHolderPtr(nullptr), data() {}

        template <typename Functor>
        move_only_function (Functor f) {
            if constexpr (sizeof (FunctorHolder<Functor, Result, Arguments...>) <= sizeof (this->data.stack)) {
                memset (&this->data.stack, '\0', sizeof (this->data.stack));
                this->functorHolderPtr = (decltype (this->functorHolderPtr)) std::addressof (this->data.stack);
                new (this->functorHolderPtr) FunctorHolder<Functor, Result, Arguments...> (std::move(f));
            }
            else
                this->functorHolderPtr = new FunctorHolder<Functor, Result, Arguments...> (std::move(f));
        }

        ~move_only_function() {
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

        move_only_function (move_only_function&& other) MANAPIHTTP_NOEXCEPT {
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

        move_only_function& operator= (move_only_function&& other) MANAPIHTTP_NOEXCEPT {
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
    private:
        FunctorHolderBase<Result, Arguments...>* functorHolderPtr;
        function_data data;
    };
}