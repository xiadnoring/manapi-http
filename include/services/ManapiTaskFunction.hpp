#pragma once

#include <functional>
#include "ManapiTask.hpp"

namespace manapi::net {
    class function_task : public task {
    public:
        explicit function_task(const std::function <void ()> &func);
        function_task (function_task &&task) noexcept;
        function_task &operator=(function_task &&task) noexcept;
        void doit () override;
    private:
        std::function <void ()> func;
    };
}