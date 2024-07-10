#ifndef MANAPIHTTP_MANAPITASKFUNCTION_H
#define MANAPIHTTP_MANAPITASKFUNCTION_H

#include <functional>
#include "ManapiTask.h"

namespace manapi::net {
    class function_task : public task {
    public:
        explicit function_task(const std::function <void ()> &_func);
        void doit () override;
    private:
        std::function <void ()> func;
    };
}

#endif //MANAPIHTTP_MANAPITASKFUNCTION_H
