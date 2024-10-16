//
// Created by Timur on 19.05.2024.
//

#include <mutex>
#include <condition_variable>

#ifndef MANAPIHTTP_MANAPITASK_H
#define MANAPIHTTP_MANAPITASK_H

namespace manapi::net {
    class task {
    public:
        task();

        virtual ~task();

        virtual void doit();

        void stop ();

        bool to_retry = false;
    };
}

#endif //MANAPIHTTP_MANAPITASK_H
