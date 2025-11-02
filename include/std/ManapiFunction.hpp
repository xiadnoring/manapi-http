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
    class fixed_function;

    namespace impl {
        template <typename ReturnType, typename... Args>
        struct FunctorHolderBase
        {
            virtual ~FunctorHolderBase() {}

            virtual ReturnType operator()(Args&&...) = 0;

            virtual void move (void *data) MANAPIHTTP_NOEXCEPT = 0;
        };

        template <typename Functor, typename Result, typename... Arguments>
        struct FunctorHolder final : FunctorHolderBase<Result, Arguments...>
        {
            FunctorHolder (Functor func) : f (std::move(func)) {}

            Result operator()(Arguments&&... args) override
            {
                return f (std::forward<Arguments> (args)...);
            }

            void move (void *data) MANAPIHTTP_NOEXCEPT override {
                new (data) FunctorHolder (std::move(this->f));
            }

            Functor f;
        };
    }

    template<std::size_t Size, typename Result, typename... Arguments>
    class move_only_function_base {
        bool flg;
        union function_data {
            char stack[Size];
        };
    public:
        move_only_function_base () : data(), flg(false) {}

        move_only_function_base (std::nullptr_t) : data(), flg(false) {}

        template <typename Functor>
        move_only_function_base (Functor &&f) : flg(false) {
            if constexpr (sizeof (impl::FunctorHolder<Functor, Result, Arguments...>) <= sizeof (this->data.stack)) {
                memset (&this->data.stack, '\0', sizeof (this->data.stack));
                new (this->data.stack) impl::FunctorHolder<Functor, Result, Arguments...> (std::forward<Functor>(f));
                this->flg = true;
            }
            else {
                static_assert("manapi func:params are too large for Static Allocation");
            }
        }

        ~move_only_function_base() {
            if (*this) {
                auto const holder = reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(this->data.stack);
                holder->~FunctorHolderBase();
            }

        }

        MANAPIHTTP_NODISCARD operator bool () const {
            return this->flg;
        }

        Result operator() (Arguments&&... args) {
            if (!this->flg)
                throw std::bad_function_call ();
            return (*reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(this->data.stack)) (std::forward<Arguments> (args)...);
        }

        move_only_function_base (move_only_function_base&& other) MANAPIHTTP_NOEXCEPT {
            memset (&this->data.stack, '\0', sizeof (this->data.stack));
            if (other) {
                reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(other.data.stack)->move(this->data.stack);
                reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(other.data.stack)->~FunctorHolderBase();
                memset(&other.data.stack, '\0', sizeof (other.data.stack));
            }
            this->flg = other.flg;
            other.flg = false;
        }

        move_only_function_base& operator= (move_only_function_base&& other) MANAPIHTTP_NOEXCEPT {
            if (this == &other) {
                return *this;
            }

            if (*this) {
                reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(this->data.stack)->~FunctorHolderBase();
                memset(&this->data.stack, '\0', sizeof (this->data.stack));
            }
            if (other) {
                reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(other.data.stack)->move(this->data.stack);
                reinterpret_cast<impl::FunctorHolderBase<Result, Arguments...> *>(other.data.stack)->~FunctorHolderBase();
                memset(&other.data.stack, '\0', sizeof (other.data.stack));
            }
            this->flg = other.flg;
            other.flg = false;
            return *this;
        }
    protected:
        function_data data;
    };

    template<typename Result, typename... Arguments>
    class fixed_function<Result(Arguments...)> : public move_only_function_base<64, Result, Arguments...> {
        using Base = move_only_function_base<64, Result, Arguments...>;
    public:
        fixed_function () = default;

        fixed_function (std::nullptr_t) : Base(nullptr) {}

        template <typename Functor>
        fixed_function (Functor &&f) : Base(std::forward<decltype(f)>(f)) {}

        fixed_function (fixed_function<Result(Arguments...)> &&n) MANAPIHTTP_NOEXCEPT = default;

        fixed_function& operator= (fixed_function<Result(Arguments...)> &&n) MANAPIHTTP_NOEXCEPT = default;
    };

    template<typename Result, typename ...Arguments>
    auto static_function (auto cb) {
        return manapi::move_only_function_base<sizeof (impl::FunctorHolder<decltype (cb), Result, Arguments...>), Result, Arguments...> (std::move(cb));
    }
}