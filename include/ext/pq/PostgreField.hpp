#pragma once

#pragma once

#include <string_view>

#include "../../ManapiUtils.hpp"
#include "../../ManapiDebug.hpp"
#include "./PostgreValue.hpp"
#include "./PostgreValueTypes.hpp"

#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

namespace manapi::ext::pq {

    class field {
    public:
        field (const PGresult *res, int row, int col) {
            this->m_res = res;
            this->m_row = row;
            this->m_col = col;
        }

        ~field () = default;

        MANAPIHTTP_NODISCARD Oid oid () const MANAPIHTTP_NOEXCEPT {
            return PQftype(this->m_res, this->m_col);
        }

        MANAPIHTTP_NODISCARD Oid type () const MANAPIHTTP_NOEXCEPT {
            return PQftype(this->m_res, this->m_col);
        }

        MANAPIHTTP_NODISCARD std::string_view name () const MANAPIHTTP_NOEXCEPT {
            return std::string_view{PQfname(this->m_res, this->m_col)};
        }

        MANAPIHTTP_NODISCARD bool is_null () const MANAPIHTTP_NOEXCEPT {
            return PQgetisnull(this->m_res, this->m_row, this->m_col);
        }

        MANAPIHTTP_NODISCARD size_t size () const MANAPIHTTP_NOEXCEPT {
            return static_cast<std::size_t>(PQgetlength(this->m_res, this->m_row, this->m_col));
        }

        MANAPIHTTP_NODISCARD char *c_str () const MANAPIHTTP_NOEXCEPT {
            return PQgetvalue(this->m_res, this->m_row, this->m_col);
        }

        template<typename T>
        T as () const {
            if (this->is_null()) {
                auto name = this->name();
                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%.*s is null", "pq", name.size(), name.data());
                throw std::runtime_error("field is null");
            }

            return pq::from_string<T>(std::string_view{this->c_str(), this->size()});
        }

        MANAPIHTTP_NODISCARD std::string_view view () const {
            return std::string_view{this->c_str(), this->size()};
        }

    private:
        const PGresult *m_res;
        int m_row;
        int m_col;
    };
}
