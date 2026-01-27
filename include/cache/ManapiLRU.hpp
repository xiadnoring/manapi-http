#pragma once

#include <cassert>
#include <map>
#include <cstddef>

#include "./../ManapiErrors.hpp"
#include "./../ManapiUtils.hpp"
#include "./../std/ManapiChain.hpp"

namespace manapi {
    template<typename K, typename V>
    class lru_cache {
        typedef typename std::pair<K, V> key_value_pair_t;

        typedef typename manapi::chain<key_value_pair_t>::iterator list_iterator_t;

        struct item_t {
            list_iterator_t item;
            std::size_t weight;
        };
    public:
        lru_cache () : lru_cache(0) {}

        lru_cache (std::size_t max_size) {
            this->m_max = max_size;
            this->m_used = 0;
        }

        MANAPIHTTP_NODISCARD std::size_t size () const MANAPIHTTP_NOEXCEPT {
            return this->m_items.size();
        }

        MANAPIHTTP_NODISCARD bool contains (const K &key) const MANAPIHTTP_NOEXCEPT {
            return this->m_items.contains(key);
        }

        void put (const K &key, const V &value, std::size_t weight) {
            iput(key, V(value), weight);
        }

        void put (const K &key, V &&value, std::size_t weight) {
            iput(key, std::forward<V>(value), weight);
        }

        void iput (const K &key, V &&value, std::size_t weight) {
            auto it = this->m_items.find(key);
            if (it == this->m_items.end()) {
                this->m_list.push_front(key_value_pair_t (key, std::forward<V>(value)));
                try {
                    item_t item (this->m_list.begin(), weight);
                    this->m_items.insert({key, std::move(item)});
                }
                catch (...) {
                    this->m_list.pop_front();
                    std::rethrow_exception(std::current_exception());
                }
            }
            else {
                it->second.item->second = std::forward<V>(value); // malloc can be only here

                std::unique_ptr<chain_item<key_value_pair_t>> un;
                this->m_used -= it->second.weight;
                this->m_list.erase(it->second.item, un);
                assert(!!un);
                this->m_list.push_front_chain(std::move(un));
                it->second.item = this->m_list.begin();
                it->second.weight = weight;
            }

            this->m_used += weight;

            this->cleanup();
        }

        MANAPIHTTP_NODISCARD manapi::status_or<V*> get (const K &key) MANAPIHTTP_NOEXCEPT {
            auto it = this->m_items.find(key);
            if (it == this->m_items.end()) {
                return manapi::status_not_found();
            }

            std::unique_ptr<chain_item<key_value_pair_t>> un;
            this->m_list.erase(it->second.item, un);
            assert(!!un);
            this->m_list.push_front_chain(std::move(un));
            it->second.item = this->m_list.begin();

            return &it->second.item->second;
        }

        void remove (const K &key) MANAPIHTTP_NOEXCEPT {
            auto it = this->m_items.find(key);
            if (it != this->m_items.end()) {
                this->m_list.erase(it->second.item);
                this->m_used -= it->second.weight;
                this->m_items.erase(it);
            }
        }

        void clear () MANAPIHTTP_NOEXCEPT {
            this->m_used = 0;
            this->m_items.clear();
            this->m_list.clear();
        }

        MANAPIHTTP_NODISCARD std::size_t used () const MANAPIHTTP_NOEXCEPT {
            return this->m_used;
        }

        void max (std::size_t m) {
            this->m_max = m;
            this->cleanup();
        }

        MANAPIHTTP_NODISCARD std::size_t max () MANAPIHTTP_NOEXCEPT {
            return this->m_max;
        }

        void cleanup () MANAPIHTTP_NOEXCEPT {
            while (this->m_used > this->m_max) {
                auto it = this->m_list.rbegin();
                auto data = this->m_items.extract(it->first);
                assert(!data.empty());
                this->m_used -= data.mapped().weight;
                this->m_list.erase(it);
            }
        }
    private:
        std::size_t m_max;

        std::size_t m_used;

        manapi::chain<key_value_pair_t> m_list;

        std::map<K, item_t> m_items;
    };
}
