#pragma once

#include <memory>
#include <utility>

#include "../ManapiUtils.hpp"

namespace manapi {
    template<typename value_type>
    struct chain_item {
        value_type src;

        std::unique_ptr<chain_item> next;
        chain_item *prev;
    };


    template <typename value_type>
    class chain {
    public:
        using chain_item_un = std::unique_ptr<chain_item<value_type>>;
        using chain_item_ptr = chain_item<value_type> *;
        using pointer = value_type *;
        class chain_iterator {
        public:

            explicit chain_iterator (chain_item_ptr n) {
                this->src_ = n;
            }

            chain_iterator &operator++(int) {
                this->src_ = this->src_ ? this->src_->next.get() : nullptr;
                return *this;
            }

            chain_iterator &operator--(int) {
                this->src_ = this->src_ ? this->src_->prev : nullptr;
                return *this;
            }

            value_type &operator*() {
                return this->src_->src;
            }

            pointer operator->() {
                return &this->src_->src;
            }

            bool operator==(const chain_iterator &_n) const {
                return this->src_ == _n.src_;
            }

            bool operator!=(const chain_iterator &_n) const {
                return false == this->operator==(_n);
            }

            friend void swap (chain_iterator &lhs, chain_iterator &rhs) noexcept {
                std::swap(lhs.src_, rhs.src_);
            }

            chain_item_ptr src_;
        };

        using iterator = chain_iterator;

        chain () {
            this->src_ = nullptr;
            this->last_ = nullptr;
            this->s_ = 0;
        }

        ~chain () {
            this->clear();
        }

        chain (chain &&n) noexcept {
            this->src_ = std::move(n.src_);
            this->last_ = std::move(n.last_);
            this->s_ = std::exchange(n.s_, 0);
        }

        chain &operator=(chain &&n) noexcept {
            this->src_ = std::move(n.src_);
            this->last_ = std::move(n.last_);
            this->s_ = std::exchange(n.s_, 0);
            return *this;
        }

        void push_back (value_type &&n) {
            auto _n = std::make_unique<chain_item<value_type>>( std::move(n), nullptr, nullptr);
            this->push_back(std::move(_n));
            ++this->s_;
        }

        void push_front (value_type &&n) {
            auto _n = std::make_unique<chain_item<value_type>>( std::move(n), nullptr, nullptr);
            this->push_front(std::move(_n));
            ++this->s_;
        }

        void push_back (const value_type &n) {
            auto _n = std::make_unique<chain_item<value_type>>( n, nullptr, nullptr);
            this->push_back(std::move(_n));
            ++this->s_;
        }

        void push_front (const value_type &n) {
            auto _n = std::make_unique<chain_item<value_type>>( n, nullptr, nullptr);
            this->push_front(std::move(_n));
            ++this->s_;
        }

        void push_back (chain_item_un n) {
            if (this->last_ == nullptr) {
                this->src_ = std::move(n);
                this->last_ = this->src_.get();
                return;
            }

            this->last_->next = std::move(n);
            this->last_->next->prev = this->last_;
            this->last_ = this->last_->next.get();
        }

        void push_front (chain_item_un n) {
            if (this->src_ == nullptr) {
                this->last_ = n.get();
                this->src_ = std::move(n);
                return;
            }

            this->src_->prev = n.get();
            n->next = std::move(this->src_);
            this->src_ = std::move(n);
        }

        void erase (chain_item<value_type> *n) {
            auto ss = this->size();

            if (!ss) {
                return;
            }

            if (ss == 1) {
                if (this->src_.get() == n) {
                    this->src_ = nullptr;
                    this->last_ = nullptr;

                    --this->s_;
                }
                return;
            }

            auto next = std::move(n->next);
            auto prev = n->prev;

            if (!next && !prev) {
                // not exists
                return;
            }

            if (!prev) {
                this->src_ = std::move(next);
            }
            else {
                prev->next = std::move(next);
            }

            if (!next) {
                this->last_ = prev;
            }
            else {
                next->prev = prev;
            }

            --this->s_;
        }

        iterator erase (iterator n) {
            if (!n.src_) { return n; }
            auto next = n.src_->next.get();
            this->erase(n.src_);
            return iterator{next};
        }

        void pop_back () {
            if (!this->last_) {
                return;
            }

            this->last_ = this->last_->prev;

            if (this->last_) {
                this->last_->next = nullptr;
            }
            else {
                this->src_ = nullptr;
            }

            --this->s_;
        }

        void pop_front () {
            if (!this->src_) {
                return;
            }

            this->src_ = std::move(this->src_->next);

            if (this->src_) {
                this->src_->prev = nullptr;
            }
            else {
                this->last_ = nullptr;
            }

            --this->s_;
        }

        [[nodiscard]] bool empty () const {
            return this->size() == 0;
        }

        void clear () {
            chain_item_un c = std::move(this->src_);
            while (c) {
                chain_item_un next = std::move(c->next);
                c->prev = nullptr;
                c = std::move(next);
            }
            this->src_ = nullptr;
            this->last_ = nullptr;
            this->s_ = 0;
        }

        iterator begin () {
            return iterator{this->src_.get()};
        }

        iterator end () {
            return iterator{nullptr};
        }

        iterator rbegin () {
            return iterator{this->last_};
        }

        iterator rend () {
            return iterator{nullptr};
        }

        [[nodiscard]] size_t size () const {
            return s_;
        }

        value_type &back () {
            return *this->rbegin();
        }

        value_type &front () {
            return *this->begin();
        }
    private:
        size_t s_;
        chain_item_un src_;
        chain_item<value_type> *last_;
    };
}
