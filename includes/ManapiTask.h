//
// Created by Timur on 19.05.2024.
//

#ifndef MANAPIHTTP_MANAPITASK_H
#define MANAPIHTTP_MANAPITASK_H

const unsigned long long BUFFER_SIZE = 4096;

namespace manapi::net {
    class task {
    public:
        task();

        virtual ~task();

        virtual void doit();

        void stop ();
    };
}

#endif //MANAPIHTTP_MANAPITASK_H
