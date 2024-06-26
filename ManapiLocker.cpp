#include "ManapiLocker.h"

manapi::net::sem_locker::sem_locker() {
    if (sem_init(&m_sem, 0, 0) != 0)
        printf("sem init error\n");
}

manapi::net::sem_locker::~sem_locker() {
    sem_destroy(&m_sem);
}

bool manapi::net::sem_locker::wait() {
    return sem_wait(&m_sem);
}

bool manapi::net::sem_locker::add() {
    return sem_post(&m_sem);
}

manapi::net::mutex_locker::mutex_locker() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
        printf("mutex init error!");
}

manapi::net::mutex_locker::~mutex_locker() {
    pthread_mutex_destroy(&m_mutex);
}

bool manapi::net::mutex_locker::mutex_lock() {
    return pthread_mutex_lock(&m_mutex);
}

bool manapi::net::mutex_locker::mutex_unlock() {
    return pthread_mutex_unlock(&m_mutex);
}

manapi::net::cond_locker::cond_locker() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
        printf("mutex init error");

    if (pthread_cond_init(&m_cond, NULL) != 0) {
        pthread_mutex_destroy(&m_mutex);
        printf("cond init error");
    }
}

manapi::net::cond_locker::~cond_locker() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

bool manapi::net::cond_locker::wait() {
    int ans = 0;
    pthread_mutex_lock(&m_mutex);
    ans = pthread_cond_wait(&m_cond, &m_mutex);
    pthread_mutex_unlock(&m_mutex);
    return ans == 0;
}
// Wake Up the thread waiting for the condition variable
bool manapi::net::cond_locker::signal() {
    return pthread_cond_signal(&m_cond) == 0;
}
// Wake up threads waiting for condition variables
bool manapi::net::cond_locker::broadcast() {
    return pthread_cond_broadcast(&m_cond) == 0;
}



