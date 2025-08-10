#pragma once

#pragma once

#include <string_view>

#include "../../../src/include/ManapiUtils.hpp"
#include "../../ManapiDebug.hpp"
#include "./AsyncPostgreValue.hpp"
#include "./AsyncPostgreValueTypes.hpp"

namespace manapi::ext::pq {
#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_ext.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

    class field {
    public:
        field (const PGresult *res, int row, int col) {
            this->res_ = res;
            this->row_ = row;
            this->col_ = col;
        }

        ~field () = default;

        [[nodiscard]] Oid oid () const noexcept {
            return PQftype(this->res_, this->col_);
        }

        [[nodiscard]] Oid type () const noexcept {
            return PQftype(this->res_, this->col_);
        }

        [[nodiscard]] std::string_view name () const noexcept {
            return std::string_view{PQfname(this->res_, this->col_)};
        }

        [[nodiscard]] bool is_null () const noexcept {
            return PQgetisnull(this->res_, this->row_, this->col_);
        }

        [[nodiscard]] size_t size () const noexcept {
            return PQgetlength(this->res_, this->row_, this->col_);
        }

        [[nodiscard]] char *c_str () const noexcept {
            return PQgetvalue(this->res_, this->row_, this->col_);
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

        [[nodiscard]] std::string_view view () const {
            return std::string_view{this->c_str(), this->size()};
        }

    private:
        const PGresult *res_;
        int row_;
        int col_;
    };
}
