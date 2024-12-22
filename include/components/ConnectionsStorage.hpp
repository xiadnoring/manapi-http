#ifndef MANAPIHTTP_CONNECTIONSSTORAGE_HPP
#define MANAPIHTTP_CONNECTIONSSTORAGE_HPP

#include <memory>
#include <mutex>
#include <map>
#include <condition_variable>

#include "./Atomic.hpp"
#include "../ManapiHttpTypes.hpp"

namespace manapi::net::worker {
    template <typename T>
    class connection_storage {
    public:
        connection_storage ();
        connection_storage (T* s, Atomic <ssize_t> &count, std::condition_variable &cv);
        connection_storage (connection_storage &&n) noexcept;
        connection_storage &operator=(connection_storage &&n) noexcept;
        ~connection_storage();
        bool operator==(T *s) const;
        T *operator->();
        T operator*();
    private:
        void _check_contains ();
        T *s = nullptr;
        std::condition_variable *cv = nullptr;
        Atomic <ssize_t> *count = nullptr;
    };
    template <typename T>
    class connection_iterator {
    public:
        connection_iterator (T s, Atomic <ssize_t> &count, std::condition_variable &cv);
        connection_iterator (connection_iterator &&n) noexcept ;
        ~connection_iterator();
        T::value_type *operator->();
        T::value_type operator*();
        T unsafe_get ();
        connection_iterator &operator= (connection_iterator &&n) noexcept ;
        connection_iterator &operator++();
        connection_iterator &operator--();
        bool operator==(const connection_iterator &) const;
    private:
        void _check_contains ();
        T iterator;
        std::condition_variable *cv = nullptr;
        Atomic <ssize_t> *count = nullptr;
    };
    template <typename K, typename V>
    class connections_storage {
    public:
        typedef std::unordered_map <K, V> vmap;
        typedef connection_storage<V> value;

        connections_storage ();
        ~connections_storage ();
        connection_storage<V> get (const K& key);
        void insert (vmap::value_type &&row);
        void update (const std::function<void(vmap &n)> &cb);
        typename vmap::size_type erase (const vmap::key_type &key);
        connection_iterator<typename vmap::iterator> find (const vmap::key_type &key);
        connection_iterator<typename vmap::iterator> end ();
        connection_iterator<typename vmap::iterator> begin ();
        bool contains (const vmap::key_type &key);
        vmap::size_type size ();
        bool empty ();
        utils::before_delete large_request ();
    private:
        void _wait_editable ();
        vmap storage;
        Atomic <ssize_t> count = static_cast<long int>(0);
        std::condition_variable cv;
        std::mutex mx;
        std::mutex lmx;
        std::mutex gmx;
    };

    template<typename T>
    connection_storage<T>::connection_storage() {
        this->count = nullptr;
        this->s = nullptr;
        this->cv = nullptr;
    }

    template<typename T>
    connection_storage<T>::connection_storage(T *s, Atomic <ssize_t> &count, std::condition_variable &cv) {
        this->count = &count;
        this->s = s;
        this->cv = &cv;

        if(this->count != nullptr) { ++(*this->count); }
    }

    template<typename T>
    connection_storage<T>::connection_storage(connection_storage &&n) noexcept {
        this->operator= (std::forward<decltype(n)>(n));
    }

    template<typename T>
    connection_storage<T> & connection_storage<T>::operator=(connection_storage &&n) noexcept {
        this->count = n.count;
        this->s = n.s;
        this->cv = n.cv;

        n.s = nullptr;
        n.count = nullptr;
        n.cv = nullptr;
        return *this;
    }

    template<typename T>
    connection_storage<T>::~connection_storage() {
        if (this->count != nullptr) { --(*this->count); }
        if (this->cv != nullptr) { this->cv->notify_all(); }
    }

    template<typename T>
    bool connection_storage<T>::operator==(T *s) const {
        return this->s == s;
    }

    template<typename T>
    T *connection_storage<T>::operator->() {
        this->_check_contains ();
        return s;
    }

    template<typename T>
    T connection_storage<T>::operator*() {
        this->_check_contains ();
        return *this->operator->();
    }

