#pragma once

#include "./AsyncPostgreField.hpp"

namespace manapi::ext::pq {
#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_ext.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

    class row {
    public:
        class const_iterator;

        row (const PGresult *res, int row) {
            this->res_ = res;
            this->row_ = row;

        }

        ~row () = default;

        [[nodiscard]] field at (const char *name) const {
            if (auto i = PQfnumber(this->res_, name); i != -1) {
                return field {this->res_, this->row_, i};
            }

            THROW_MANAPIHTTP_EXCEPTION(ERR_POSTGRE_RESULT, "Field not exists: {}", name);
        }

        [[nodiscard]] field at (int index) const {
            if (index >= 0 && index < this->size()) {
                return field{this->res_, this->row_, index};
            }

            THROW_MANAPIHTTP_EXCEPTION(ERR_POSTGRE_RESULT, "Field with the index {} not found", index);
        }

        [[nodiscard]] field operator[] (int index) const {
            return this->at(index);
        }

        [[nodiscard]] field operator[] (const char *name) const {
            return this->at(name);
        }

        [[nodiscard]] int size () const noexcept {
            return PQnfields(this->res_);
        }

        [[nodiscard]] bool empty () const noexcept {
            return this->size() == 0;
        }

        [[nodiscard]] const_iterator begin() const noexcept;

        [[nodiscard]] const_iterator end() const noexcept;
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


    inline row::const_iterator row::begin() const noexcept {
        return const_iterator{this->res_, this->row_, 0};
    }

    inline row::const_iterator row::end() const noexcept {
        return const_iterator{this->res_, this->row_, this->size()};
    }
}