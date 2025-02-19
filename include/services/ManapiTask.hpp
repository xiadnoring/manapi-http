#pragma once

#include <mutex>
#include <condition_variable>

#include "../ManapiUtils.hpp"
namespace manapi {
    class task {
    public:
        task();
        task(task &&n) noexcept;
        task &operator=(task &&n) noexcept;
        virtual ~task();
        virtual void doit();
        void stop ();
    };
}