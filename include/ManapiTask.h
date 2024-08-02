//
// Created by Timur on 19.05.2024.
//

#ifndef MANAPIHTTP_MANAPITASK_H
#define MANAPIHTTP_MANAPITASK_H

namespace manapi::net {
    class task {
    public:
        task();

        virtual ~task();

        virtual void doit();

        void stop ();

        bool to_delete = true;
    };
}

#endif //MANAPIHTTP_MANAPITASK_H
