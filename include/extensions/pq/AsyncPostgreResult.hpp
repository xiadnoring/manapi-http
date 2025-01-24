#pragma once

#include "./AsyncPostgreRow.hpp"
#include "ManapiErrors.hpp"

namespace manapi::ext::pq {
#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_ext.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

    class result {
    public:
        class const_iterator;

        struct pgresult_deleter {
            void operator()(PGresult *res) {
                PQclear(res);
            }
        };

        result (PGresult *res) {
            this->res_.reset(res);
        }

        result (result &&n) noexcept {
            this->res_ = std::move(n.res_);
        }

        ~result() = default;

        result &operator=(result &&n) noexcept {
            this->res_ = std::move(n.res_);
            return *this;
        }

        operator bool() const noexcept {
            return !!this->res_;
        }

        [[nodiscard]] int size () const noexcept {
            return PQntuples(this->res_.get());
        }

        [[nodiscard]] bool empty () {
            return this->size()==0;
        }

        PGresult *native_handle () noexcept {
            return this->res_.get();
        }

        row at (const int &index) {
            if (index < this->size()) {
                return row{this->res_.get(), index};
            }

            THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_RESULT, "Out of range");
        }

        row operator[](const int &index) {
            return this->at(index);
        }

        [[nodiscard]] const_iterator begin () const noexcept;
        [[nodiscard]] const_iterator end () const noexcept;
    private:
        std::unique_ptr<PGresult, pgresult_deleter> res_;

    };

    class result::const_iterator
    {
        const PGresult* pgresult_{};
        int row_{};

    public:
        using value_type        = row;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using pointer           = row;
        using reference         = row;

        const_iterator() = default;

        const_iterator(const PGresult* pg_result, int row)
          : pgresult_{ pg_result }
        , row_{ row }
        {
        }

        const_iterator operator++(int)
        {
            const auto tmp = *this;
            ++*this;
            return tmp;
        }

        const_iterator& operator++()
        {
            this->row_++;
            return *this;
        }

        const_iterator operator--(int)
        {
            const auto tmp = *this;
            --*this;
            return tmp;
        }

        const_iterator& operator--()
        {
            this->row_--;
            return *this;
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator==(const const_iterator& rhs) const
        {
            return this->pgresult_ == rhs.pgresult_ && this->row_ == rhs.row_;
        }

        row operator*() const
        {
            return row{ this->pgresult_, this->row_ };
        }

        row operator->() const
        {
            return row{ this->pgresult_, this->row_ };
        }
    };

    manapi::ext::pq::result::const_iterator manapi::ext::pq::result::begin() const noexcept {
        return const_iterator{this->res_.get(), 0};
    }

    manapi::ext::pq::result::const_iterator manapi::ext::pq::result::end() const noexcept {
        return const_iterator{this->res_.get(), this->size()};
    }
}
