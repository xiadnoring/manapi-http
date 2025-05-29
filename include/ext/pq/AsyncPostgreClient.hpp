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
        struct pgconn_deleter {
            void operator()(PGconn *p) {
                PQfinish(p);
            }
        };

        struct data_t {
            async::cancellation_action cancellation{nullptr};
            std::move_only_function<manapi::future<>(notification notify)> notify_cb_{nullptr};
            ssize_t timeoutms_;
            bool init_{true};
            manapi::fd_t fd_{-1};
            std::unique_ptr<PGconn, pgconn_deleter> conn{nullptr};
            async::mutex mx;
        };
    public:
        connection () {
            this->data_ = std::make_shared<data_t>(nullptr,
                nullptr, 5000, true, -1, nullptr);
        }

        ~connection () = default;

        manapi::future<> connect (std::string_view uri) {
            if (std::exchange(this->data_->init_, false)) {
                this->data_->conn.reset(PQconnectStart(uri.data()));

                if (PQstatus(this->data_->conn.get()) == CONNECTION_BAD) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "Connection bad");
                }

                if (PQsetnonblocking(this->data_->conn.get(), 1)) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "set non-blocing failure");
                }

                PQsetNoticeProcessor(
                    this->data_->conn.get(), +[](void *, const char *) -> void{}, nullptr);

                this->data_->fd_ = PQsocket(this->data_->conn.get());

                while (true) {
                    auto ret = PQconnectPoll(this->data_->conn.get());

                    this->data_->cancellation.reset();

                    if (this->data_->timeoutms_) {
                        this->data_->cancellation.timeout(this->data_->timeoutms_);
                    }

                    switch (ret) {
                        case PGRES_POLLING_READING:
                            this->data_->fd_ = PQsocket(this->data_->conn.get());
                            if (-1 == co_await async::read_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation))) {
                                THROW_MANAPIHTTP_EXCEPTION2(ERR_CONNECTION_TIMEOUT, "postgre: timeout was reached (read)");
                            }
                        continue;
                        case PGRES_POLLING_WRITING:
                            this->data_->fd_ = PQsocket(this->data_->conn.get());
                            if (-1 == co_await async::write_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation))) {
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
            this->check_conn_();

            auto lk = co_await this->data_->mx.lock_guard();

            if (!PQsendQueryParams(this->data_->conn.get(), sql.data(), 0, nullptr, nullptr, nullptr, nullptr, 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send the query failed");
            }

            co_return co_await this->generic_single_result_query();
        }

        template<typename ...Args>
        manapi::future<pq::result> exec (std::string_view sql, Args &&...args) {
            this->check_conn_();

            auto lk = co_await this->data_->mx.lock_guard();

            std::string buffer;
            auto [t, v, l, f] = pq::serialize(buffer, std::make_tuple(args...));
            if (!PQsendQueryParams(this->data_->conn.get(), sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send query failed");
            }

            co_return co_await this->generic_single_result_query();
        }

        std::string esc (std::string_view text) {
            this->check_conn_();
            std::string buff;
            buff.resize(text.size() * 2 + 1);
            const auto copied = this->esc_to_buff(text, buff.data());
            buff.resize(copied);
            return std::move(buff);
        }

        void close () {
            this->data_->conn.reset();
            this->data_->init_ = false;
        }

        void notify_cb (std::move_only_function<manapi::future<>(notification notify)> cb) {
            this->data_->notify_cb_ = std::move(cb);
        }

        void timeout (ssize_t ms) {
            this->data_->timeoutms_ = ms;
        }

        [[nodiscard]] ssize_t timeout () const {
            return this->data_->timeoutms_;
        }
    private:
        void check_conn_ () const {
            if (this->data_->conn) {
                return;
            }

            THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "connection doesn't exists");
        }
        size_t esc_to_buff (std::string_view text, char *buff) {
            int err{0};
            auto const copied{
                PQescapeStringConn(this->data_->conn.get(), buff, text.data(), text.size(), &err)};
            if (err) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "PQescapeStringConn failed");
            }
            return copied;
        }

        future<> flush () {
            auto rhs = PQflush(this->data_->conn.get());
            if (rhs == -1) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "flush failed");
            }
            if (rhs == 0) {
                /* done */
                co_return;
            }

            this->data_->cancellation.reset();
            if (this->data_->timeoutms_) {
                this->data_->cancellation.timeout(this->data_->timeoutms_);
            }

            int revents = ev::READ|ev::WRITE;

            while (true) {
                if (revents & ev::READ) {
                    if (!PQconsumeInput(this->data_->conn.get())) {
                        THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input error");
                    }
                }

                if (revents & ev::WRITE) {
                    auto rhs = PQflush(this->data_->conn.get());
                    if (rhs == -1) {
                        THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "flush failed");
                    }

                    if (rhs == 0) {
                        /* done */
                        break;
                    }
                }

                if (-1 == (revents = co_await async::custom_ready(ev::READ|ev::WRITE, PQsocket(this->data_->conn.get()),
                    manapi::async::cancellation_action::unit(this->data_->cancellation)))) {
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
                throw pq::sqlexception (sqlstate, PQerrorMessage(this->data_->conn.get()));
            }

            co_return std::move(result);
        }
        future<pq::result> receive_result () {
            while (PQisBusy(this->data_->conn.get())) {
                if (!PQconsumeInput(this->data_->conn.get())) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input failed");
                }
                if (!PQisBusy(this->data_->conn.get())) {
                    break;
                }
                this->data_->fd_ = PQsocket(this->data_->conn.get());
                this->data_->cancellation.reset();
                if (this->data_->timeoutms_) {
                    this->data_->cancellation.timeout(this->data_->timeoutms_);
                }
                if (-1 == co_await async::read_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation))) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_CONNECTION_TIMEOUT, "postgre: timeout was reached (read)");
                }
            }

            auto res = pq::result {PQgetResult(this->data_->conn.get())};
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
            this->check_conn_();
            return this->data_->conn.get();
        }

        [[nodiscard]] bool connected () const {
            return this->data_->conn && PQstatus(this->data_->conn.get()) == ConnStatusType::CONNECTION_OK;
        }

        manapi::future<> receive_notifications () {

            while (true) {
                notification notify {PQnotifies(this->data_->conn.get())};
                if (!notify) {
                    break;
                }

                if (this->data_->notify_cb_) {
                    co_await this->data_->notify_cb_(std::move(notify));
                }
            }
            co_return;
        }

        std::shared_ptr<data_t> data_;
    };
};