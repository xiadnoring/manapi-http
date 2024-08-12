#ifndef MANAPIHTTP_MANAPITHREADSAFE_H
#define MANAPIHTTP_MANAPITHREADSAFE_H

#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace manapi::utils {
    template <typename K, typename V> class safe_unordered_map;

    template <typename K, typename V>
    class safe_unordered_map {
    public:
        safe_unordered_map();
        ~safe_unordered_map();

        void insert (const K &key, const V &value);

        std::unordered_map<K,V>::size_type erase (const K &key);

        std::unordered_map<K, V>::iterator begin ();

        std::unordered_map<K, V>::iterator end ();

        std::unordered_map<K, V>::iterator erase (const std::unordered_map<K, V>::iterator &it);

        bool contains (const K &key);

        V& at (const K &key);

        void lock ();

        void unlock ();

        void reset ();

        void wait_update ();
    private:
        std::mutex              m_update;
        std::condition_variable cv_update;
        std::mutex              locker;
        std::unordered_map<K,V> map;
    };


}
template <typename K, typename V>
manapi::utils::safe_unordered_map<K, V>::safe_unordered_map() {}

template <typename K, typename V>
manapi::utils::safe_unordered_map<K, V>::~safe_unordered_map() {}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::reset() {
    map = {};

    cv_update.notify_all();
}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::wait_update() {
    std::unique_lock<std::mutex> lkq (m_update);
    cv_update.wait(lkq);
}

template <typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::insert (const K &key, const V &value) {
    // auto release at the end
    map.insert({key, value});

    cv_update.notify_all();
}

template <typename K, typename V>
std::unordered_map<K,V>::size_type manapi::utils::safe_unordered_map<K, V>::erase (const K &key) {
    const auto it = map.erase(key);
    cv_update.notify_all();

    return it;
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::begin () {
    return map.begin();
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::end () {
    return map.end();
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::erase (const std::unordered_map<K, V>::iterator &it) {
    const auto n_it = map.erase(it);
    cv_update.notify_all();

    return n_it;
}

template<typename K, typename V>
bool manapi::utils::safe_unordered_map<K, V>::contains(const K &key) {
    return map.contains(key);
}

template<typename K, typename V>
V& manapi::utils::safe_unordered_map<K, V>::at(const K &key) {
    return map.at(key);
}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::unlock() {
    // printf("UNBLOCKED\n");
    locker.unlock();
}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::lock() {
    // printf("BLOCKED\n");
    locker.lock();
}

#endif //MANAPIHTTP_MANAPITHREADSAFE_H
