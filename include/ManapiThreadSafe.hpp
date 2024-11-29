#ifndef MANAPIHTTP_MANAPITHREADSAFE_H
#define MANAPIHTTP_MANAPITHREADSAFE_H

#include <mutex>
#include <condition_variable>
#include <unordered_map>

#include "ManapiBeforeDelete.hpp"

namespace manapi::net::utils {
    template <typename K, typename V> class safe_unordered_map;

    template <typename K, typename V>
    class safe_unordered_map : public std::unordered_map<K, V> {
    public:
        typedef std::unordered_map<K,V>::const_iterator const_iterator;
        typedef std::unordered_map<K,V>::iterator iterator;
        typedef std::unordered_map<K,V>::size_type size_type;
        typedef std::unordered_map<K,V>::value_type value_type;
        typedef std::unordered_map<K,V>::insert_return_type insert_return_type;
        typedef std::pair<manapi::net::utils::safe_unordered_map<K, V>::iterator, bool> insert_return_pair;

        safe_unordered_map();
        safe_unordered_map(std::initializer_list <std::pair <const K, V> > list);

        insert_return_pair insert (K key, V value);
        insert_return_pair insert (value_type &&row);

        size_type erase (const K &key);

        iterator erase (std::unordered_map<K, V>::iterator it);

        void lock ();

        void unlock ();

        manapi::net::utils::before_delete lock_guard ();

        void reset ();

        void wait_update ();

        bool try_lock ();
    private:
        std::mutex              m_update;
        std::condition_variable cv_update;
        std::mutex              locker;
    };


}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::reset() {
    std::unordered_map<K, V>::clear();
    cv_update.notify_all();
}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::wait_update() {
    std::unique_lock<std::mutex> lkq (m_update);
    cv_update.wait(lkq);
}

template<typename K, typename V>
bool manapi::net::utils::safe_unordered_map<K, V>::try_lock() {
    return locker.try_lock();
}

template<typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::safe_unordered_map() {}

template<typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::safe_unordered_map(std::initializer_list<std::pair<const K, V>> list) : std::unordered_map<K, V>(std::move(list)) {}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::insert_return_pair manapi::net::utils::safe_unordered_map<K, V>::insert (K key, V value) {
    auto it = std::unordered_map<K, V>::insert({std::move(key), std::move(value)});
    cv_update.notify_all();
    return std::move(it);
}

template<typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::insert_return_pair manapi::net::utils::safe_unordered_map<K, V>::
insert(value_type &&row) {
    auto it = std::unordered_map<K, V>::insert(std::forward<decltype(row)>(row));
    cv_update.notify_all();
    return std::move(it);
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::size_type manapi::net::utils::safe_unordered_map<K, V>::erase (const K &key) {
    const auto it = std::unordered_map<K, V>::erase(key);
    cv_update.notify_all();

    return it;
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::iterator manapi::net::utils::safe_unordered_map<K, V>::erase (std::unordered_map<K, V>::iterator it) {
    const auto n_it = std::unordered_map<K, V>::erase(it);
    cv_update.notify_all();
    return n_it;
}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::unlock() {
    // printf("UNBLOCKED\n");
    locker.unlock();
}

template<typename K, typename V>
manapi::net::utils::before_delete manapi::net::utils::safe_unordered_map<K, V>::lock_guard() {
    lock();
    manapi::net::utils::before_delete bd([this] () -> void { unlock(); });
    return std::move(bd);
}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::lock() {
    // printf("BLOCKED\n");
    locker.lock();
}

#endif //MANAPIHTTP_MANAPITHREADSAFE_H
