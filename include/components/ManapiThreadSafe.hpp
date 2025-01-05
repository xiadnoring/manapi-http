#ifndef MANAPIHTTP_MANAPITHREADSAFE_H
#define MANAPIHTTP_MANAPITHREADSAFE_H

#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <map>

#include "../ManapiBeforeDelete.hpp"

namespace manapi::net::utils {
    template <typename K, typename V, class C = std::unordered_map <K, V>>
    class atomic_map : public C {
    public:
        typedef typename C::const_iterator const_iterator;
        typedef typename C::iterator iterator;
        typedef typename C::size_type size_type;
        typedef typename C::value_type value_type;
        typedef typename C::insert_return_type insert_return_type;
        typedef std::pair<typename manapi::net::utils::atomic_map<K, V>::iterator, bool> insert_return_pair;

        atomic_map();
        atomic_map(std::initializer_list <std::pair <const K, V> > list);

        insert_return_pair insert (K key, V value);
        insert_return_pair insert (value_type &&row);

        size_type erase (const K &key);

        iterator erase (C::iterator it);

        void lock ();

        void unlock ();

        manapi::before_delete lock_guard ();

        void reset ();

        void wait_update ();

        bool try_lock ();
    private:
        std::mutex              m_update;
        std::condition_variable cv_update;
        std::mutex              locker;
    };


}

template<typename K, typename V, class C>
void manapi::net::utils::atomic_map<K, V, C>::reset() {
    C::clear();
    cv_update.notify_all();
}

template<typename K, typename V, class C>
void manapi::net::utils::atomic_map<K, V, C>::wait_update() {
    std::unique_lock<std::mutex> lkq (m_update);
    cv_update.wait(lkq);
}

template<typename K, typename V, class C>
bool manapi::net::utils::atomic_map<K, V, C>::try_lock() {
    return locker.try_lock();
}

template<typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::atomic_map() {}

template<typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::atomic_map(std::initializer_list<std::pair<const K, V>> list) : C (std::move(list)) {}

template <typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::insert_return_pair manapi::net::utils::atomic_map<K, V, C>::insert (K key, V value) {
    auto it = C::insert({std::move(key), std::move(value)});
    cv_update.notify_all();
    return std::move(it);
}

template<typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::insert_return_pair manapi::net::utils::atomic_map<K, V, C>::
insert(value_type &&row) {
    auto it = C::insert(std::forward<decltype(row)>(row));
    cv_update.notify_all();
    return std::move(it);
}

template <typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::size_type manapi::net::utils::atomic_map<K, V, C>::erase (const K &key) {
    const auto it = C::erase(key);
    cv_update.notify_all();

    return it;
}

template <typename K, typename V, class C>
manapi::net::utils::atomic_map<K, V, C>::iterator manapi::net::utils::atomic_map<K, V, C>::erase (C::iterator it) {
    const auto n_it = C::erase(it);
    cv_update.notify_all();
    return n_it;
}

template<typename K, typename V, class C>
void manapi::net::utils::atomic_map<K, V, C>::unlock() {
    // printf("UNBLOCKED\n");
    locker.unlock();
}

template<typename K, typename V, class C>
manapi::before_delete manapi::net::utils::atomic_map<K, V, C>::lock_guard() {
    lock();
    manapi::before_delete bd([this] () -> void { unlock(); });
    return std::move(bd);
}

template<typename K, typename V, class C>
void manapi::net::utils::atomic_map<K, V, C>::lock() {
    // printf("BLOCKED\n");
    locker.lock();
}

#endif //MANAPIHTTP_MANAPITHREADSAFE_H
