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

        std::unique_ptr<PGnotify, pgnotify_deter> pg_notify_;

    public:
        notification () = default;

        notification (PGnotify *p) {
            this->pg_notify_.reset(p);
        }

        notification (notification &&n) MANAPIHTTP_NOEXCEPT {
            this->pg_notify_ = std::move(n.pg_notify_);
        }

        operator bool () const {
            return !!this->pg_notify_;
        }

        MANAPIHTTP_NODISCARD int pid () const MANAPIHTTP_NOEXCEPT {
            if (this->pg_notify_) {
                return this->pg_notify_->be_pid;
            }

            return -1;
        }

        MANAPIHTTP_NODISCARD std::string_view channel () const MANAPIHTTP_NOEXCEPT {
            if (this->pg_notify_) {
                return this->pg_notify_->relname;
            }

            return {};
        }

        MANAPIHTTP_NODISCARD std::string_view payload () const MANAPIHTTP_NOEXCEPT {
            if (this->pg_notify_) {
                return this->pg_notify_->extra;
            }

            return {};
        }
    };
}