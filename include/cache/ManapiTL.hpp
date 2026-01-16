#pragma once

#include <chrono>
#include <map>
#include <set>

#include "./../ManapiErrors.hpp"

namespace manapi {
    template<typename K, typename V>
    class tl_cache {
        typedef typename std::pair<K, V> key_value_pair_t;
    public:
        tl_cache () = default;

        MANAPIHTTP_NODISCARD std::size_t size () const MANAPIHTTP_NOEXCEPT {
            return this->m_data.size();
        }

        MANAPIHTTP_NODISCARD bool contains (const K &key) const MANAPIHTTP_NOEXCEPT {
            return this->m_data.contains(key);
        }

        void put (const K &key, const V &value, std::chrono::milliseconds duration) {
            this->cleanup();
            auto id = std::make_pair(std::chrono::steady_clock::now() + duration, value);
            auto it = this->m_data.insert({key, id});
            if (!it.second) {
                this->m_sorted.erase(std::make_pair(it.first->second.first, it.first->first));
                it.first->second = id;
            }
            try {
                this->m_sorted.insert(std::make_pair(id.first, it.first->first));
            }
            catch (...) {
                this->m_data.erase(it.first);
                std::rethrow_exception(std::current_exception());
            }
        }

        MANAPIHTTP_NODISCARD manapi::status_or<V*> get (const K &key) MANAPIHTTP_NOEXCEPT {
            this->cleanup();

            auto it = this->m_data.find(key);
            if (it == this->m_data.end())
                return manapi::status_not_found();
            return &it->second.second;
        }

        void remove (const K &key) MANAPIHTTP_NOEXCEPT {
            auto it = this->m_data.find(key);
            if (it != this->m_data.end()) {
                this->m_sorted.erase(std::make_pair(it->second.first, it->first));
                this->m_data.erase(it);
            }

            this->cleanup();
        }

        void clear () MANAPIHTTP_NOEXCEPT {
            this->m_data.clear();
            this->m_sorted.clear();
        }

        void cleanup () MANAPIHTTP_NOEXCEPT {
            auto now = std::chrono::steady_clock::now();
            while (!this->m_sorted.empty()) {
                auto it = this->m_sorted.begin();
                if (it->first >= now) break;
                this->remove(it->second);
            }
        }
    private:
        std::map<K, std::pair<std::chrono::steady_clock::time_point, V>> m_data;
        std::set<std::pair<std::chrono::steady_clock::time_point, K>> m_sorted;
    };
}