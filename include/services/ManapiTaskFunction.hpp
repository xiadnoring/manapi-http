#pragma once

#include <functional>
#include "./ManapiTask.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::net {
    class function_task : public task {
    public:
        function_task(std::move_only_function <void ()> func);
        function_task (function_task &&task) noexcept;
        function_task &operator=(function_task &&task) noexcept;
        void doit () override;
    private:
        std::move_only_function <void ()> func;
    };
}