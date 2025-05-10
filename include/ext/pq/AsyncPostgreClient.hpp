#pragma once


#include "../../ManapiUtils.hpp"
#include "../../async/ManapiAsyncContext.hpp"
#include "../../async/ManapiAsyncSocket.hpp"

#include "./AsyncPostgreResult.hpp"
#include "./AsyncPostgreNotification.hpp"

namespace manapi::ext::pq {
    #include "libpq-events.h"
    #include "libpq-fe.h"
    #include "libpq/libpq-fs.h"
    #include "pg_config.h"
    #include "pg_config_ext.h"
    #include "pg_config_manual.h"
    #include "pg_config_os.h"

    enum error_code {
        RESULT_STATUS_OK,
        RESULT_STATUS_BAD_RESPONSE,
        RESULT_STATUS_EMPTY_QUERY,
        RESULT_STATUS_FATAL_ERROR,
        RESULT_STATUS_UNEXPECTED,
        RESULT_STATUS_PIPELINE_ABORTED
    };

    class sqlexception : public std::exception {
    public:
        explicit sqlexception (const int &errcode, std::string errmsg) {
            this->errcode = errcode;
            this->errmsg = std::move(errmsg);
        }
        [[nodiscard]] const char * what() const noexcept override {
            return this->errmsg.data();
        }
        [[nodiscard]] int sqlstate () const {
            return this->errcode;
        }
    private:
        int errcode;
        std::string errmsg;
    };

    class connection {
    public:
        struct pgconn_deleter {
            void operator()(PGconn *p) {
                PQfinish(p);
            }
        };

        connection (async::shared_ctx ctx) {
            this->ctx = std::move(ctx);
            this->conn = nullptr;
        }

        ~connection () = default;

        manapi::future<> connect (std::string_view uri) {
            if (std::exchange(this->init_, false)) {
                this->conn.reset(PQconnectStart(uri.data()));

                if (PQstatus(this->conn.get()) == CONNECTION_BAD) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "Connection bad");
                }

