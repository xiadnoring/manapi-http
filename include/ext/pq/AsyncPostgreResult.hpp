#pragma once

#include "./AsyncPostgreRow.hpp"
#include "./AsyncPostgreError.hpp"
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

        result (result &&n) MANAPIHTTP_NOEXCEPT {
            this->res_ = std::move(n.res_);
        }

        ~result() = default;

        result &operator=(result &&n) MANAPIHTTP_NOEXCEPT {
            if (this != &n) {
                this->res_ = std::move(n.res_);
            }
            return *this;
        }

        operator bool() const MANAPIHTTP_NOEXCEPT {
            return !!this->res_;
        }

        MANAPIHTTP_NODISCARD int size () const MANAPIHTTP_NOEXCEPT {
            return PQntuples(this->res_.get());
        }

        MANAPIHTTP_NODISCARD bool empty () {
            return this->size()==0;
        }

        PGresult *native_handle () MANAPIHTTP_NOEXCEPT {
            return this->res_.get();
        }

        result copy () const {
            return {PQcopyResult(this->res_.get(), PG_COPYRES_ATTRS | PG_COPYRES_TUPLES)};
        }

        MANAPIHTTP_NODISCARD pq::sql_states sqlstate() const MANAPIHTTP_NOEXCEPT {
            if (this->sqlstate_.has_value()) {
                return static_cast<sql_states>(this->sqlstate_.value());
            }

            int n = 0;
            const char *ptr = PQresultErrorField(this->res_.get(), PG_DIAG_SQLSTATE);

            while (ptr && *ptr) {
                n *= 43;
                if (isalpha(*ptr))
                    n += tolower(*(ptr++))-'a'+10;
                else
                    n += (*(ptr++))-'0';
            }

            this->sqlstate_ = n;

            return static_cast<sql_states>(n);
        }

        row at (int index) {
            if (index < this->size()) {
                return row{this->res_.get(), index};
            }

            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%s id=%d", "pq", "out of range in result", index);
            throw std::out_of_range("row doesn't exist");
        }

        row operator[](int index) {
            return this->at(index);
        }

        MANAPIHTTP_NODISCARD size_t affected_rows () const {
            if (this->affected_rows_.has_value()) {
                return this->affected_rows_.value();
            }

            char *s = PQcmdTuples(this->res_.get());
            size_t cnt = 0;
            while (s && *s!='\0') {
                cnt *= 10;
                assert((*s >= '0'));
                cnt += static_cast<uint32_t>(*(s++)-'0');
            }
            this->affected_rows_ = cnt;
            return cnt;
        }

        MANAPIHTTP_NODISCARD const_iterator begin () const MANAPIHTTP_NOEXCEPT;
        MANAPIHTTP_NODISCARD const_iterator end () const MANAPIHTTP_NOEXCEPT;
    private:
        std::unique_ptr<PGresult, pgresult_deleter> res_;
        std::optional<std::size_t> mutable sqlstate_;
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

    inline manapi::ext::pq::result::const_iterator manapi::ext::pq::result::begin() const MANAPIHTTP_NOEXCEPT {
        return const_iterator{this->res_.get(), 0};
    }

    inline manapi::ext::pq::result::const_iterator manapi::ext::pq::result::end() const MANAPIHTTP_NOEXCEPT {
        return const_iterator{this->res_.get(), this->size()};
    }
}
