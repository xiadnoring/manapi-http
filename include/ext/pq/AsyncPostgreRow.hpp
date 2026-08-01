#pragma once

#include "./AsyncPostgreField.hpp"
#include "../../ManapiUtils.hpp"

#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

namespace manapi::ext::pq {

    class row {
    public:
        class const_iterator;

        row (const PGresult *res, int row) {
            this->res_ = res;
            this->row_ = row;

        }

        ~row () = default;

        MANAPIHTTP_NODISCARD field at (const char *name) const {
            if (auto i = PQfnumber(this->res_, name); i != -1) {
                return field {this->res_, this->row_, i};
            }

            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%s name=%s", "pq", "field doesn't exist", name);
            throw std::runtime_error("field doesn't exist");
        }

        MANAPIHTTP_NODISCARD field at (int index) const {
            if (index >= 0 && index < this->size()) {
                return field{this->res_, this->row_, index};
            }

            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%s id=%d", "pq", "field doesn't exist", index);
            throw std::runtime_error("field doesn't exist");
        }

        MANAPIHTTP_NODISCARD field operator[] (int index) const {
            return this->at(index);
        }

        MANAPIHTTP_NODISCARD field operator[] (const char *name) const {
            return this->at(name);
        }

        MANAPIHTTP_NODISCARD int size () const MANAPIHTTP_NOEXCEPT {
            return PQnfields(this->res_);
        }

        MANAPIHTTP_NODISCARD bool empty () const MANAPIHTTP_NOEXCEPT {
            return this->size() == 0;
        }

        MANAPIHTTP_NODISCARD const_iterator begin() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD const_iterator end() const MANAPIHTTP_NOEXCEPT;
    private:
        const PGresult *res_;
        int row_;
    };

    class row::const_iterator
    {
        const PGresult* pg_result_{};
        int row_{};
        int col_{};

    public:
        using value_type        = field;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using pointer           = field;
        using reference         = field;

        const_iterator() = default;

        const_iterator(const PGresult* pg_result, int row, int col)
          : pg_result_{ pg_result }
        , row_{ row }
        , col_{ col }
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
            this->col_++;
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
            this->col_--;
            return *this;
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator==(const const_iterator& rhs) const
        {
            return this->pg_result_ == rhs.pg_result_ && this->row_ == rhs.row_ && this->col_ == rhs.col_;
        }

        field operator*() const
        {
            return field{ this->pg_result_, this->row_, this->col_ };
        }

        field operator->() const
        {
            return field{ this->pg_result_, this->row_, this->col_ };
        }
    };


    inline row::const_iterator row::begin() const MANAPIHTTP_NOEXCEPT {
        return const_iterator{this->res_, this->row_, 0};
    }

    inline row::const_iterator row::end() const MANAPIHTTP_NOEXCEPT {
        return const_iterator{this->res_, this->row_, this->size()};
    }
}