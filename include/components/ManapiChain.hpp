#pragma once
#include <memory>
#include <utility>

#include "../ManapiUtils.hpp"
namespace manapi {
    template<typename value_type>
    struct chain_item {
        value_type src;

        std::shared_ptr<chain_item> next;
        std::shared_ptr<chain_item> prev;
    };


    template <typename value_type>
    class chain {
    public:
        using _chain_item = std::shared_ptr<chain_item<value_type>>;
        using pointer = value_type *;
        class chain_iterator {
        public:

            explicit chain_iterator (_chain_item _n) {
                this->_src = std::move(_n);
            }

            chain_iterator &operator++(int) {
                this->_src = this->_src ? this->_src->next : nullptr;
                return *this;
            }

            chain_iterator &operator--(int) {
                this->_src = this->_src ? this->_src->prev : nullptr;
                return *this;
            }

            value_type &operator*() {
                return this->_src->src;
            }

            pointer operator->() {
                return &this->_src->src;
            }

            bool operator==(const chain_iterator &_n) const {
                return this->_src == _n._src;
            }

            bool operator!=(const chain_iterator &_n) const {
                return false == this->operator==(_n);
            }

            friend void swap (chain_iterator &lhs, chain_iterator &rhs) noexcept {
                std::swap(lhs._src, rhs._src);
            }

            _chain_item _src;
        };

        using iterator = chain_iterator;

        chain () {
            this->_src = nullptr;
            this->_last = nullptr;
        }

        ~chain () {
            this->clear();
        }

        chain (chain &&n) noexcept {
            this->_src = std::move(n._src);
            this->_last = std::move(n._last);
            this->_s = std::exchange(n._s, 0);
        }

        chain &operator=(chain &&n) noexcept {
            this->_src = std::move(n._src);
            this->_last = std::move(n._last);
            this->_s = std::exchange(n._s, 0);
            return *this;
        }

        void push_back (value_type &&n) {
            auto _n = std::make_shared<chain_item<value_type>>( std::move(n), nullptr, nullptr);
            this->push_back(std::move(_n));
            ++this->_s;
        }

        void push_front (value_type &&n) {
            auto _n = std::make_shared<chain_item<value_type>>( std::move(n), nullptr, nullptr);
            this->push_front(std::move(_n));
            ++this->_s;
        }

        void push_back (const value_type &n) {
            auto _n = std::make_shared<chain_item<value_type>>( n, nullptr, nullptr);
            this->push_back(std::move(_n));
            ++this->_s;
        }

        void push_front (const value_type &n) {
            auto _n = std::make_shared<chain_item<value_type>>( n, nullptr, nullptr);
            this->push_front(std::move(_n));
            ++this->_s;
        }

        void push_back (_chain_item _n) {
            if (this->_last == nullptr) {
                this->_src = _n;
                this->_last = this->_src;
                return;
            }

            this->_last->next = _n;
            _n->prev = this->_last;
            this->_last = _n;
        }

        void push_front (_chain_item _n) {
            if (this->_src == nullptr) {
                this->_last = _n;
                this->_src = this->_last;
                return;
            }

            this->_src->prev = _n;
            _n->next = this->_src;
            this->_src = _n;
        }

        void erase (_chain_item _n) {
            auto ss = this->size();

            if (!ss) {
                return;
            }

            if (ss == 1) {
                if (this->_src == _n) {
                    this->_src = nullptr;
                    this->_last = nullptr;

                    --this->_s;
                }
                return;
            }

            auto &next = _n->next;
            auto &prev = _n->prev;

            if (!next && !prev) {
                // not exists
                return;
            }

            if (prev == nullptr) {
                this->_src = next;
            }
            else {
                prev->next = next;
            }

            if (next == nullptr) {
                this->_last = prev;
            }
            else {
                next->prev = prev;
            }

            prev.reset();
            next.reset();

            --this->_s;
        }

        iterator erase (iterator n) {
            if (!n._src) { return n; }
            _chain_item next = n._src->next;
            this->erase(std::move(n._src));
            return iterator{next};
        }

        void pop_back () {
            if (!this->_last) {
                return;
            }

            this->_last = std::move(this->_last->prev);

            if (this->_last) {
                this->_last->next = nullptr;
            }
            else {
                this->_src = nullptr;
            }

            --this->_s;
        }

        void pop_front () {
            if (!this->_src) {
                return;
            }

            this->_src = std::move(this->_src->next);

            if (this->_src) {
                this->_src->prev = nullptr;
            }
            else {
                this->_last = nullptr;
            }

            --this->_s;
        }

        [[nodiscard]] bool empty () const {
            return this->size() == 0;
        }

        void clear () {
            _chain_item &_c = this->_src;
            while (_c) {
                _chain_item next = std::move(_c->next);
                _c->prev = nullptr;
                _c = std::move(next);
            }
            this->_src = nullptr;
            this->_last = nullptr;
            this->_s = 0;
        }

        iterator begin () {
            return iterator{this->_src};
        }

        iterator end () {
            return iterator{nullptr};
        }

        iterator rbegin () {
            return iterator{this->_last};
        }

        iterator rend () {
            return iterator{nullptr};
        }

        [[nodiscard]] size_t size () const {
            return _s;
        }

        value_type &back () {
            return *this->rbegin();
        }

        value_type &front () {
            return *this->begin();
        }
    private:
        size_t _s = 0;
        _chain_item _src;
        _chain_item _last;
    };
}
