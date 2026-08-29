#pragma once


#include "../../ManapiUtils.hpp"
#include "../../std/ManapiContext.hpp"
#include "../../std/ManapiSocket.hpp"
#include "../../std/ManapiEasyCancellation.hpp"
#include "../../std/ManapiMutex.hpp"
#include "../../json/ManapiJson.hpp"

#include "./PostgreResult.hpp"
#include "./PostgreError.hpp"
#include "./PostgreNotification.hpp"

#include "libpq-events.h"
#include "libpq-fe.h"
#include "libpq/libpq-fs.h"
#include "pg_config.h"
#include "pg_config_manual.h"
#include "pg_config_os.h"

namespace manapi::ext::pq {

    enum error_code {
        RESULT_STATUS_OK,
        RESULT_STATUS_BAD_RESPONSE,
        RESULT_STATUS_EMPTY_QUERY,
        RESULT_STATUS_FATAL_ERROR,
        RESULT_STATUS_UNEXPECTED,
        RESULT_STATUS_PIPELINE_ABORTED
    };

    class connection : public std::enable_shared_from_this<connection> {
        struct data_t;

        connection ();
    public:

        ~connection ();

        static manapi::status_or<std::shared_ptr<pq::connection>> create () MANAPIHTTP_NOEXCEPT;

        manapi::future<manapi::ev::status> connect (std::string_view uri, manapi::ctoken token = nullptr);

        manapi::future<manapi::ev::status> connect (std::string host, std::string port, std::string username, std::string password, std::string database, manapi::ctoken token = nullptr);

        manapi::future<manapi::ev::status> connect (manapi::json params, manapi::ctoken token = nullptr);

        manapi::future<manapi::ev::status> connect (const char * const *keywords, const char * const *values, manapi::ctoken token = nullptr);

        manapi::future<pq::status_or<pq::result>> pexec (const char *command, std::size_t nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths, const int *paramFormats, int resultFormat, manapi::ctoken token = nullptr);

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> exec (const std::string &sql, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1);
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> exec (const char *sql, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (sql, t.size(), t.data(), v.data(), l.data(), f.data(), 1);
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> execl (const std::string &sql, ctoken token, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1, std::move(token));
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> execl (const char *sql, ctoken token, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res.ok()) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (sql, t.size(), t.data(), v.data(), l.data(), f.data(), 1, std::move(token));
        }

        manapi::future<bool> ping (std::size_t timeoutms = 5000);

        /**
         * Escape string.
         * DataLoss error in that function means that you need to resize the source buffer and repeat again
         *
         * @param dst to copy
         * @param dst_size destionation size
         * @param text source string
         * @return Ok if in case of sucess; otherwise, it returns DataLoss, InternalError, InvalidArgument
         */
        manapi::status esc (char *dst, std::size_t *dst_size, std::string_view text) MANAPIHTTP_NOEXCEPT;

        /**
         * Escape string
         * @param text source string
         * @return Ok in case of success; otherwise, it returns InternalError, InvalidArgument
         */
        manapi::status_or<std::string> esc (std::string_view text) MANAPIHTTP_NOEXCEPT;

        void close () MANAPIHTTP_NOEXCEPT;

        void notify_cb (std::move_only_function<manapi::future<>(notification notify)> cb) MANAPIHTTP_NOEXCEPT;
    private:
        manapi::future<manapi::ev::status> connect_psql_ (manapi::ctoken token) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD manapi::status check_conn_ () const MANAPIHTTP_NOEXCEPT;

        manapi::status_or<size_t> esc_to_buff (std::string_view text, char *buff) MANAPIHTTP_NOEXCEPT;

        future<manapi::status> flush (manapi::ctoken token);

        future<pq::status_or<pq::result>> generic_single_result_query (manapi::ctoken token);

        future<manapi::status_or<pq::result>> receive_result (manapi::ctoken token);

        static error_code result_status_to_error_code (result &result) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD ::PGconn * native_handle () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool connected () const MANAPIHTTP_NOEXCEPT;

        manapi::future<> receive_notifications ();

        std::unique_ptr <data_t> m_data;
    };
};