                if (PQsetnonblocking(this->conn.get(), 1)) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "set non-blocing failure");
                }

                PQsetNoticeProcessor(
                    this->conn.get(), +[](void *, const char *) -> void{}, nullptr);

                this->fd_ = PQsocket(this->conn.get());

                while (true) {
                    auto ret = PQconnectPoll(this->conn.get());

                    this->cancellation.reset(this->ctx);
                    if (this->timeoutms_) {
                        this->cancellation.timeout(this->timeoutms_);
                    }

                    switch (ret) {
                        case PGRES_POLLING_READING:
                            this->fd_ = PQsocket(this->conn.get());
                            if (-1 == co_await async::read_ready(this->ctx, this->fd_, this->cancellation)) {
                                THROW_MANAPIHTTP_EXCEPTION2(ERR_CONNECTION_TIMEOUT, "postgre: timeout was reached (read)");
                            }
                        continue;
                        case PGRES_POLLING_WRITING:
                            this->fd_ = PQsocket(this->conn.get());
                            if (-1 == co_await async::write_ready(this->ctx, this->fd_, this->cancellation)) {
                                THROW_MANAPIHTTP_EXCEPTION2(ERR_CONNECTION_TIMEOUT, "postgre: timeout was reached (write)");
                            }
                        continue;
                        case PGRES_POLLING_FAILED:
                            THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "Polling failed");
                        break;
                        default:
                            break;
                    }

                    break;
                }
            }
        }

        manapi::future<> connect (std::string_view host, std::string_view port, std::string_view username, std::string_view password, std::string_view database) {
            auto uri = std::format("postgresql://{}:{}@{}:{}/{}", username, password, host, port, database);
            co_return co_await this->connect(uri);
        }

        manapi::future<pq::result> exec (std::string_view sql) {
            this->_check_conn();
            if (!PQsendQueryParams(this->conn.get(), sql.data(), 0, nullptr, nullptr, nullptr, nullptr, 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send the query failed");
            }

            return this->generic_single_result_query();
        }

        template<typename ...Args>
        manapi::future<pq::result> exec (std::string_view sql, Args &&...args) {
            this->_check_conn();
            std::string buffer;
            auto [t, v, l, f] = pq::serialize(buffer, std::make_tuple(args...));
            if (!PQsendQueryParams(this->conn.get(), sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send query failed");
            }

            co_return std::move(co_await this->generic_single_result_query());
        }

        std::string esc (std::string_view text) {
            this->_check_conn();
            std::string buff;
            buff.resize(text.size() * 2 + 1);
            const auto copied = this->esc_to_buff(text, buff.data());
            buff.resize(copied);
            return std::move(buff);
        }

        void close () {
            this->conn.reset();
            this->init_ = false;
        }

        void set_notify_cb (std::function<manapi::future<>(notification notify)> cb) {
            this->notify_cb = std::move(cb);
        }

        void timeout (ssize_t ms) {
            this->timeoutms_ = ms;
        }

        [[nodiscard]] ssize_t timeout () const {
            return this->timeoutms_;
        }
    private:
        void _check_conn () const {
            if (this->conn) {
                return;
            }

            THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "connection doesn't exists");
        }
        size_t esc_to_buff (std::string_view text, char *buff) {
            int err{0};
            auto const copied{
                PQescapeStringConn(this->conn.get(), buff, text.data(), text.size(), &err)};
            if (err) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "PQescapeStringConn failed");
            }
            return copied;
        }

        future<> flush () {
            auto rhs = PQflush(this->conn.get());
            if (rhs == -1) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "flush failed");
            }
            if (rhs == 0) {
                /* done */
                co_return;
            }

            this->cancellation.reset(this->ctx);
            if (this->timeoutms_) {
                this->cancellation.timeout(this->timeoutms_);
            }

            int revents = ev::READ|ev::WRITE;

            while (true) {
                if (revents & ev::READ) {
                    if (!PQconsumeInput(this->conn.get())) {
                        THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input error");
                    }
                }

                if (revents & ev::WRITE) {
                    auto rhs = PQflush(this->conn.get());
                    if (rhs == -1) {
                        THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "flush failed");
                    }

                    if (rhs == 0) {
                        /* done */
                        break;
                    }
                }

                if (-1 == (revents = co_await async::custom_ready(this->ctx, ev::READ|ev::WRITE, PQsocket(this->conn.get())))) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_CONNECTION_TIMEOUT, "postgre: Timeout was reached");
                }
            }
        }
        future<pq::result> generic_single_result_query () {
            co_await this->flush();
            auto result = co_await this->receive_result();
            if (co_await this->receive_result()) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "unexpected non-null result");
            }
            auto err = connection::result_status_to_error_code(result);

            auto rows = result.affected_rows();
            auto sqlstate = result.sqlstate();

            if (err != error_code::RESULT_STATUS_OK) {
                throw pq::sqlexception (sqlstate, PQerrorMessage(this->conn.get()));
            }

            co_return std::move(result);
        }
        future<pq::result> receive_result () {
            while (PQisBusy(this->conn.get())) {
                if (!PQconsumeInput(this->conn.get())) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input failed");
                }
                if (!PQisBusy(this->conn.get())) {
                    break;
                }
                this->fd_ = PQsocket(this->conn.get());
                this->cancellation.reset(this->ctx);
                if (this->timeoutms_) {
                    this->cancellation.timeout(this->timeoutms_);
                }
                if (-1 == co_await async::read_ready(this->ctx, this->fd_, this->cancellation)) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_CONNECTION_TIMEOUT, "postgre: timeout was reached (read)");
                }
            }

            auto res = pq::result {PQgetResult(this->conn.get())};
            co_await this->receive_notifications();

            co_return std::move(res);
        }

        static error_code result_status_to_error_code (result &result) {
            switch (PQresultStatus(result.native_handle())) {
                case PGRES_SINGLE_TUPLE:
                case PGRES_TUPLES_OK:
                case PGRES_COMMAND_OK:
                    return error_code::RESULT_STATUS_OK;
                case PGRES_BAD_RESPONSE:
                    return error_code::RESULT_STATUS_BAD_RESPONSE;
                case PGRES_EMPTY_QUERY:
                    return error_code::RESULT_STATUS_EMPTY_QUERY;
                case PGRES_FATAL_ERROR:
                    return error_code::RESULT_STATUS_FATAL_ERROR;
                case PGRES_PIPELINE_ABORTED:
                    return error_code::RESULT_STATUS_PIPELINE_ABORTED;
                default:
                    return error_code::RESULT_STATUS_UNEXPECTED;
            }
        }

        [[nodiscard]] PGconn * native_handle () const {
            this->_check_conn();
            return this->conn.get();
        }

        [[nodiscard]] bool connected () const {
            return this->conn && PQstatus(this->conn.get()) == ConnStatusType::CONNECTION_OK;
        }

        manapi::future<> receive_notifications () {

            while (true) {
                notification notify {PQnotifies(this->conn.get())};
                if (!notify) {
                    break;
                }

                if (this->notify_cb) {
                    co_await this->notify_cb(std::move(notify));
                }
            }
            co_return;
        }

        async::cancellation_action cancellation{nullptr};
        std::function<manapi::future<>(notification notify)> notify_cb{nullptr};
        ssize_t timeoutms_;
        bool init_{true};
        int fd_{-1};
        std::unique_ptr<PGconn, pgconn_deleter> conn{nullptr};
        async::shared_ctx ctx;
    };
};