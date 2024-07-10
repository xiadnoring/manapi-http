#ifndef MANAPIHTTP_MANAPITHREADSAFE_H
#define MANAPIHTTP_MANAPITHREADSAFE_H

#include <mutex>
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

        void block ();

        void unblock ();
    private:
        std::condition_variable cv;
        std::mutex              locker;
        std::mutex              m;
        std::unordered_map<K,V> map;
    };


}
template <typename K, typename V>
manapi::utils::safe_unordered_map<K, V>::safe_unordered_map() {}

template <typename K, typename V>
manapi::utils::safe_unordered_map<K, V>::~safe_unordered_map() {}

template <typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::insert (const K &key, const V &value) {
    // auto release at the end
    std::lock_guard <std::mutex> lock (m);
    map.insert({key, value});
}

template <typename K, typename V>
std::unordered_map<K,V>::size_type manapi::utils::safe_unordered_map<K, V>::erase (const K &key) {
    std::lock_guard <std::mutex> lock (m);
    return map.erase(key);
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::begin () {
    std::lock_guard <std::mutex> lock (m);
    return map.begin();
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::end () {
    std::lock_guard <std::mutex> lock (m);
    return map.end();
}

template <typename K, typename V>
std::unordered_map<K, V>::iterator manapi::utils::safe_unordered_map<K, V>::erase (const std::unordered_map<K, V>::iterator &it) {
    std::lock_guard <std::mutex> lock (m);
    return map.erase(it);
}

template<typename K, typename V>
bool manapi::utils::safe_unordered_map<K, V>::contains(const K &key) {
    std::lock_guard <std::mutex> lock (m);
    return map.contains(key);
}

template<typename K, typename V>
V& manapi::utils::safe_unordered_map<K, V>::at(const K &key) {
    std::lock_guard<std::mutex> lock (m);
    return map.at(key);
}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::unblock() {
    //printf("UNBLOCKED\n");
    locker.unlock();
}

template<typename K, typename V>
void manapi::utils::safe_unordered_map<K, V>::block() {
    //printf("BLOCKED\n");
    locker.lock();
}

#endif //MANAPIHTTP_MANAPITHREADSAFE_H
