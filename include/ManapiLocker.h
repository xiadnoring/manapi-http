//
// Created by Timur on 19.05.2024.
//

#ifndef MANAPIHTTP_MANAPILOCKER_H
#define MANAPIHTTP_MANAPILOCKER_H

#include <pthread.h>
#include <cstdio>
#include <semaphore>

namespace manapi::net {
    /** class of semaphore */
    class sem_locker {
    private:
        sem_t m_sem;
    public:
        sem_locker();
        ~sem_locker();
        bool wait();
        bool add();
    };

    /** mutual exclusion locker */
    class mutex_locker {
    private:
        pthread_mutex_t m_mutex;
    public:
        mutex_locker();
        ~mutex_locker();
        bool mutex_lock();
        bool mutex_unlock();
    };

    /** condition variable locker */
    class cond_locker {
    private:
        pthread_mutex_t m_mutex;
        pthread_cond_t  m_cond;
    public:
        cond_locker();
        ~cond_locker();
        bool wait();
        bool signal();
        bool broadcast();
    };
}

#endif //MANAPIHTTP_MANAPILOCKER_H
