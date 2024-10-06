#ifndef MANAPIHTTP_MANAPITHREADSAFE_H
#define MANAPIHTTP_MANAPITHREADSAFE_H

#include <mutex>
#include <condition_variable>
#include <unordered_map>

#include "ManapiBeforeDelete.hpp"

namespace manapi::net::utils {
    template <typename K, typename V> class safe_unordered_map;

    template <typename K, typename V>
    class safe_unordered_map {
    public:
        typedef std::unordered_map<K,V>::const_iterator const_iterator;
        typedef std::unordered_map<K,V>::iterator iterator;
        typedef std::unordered_map<K,V>::size_type size_type;
        typedef std::unordered_map<K,V>::value_type value_type;
        typedef std::unordered_map<K,V>::insert_return_type insert_return_type;
        typedef std::pair<manapi::net::utils::safe_unordered_map<K, V>::iterator, bool> insert_return_pair;

        safe_unordered_map();
        safe_unordered_map(std::initializer_list <std::pair <const K, V> > list);

        ~safe_unordered_map();

        insert_return_pair insert (const K &key, const V &value);
        insert_return_pair insert (value_type &&row);

        size_type erase (const K &key);

        iterator begin ();
        iterator end ();

        const_iterator begin () const;
        const_iterator end () const;

        iterator erase (const std::unordered_map<K, V>::iterator &it);

        bool contains (const K &key) const;

        V& at (const K &key);

        const V& at (const K &key) const;

        void lock ();

        void unlock ();

        manapi::net::utils::before_delete lock_guard ();

        void reset ();

        void wait_update ();

        [[nodiscard]] size_t size() const;

        bool try_lock ();
    private:
        std::mutex              m_update;
        std::condition_variable cv_update;
        std::mutex              locker;
        std::unordered_map<K,V> map;
    };


}
template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::safe_unordered_map() {}

template<typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::safe_unordered_map(std::initializer_list<std::pair<const K, V>> list) {
    for (const auto p: list)
    {
        map.insert(p);
    }
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::~safe_unordered_map() {

}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::reset() {
    map = {};

    cv_update.notify_all();
}

template<typename K, typename V>
void manapi::net::utils::safe_unordered_map<K, V>::wait_update() {
    std::unique_lock<std::mutex> lkq (m_update);
    cv_update.wait(lkq);
}

template<typename K, typename V>
size_t manapi::net::utils::safe_unordered_map<K, V>::size() const {
    return map.size();
}

template<typename K, typename V>
bool manapi::net::utils::safe_unordered_map<K, V>::try_lock() {
    return locker.try_lock();
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::insert_return_pair manapi::net::utils::safe_unordered_map<K, V>::insert (const K &key, const V &value) {
    return std::move(this->insert({key, value}));
}

template<typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::insert_return_pair manapi::net::utils::safe_unordered_map<K, V>::
insert(value_type &&row) {
    auto it = map.insert(std::move(row));

    cv_update.notify_all();

    return std::move(it);
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::size_type manapi::net::utils::safe_unordered_map<K, V>::erase (const K &key) {
    const auto it = map.erase(key);
    cv_update.notify_all();

    return it;
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::iterator manapi::net::utils::safe_unordered_map<K, V>::begin () {
    return map.begin();
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::iterator manapi::net::utils::safe_unordered_map<K, V>::end () {
    return map.end();
}

template<typename K, typename V>
typename manapi::net::utils::safe_unordered_map<K, V>::const_iterator manapi::net::utils::safe_unordered_map<K, V>::
begin() const {
    return map.begin();
}

template<typename K, typename V>
typename manapi::net::utils::safe_unordered_map<K, V>::const_iterator manapi::net::utils::safe_unordered_map<K, V>::
end() const {
    return map.end();
}

template <typename K, typename V>
manapi::net::utils::safe_unordered_map<K, V>::iterator manapi::net::utils::safe_unordered_map<K, V>::erase (const std::unordered_map<K, V>::iterator &it) {
    const auto n_it = map.erase(it);
    cv_update.notify_all();

    return n_it;
}

template<typename K, typename V>
bool manapi::net::utils::safe_unordered_map<K, V>::contains(const K &key) const {
    return map.contains(key);
}

template<typename K, typename V>
V& manapi::net::utils::safe_unordered_map<K, V>::at(const K &key) {
    return map.at(key);
}

template<typename K, typename V>
const V & manapi::net::utils::safe_unordered_map<K, V>::at(const K &key) const {
    return map.at(key);
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
