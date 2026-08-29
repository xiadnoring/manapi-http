#pragma once

#include <memory>
#include <string_view>
#include "../../ManapiUtils.hpp"

namespace manapi::ext::pq {
    #include <libpq-fe.h>
    class notification {
        struct pgnotify_deter {
            void operator () (PGnotify *p) {
                PQfreemem(p);
            }
        };

        std::unique_ptr<PGnotify, pgnotify_deter> m_pg_notify;

    public:
        notification () = default;

        notification (PGnotify *p) {
            this->m_pg_notify.reset(p);
        }

        notification (notification &&n) MANAPIHTTP_NOEXCEPT {
            this->m_pg_notify = std::move(n.m_pg_notify);
        }

        operator bool () const {
            return !!this->m_pg_notify;
        }

        MANAPIHTTP_NODISCARD int pid () const MANAPIHTTP_NOEXCEPT {
            if (this->m_pg_notify) {
                return this->m_pg_notify->be_pid;
            }

            return -1;
        }

        MANAPIHTTP_NODISCARD std::string_view channel () const MANAPIHTTP_NOEXCEPT {
            if (this->m_pg_notify) {
                return this->m_pg_notify->relname;
            }

            return {};
        }

        MANAPIHTTP_NODISCARD std::string_view payload () const MANAPIHTTP_NOEXCEPT {
            if (this->m_pg_notify) {
                return this->m_pg_notify->extra;
            }

            return {};
        }
    };
}