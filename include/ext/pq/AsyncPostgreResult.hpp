#pragma once

#include "./AsyncPostgreRow.hpp"
#include "../../ManapiErrors.hpp"
#include "../../ManapiUtils.hpp"

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

        result () {}

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

        [[nodiscard]] int sqlstate() const noexcept {
            if (this->sqlstate_.has_value()) {
                return this->sqlstate_.value();
            }

            int n = 0;
            const char *ptr = PQresultErrorField(this->res_.get(), PG_DIAG_SQLSTATE);

            while (ptr && *ptr) {
                n *= 10;
                n += (*(ptr++))-'0';
            }

            this->sqlstate_ = n;

            return n;
        }

        row at (int index) {
            if (index < this->size()) {
                return row{this->res_.get(), index};
            }

            THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_RESULT, "Out of range");
        }

        row operator[](int index) {
            return this->at(index);
        }

        [[nodiscard]] size_t affected_rows () const {
            if (this->affected_rows_.has_value()) {
                return this->affected_rows_.value();
            }

            char *s = PQcmdTuples(this->res_.get());
            size_t cnt = 0;
            while (s && *s!='\0') {
                cnt *= 10;
                cnt += *(s++)-'0';
            }
            this->affected_rows_ = cnt;
            return cnt;
        }

        [[nodiscard]] const_iterator begin () const noexcept;
        [[nodiscard]] const_iterator end () const noexcept;
    private:
        std::unique_ptr<PGresult, pgresult_deleter> res_;
        std::optional<int> mutable sqlstate_;
        std::optional<size_t> mutable affected_rows_;
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

    inline manapi::ext::pq::result::const_iterator manapi::ext::pq::result::begin() const noexcept {
        return const_iterator{this->res_.get(), 0};
    }

    inline manapi::ext::pq::result::const_iterator manapi::ext::pq::result::end() const noexcept {
        return const_iterator{this->res_.get(), this->size()};
    }
}