    template<typename T>
    void connection_storage<T>::_check_contains() {
        if (s == nullptr) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_STORAGE_OBJECT_IS_NULL, "That storage not contains anything");
        }
    }

    template<typename T>
    connection_iterator<T>::connection_iterator(T s, Atomic <ssize_t> &count, std::condition_variable &cv) {
        this->count = &count;
        this->iterator = std::move(s);
        this->cv = &cv;

        if(this->count != nullptr) { ++(*this->count); }
    }

    template<typename T>
    connection_iterator<T>::connection_iterator(connection_iterator &&n) noexcept {
        this->operator=(std::forward<decltype(n)>(n));
    }

    template<typename T>
    connection_iterator<T>::~connection_iterator() {
        if(this->count != nullptr) { --(*this->count); }
        if(this->cv != nullptr) { this->cv->notify_all(); }
    }

    template<typename T>
    T::value_type *connection_iterator<T>::operator->() {
        this->_check_contains();
        return &*this->iterator;
    }

    template<typename T>
    T::value_type connection_iterator<T>::operator*() {
        this->_check_contains();
        return *this->operator->();
    }

    template<typename T>
    T connection_iterator<T>::unsafe_get() {
        this->_check_contains();
        return this->iterator;
    }

    template<typename T>
    connection_iterator<T> & connection_iterator<T>::operator=(connection_iterator &&n) noexcept {
        this->iterator = n.iterator;
        this->count = n.count;
        this->cv = n.cv;
        n.cv = nullptr;
        n.count = nullptr;
        return *this;
    }

    template<typename T>
    connection_iterator<T> & connection_iterator<T>::operator++() {
        this->_check_contains();
        ++this->iterator;
        return *this;
    }

    template<typename T>
    connection_iterator<T> & connection_iterator<T>::operator--() {
        this->_check_contains();
        --this->iterator;
        return *this;
    }

    template<typename T>
    bool connection_iterator<T>::operator==(const connection_iterator &n) const {
        return this->iterator == n.iterator;
    }

    template<typename T>
    void connection_iterator<T>::_check_contains() {
        if (count == nullptr) { THROW_MANAPIHTTP_EXCEPTION2(ERR_STORAGE_OBJECT_IS_NULL, "class connection_iterator(...): That storage not contains anything"); }
    }

    template<typename K, typename V>
    connections_storage<K,V>::connections_storage() {}

    template<typename K, typename V>
    connections_storage<K,V>::~connections_storage() {
        std::lock_guard<std::mutex> lk (this->gmx);
        this->_wait_editable ();
    }

    template<typename K, typename V>
    connection_storage<V> connections_storage<K, V>::get(const K &key) {
        std::lock_guard<std::mutex> lk (this->gmx);
        auto it = storage.find(key);
        if (it == storage.end()) {
            return connection_storage<V> ();
        }
        return connection_storage<V> (&it->second, count, cv);
    }

    template<typename K, typename V>
    void connections_storage<K, V>::insert(vmap::value_type &&row) {
        std::lock_guard<std::mutex> llk (this->lmx);
        std::lock_guard<std::mutex> lk (this->gmx);
        this->_wait_editable ();
        storage.insert(std::forward<decltype(row)>(row));
    }

    template<typename K, typename V>
    void connections_storage<K, V>::update(const std::function<void(vmap &n)> &cb) {
        std::lock_guard<std::mutex> llk (this->lmx);
        std::lock_guard<std::mutex> lk (this->gmx);
        this->_wait_editable ();
        cb (this->storage);
    }


    template<typename K, typename V>
    typename connections_storage<K, V>::vmap::size_type connections_storage<K, V>::erase(const vmap::key_type &key) {
        std::lock_guard<std::mutex> llk (this->lmx);
        std::lock_guard<std::mutex> lk (this->gmx);
        this->_wait_editable ();
        return storage.erase(key);
    }

    template<typename K, typename V>
    connection_iterator<typename connections_storage<K, V>::vmap::iterator> connections_storage<K, V>::find(const vmap::key_type &key) {
        std::lock_guard<std::mutex> lk (this->gmx);
        return std::move(connection_iterator<typename vmap::iterator> (storage.find(key), count, cv));
    }

    template<typename K, typename V>
    connection_iterator<typename connections_storage<K,V>::vmap::iterator> connections_storage<K, V>::end() {
        std::lock_guard<std::mutex> lk (this->gmx);
        return std::move(connection_iterator<typename vmap::iterator> (storage.end(), count, cv));
    }

    template<typename K, typename V>
    connection_iterator<typename connections_storage<K,V>::vmap::iterator> connections_storage<K, V>::begin() {
        std::lock_guard<std::mutex> lk (this->gmx);
        return std::move(connection_iterator<typename vmap::iterator> (storage.begin(), count, cv));
    }

    template<typename K, typename V>
    bool connections_storage<K, V>::contains(const typename vmap::key_type &key) {
        std::lock_guard<std::mutex> lk (this->gmx);
        return storage.contains(key);
    }

    template<typename K, typename V>
    typename connections_storage<K,V>::vmap::size_type connections_storage<K, V>::size() {
        std::lock_guard<std::mutex> lk (this->gmx);
        return storage.size();
    }

    template<typename K, typename V>
    bool connections_storage<K, V>::empty() {
        return this->size() == 0;
    }

    template<typename K, typename V>
    utils::before_delete connections_storage<K, V>::large_request() {
        lmx.lock();
        std::lock_guard<std::mutex> lk (gmx);
        ++this->count;
        utils::before_delete bd ([this] () -> void {
            --this->count;
            lmx.unlock();
            cv.notify_all();
        });
        return std::move(bd);
    }

    template<typename K, typename V>
    void connections_storage<K, V>::_wait_editable() {
        std::unique_lock<std::mutex> lk (mx);
        this->cv.wait(lk, [this] () -> bool { return *(this->count.get()) == 0; });
    }
};

#endif //MANAPIHTTP_CONNECTIONSSTORAGE_HPP
