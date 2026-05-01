#pragma once

#include <memory>
#include "./../../ManapiEventStructures.hpp"
#include "./AsyncPostgreClient.hpp"

namespace manapi::ext::pq {
    class pool;

    class db;

    enum ktypes {
        kMaster,
        kSlave
    };

    class item {
        friend db;
    public:
        item (std::shared_ptr<pool> pool, manapi::ext::pq::connection *conn);

        ~item ();

        manapi::ext::pq::connection *operator->() MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD const manapi::ext::pq::connection *operator->() const MANAPIHTTP_NOEXCEPT;

        manapi::ext::pq::connection &operator*() MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD const manapi::ext::pq::connection &operator*() const MANAPIHTTP_NOEXCEPT;

        void release () MANAPIHTTP_NOEXCEPT;
    private:
        manapi::ext::pq::connection *m_conn;

        std::shared_ptr<pool> m_pool;
    };

    class pool : public std::enable_shared_from_this<pool> {
        friend void item::release() MANAPIHTTP_NOEXCEPT;

        friend class db;

        pool ();
    public:

        ~pool ();

        static manapi::status_or<std::shared_ptr<pool>> create () MANAPIHTTP_NOEXCEPT;

        future<ev::status> connect (std::size_t size, std::string host, std::string port, std::string user, std::string password, std::string db, manapi::ctoken token = nullptr);

        future<ev::status> connect (std::size_t size, manapi::json params, manapi::ctoken token = nullptr);

        future<manapi::status> stop ();

        future<manapi::status_or<item>> peer ();

        MANAPIHTTP_NODISCARD std::size_t size () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool connected () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t waiting () const MANAPIHTTP_NOEXCEPT;

        manapi::status connected (bool active) MANAPIHTTP_NOEXCEPT;

        manapi::future<> ping (std::size_t timeoutms = 5000);
    private:
        uint8_t m_flags;

        std::vector<std::shared_ptr<pq::connection>> m_clients;

        std::stack<pq::connection *> m_available;

        manapi::async::mutex m_mx;

        std::size_t m_waiting;

        manapi::timer m_ping;

        std::shared_ptr<db> m_db;
    };

    class db : public std::enable_shared_from_this<db> {
        friend class pool;

        friend class item;

        db ();
    public:

        ~db ();

        static manapi::status_or<std::shared_ptr<db>> create () MANAPIHTTP_NOEXCEPT;

        manapi::status set_master (std::shared_ptr<pq::pool> master);

        void remove_master ();

        manapi::status add_slave (std::shared_ptr<pq::pool> slave) MANAPIHTTP_NOEXCEPT;

        void remove_slave (std::shared_ptr<pq::pool> slave);

        void remove_slaves ();

        future<manapi::status_or<item>> slave ();

        future<manapi::status_or<item>> master ();

        bool has_master () const;

        bool has_slaves () const;

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> exec (ktypes type, const std::string &sql, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (type, sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), nullptr);
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> exec (ktypes type, const char *sql, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (type, sql, t.size(), t.data(), v.data(), l.data(), f.data(), nullptr);
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> execl (ktypes type, const std::string &sql, ctoken token, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (type, sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), std::move(token));
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> execl (ktypes type, const char *sql, ctoken token, Args &&...args) {
            std::string buffer;
            auto res = pq::serialize(buffer, std::make_tuple(args...));
            if (!res) co_return res.err();
            auto [t, v, l, f] = res.unwrap();
            co_return co_await this->pexec (type, sql, t.size(), t.data(), v.data(), l.data(), f.data(), std::move(token));
        }
    protected:
        manapi::future<pq::status_or<pq::result>> pexec (ktypes type, const char *command, int nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths, const int *paramFormats, ctoken token);

    private:
        std::shared_ptr<pq::pool> m_master;

        std::set<std::pair<std::size_t, std::shared_ptr<pq::pool>>> m_slaves;
    };
}
