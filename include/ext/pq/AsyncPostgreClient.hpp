#pragma once


#include "../../../src/include/ManapiUtils.hpp"
#include "../../async/ManapiAsyncContext.hpp"
#include "../../async/ManapiAsyncSocket.hpp"

#include "./AsyncPostgreResult.hpp"
#include "./AsyncPostgreError.hpp"
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

    class connection {
        enum data_flags {
            DATA_FLAG_INIT = 1
        };
        struct pgconn_deleter {
            void operator()(PGconn *p) {
                PQfinish(p);
            }
        };

        struct data_t {
            int flags;
            async::cancellation_action cancellation{nullptr};
            std::move_only_function<manapi::future<>(notification notify)> notify_cb_{nullptr};
            ssize_t timeoutms_;
            manapi::socket_t fd_{-1};
            std::unique_ptr<PGconn, pgconn_deleter> conn{nullptr};
            async::mutex mx;
        };
    public:
        connection () : data_(nullptr) {

        }

        ~connection () = default;

        static manapi::error::status_or<pq::connection> create () MANAPIHTTP_NOEXCEPT {
            try {
                pq::connection conn;
                conn.data_ = std::make_shared<data_t>(0, nullptr, nullptr, 5000, -1, nullptr);
                return std::move(conn);
            }
            catch (std::exception const &e) {
                return error::status_resource_exhausted();
            }
        }

        manapi::future<manapi::sys_error::status> connect (std::string_view uri) {
            manapi::sys_error::status status;
            try {
                if (!this->data_)
                    co_return error::status_invalid_argument("pq:connection doesn't exists");

                if (this->data_->flags & DATA_FLAG_INIT)
                    co_return error::status_already_exists();

                this->data_->conn.reset(PQconnectStart(uri.data()));

                if (!this->data_->conn) {
                    status = error::status_resource_exhausted();
                    goto err;
                }

                if (PQstatus(this->data_->conn.get()) == CONNECTION_BAD) {
                    status = error::status_invalid_argument("pq::connection_bad");
                    goto err;
                }

                if (PQsetnonblocking(this->data_->conn.get(), 1)) {
                    status = error::status_invalid_argument("pq::nonblocking failed");
                    goto err;
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
                            status = co_await async::read_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation));
                            if (!status)
                                goto err;
                        continue;
                        case PGRES_POLLING_WRITING:
                            this->data_->fd_ = PQsocket(this->data_->conn.get());
                            status = co_await async::write_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation));
                            if (!status)
                                goto err;

                        continue;
                        case PGRES_POLLING_FAILED:
                            status = manapi::error::status_aborted("pq:polling failed");
                        goto err;
                        default:
                            break;
                    }

                    break;
                }

                this->data_->flags |= DATA_FLAG_INIT;

                co_return error::status_ok();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "pq::connect failed", e.what());
                status = error::status_internal("pq::connect failed");
            }

            err:
            if (this->data_)
                this->data_.reset();

            co_return std::move(status);
        }

        manapi::future<manapi::error::status> connect (std::string_view host, std::string_view port, std::string_view username, std::string_view password, std::string_view database) {
            auto uri = std::format("postgresql://{}:{}@{}:{}/{}", username, password, host, port, database);
            co_return co_await this->connect(uri);
        }

        manapi::future<pq::status_or<pq::result>> exec (std::string_view sql) {
            pq::status status;

            status = this->check_conn_();

            if (!status)
                goto err;

            try {
                auto lk = co_await this->data_->mx.lock_guard();

                if (!PQsendQueryParams(this->data_->conn.get(), sql.data(), 0, nullptr, nullptr, nullptr, nullptr, 1)) {
                    status = manapi::error::status_invalid_argument("pq:send the query failed");
                    goto err;
                }

                co_return co_await this->generic_single_result_query();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "pq::exec failed", e.what());
                status = manapi::error::status_internal("pq::exec failed");
            }
err:
            co_return std::move(status);
        }

        template<typename ...Args>
        manapi::future<pq::status_or<pq::result>> exec (std::string_view sql, Args &&...args) {
            {
                pq::status status = this->check_conn_();
                if (!status)
                    co_return std::move(status);
            }

            try {
                auto lk = co_await this->data_->mx.lock_guard();

                std::string buffer;
                auto [t, v, l, f] = pq::serialize(buffer, std::make_tuple(args...));
                if (!PQsendQueryParams(this->data_->conn.get(), sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1)) {
                    co_return pq::status{error::status_internal("pq:send query failed")};
                }

                co_return co_await this->generic_single_result_query();
            }
            catch (std::bad_alloc const &) {
                co_return pq::status{error::status_resource_exhausted()};
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "pq:exec", e.what());
                co_return pq::status{error::status_internal("pq:exec")};
            }
        }

        /**
         * Escape string.
         * DataLoss error in that function means that you need to resize the source buffer and repeat again
         *
         * @param dst to copy
         * @param dst_size destionation size
         * @param text source string
         * @return Ok if in case of sucess; otherwise, it returns DataLoss, InternalError, InvalidArgument
         */
        manapi::error::status esc (char *dst, std::size_t *dst_size, std::string_view text) MANAPIHTTP_NOEXCEPT {
            if (!dst || !dst_size)
                return error::status_invalid_argument("null");
            auto res = this->check_conn_();
            if (!res)
                return std::move(res);
            char buff[text.size() * 2 + 1];
            auto copied = this->esc_to_buff(text, buff);
            if (!copied.ok())
                return copied.err();
            auto const size = copied.unwrap();
            if (*dst_size < size) {
                *dst_size = size;
                return error::status_data_loss("resize");
            }
            memcpy (dst, buff, size);
            return error::status_ok();
        }

        /**
         * Escape string
         * @param text source string
         * @return Ok in case of success; otherwise, it returns InternalError, InvalidArgument
         */
        manapi::error::status_or<std::string> esc (std::string_view text) MANAPIHTTP_NOEXCEPT {
            try {
                auto res = this->check_conn_();
                if (!res)
                    return std::move(res);
                char buff[text.size() * 2 + 1];
                auto copied = this->esc_to_buff(text, buff);
                if (!copied.ok())
                    return copied.err();
                std::string escaped;
                auto const size = copied.unwrap();
                escaped.resize(size);
                memcpy (escaped.data(), buff, size);
                return std::move(escaped);
            }
            catch (std::bad_alloc const &) {
                return error::status_resource_exhausted();
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                return error::status_internal();
            }
        }

        void close () MANAPIHTTP_NOEXCEPT {
            if (!this->data_)
                return;

            this->data_->conn.reset();
            this->data_->fd_ = 0;
            this->data_->timeoutms_ = 5000;

            if (this->data_->flags & DATA_FLAG_INIT)
                this->data_->flags ^= DATA_FLAG_INIT;
        }

        void notify_cb (std::move_only_function<manapi::future<>(notification notify)> cb) MANAPIHTTP_NOEXCEPT {
            if (this->data_)
                this->data_->notify_cb_ = std::move(cb);
        }

        void timeout (ssize_t ms) MANAPIHTTP_NOEXCEPT {
            if (this->data_)
                this->data_->timeoutms_ = ms;
        }

        MANAPIHTTP_NODISCARD ssize_t timeout () const MANAPIHTTP_NOEXCEPT {
            if (!this->data_)
                return 0;

            return this->data_->timeoutms_;
        }
    private:
        MANAPIHTTP_NODISCARD manapi::error::status check_conn_ () const MANAPIHTTP_NOEXCEPT {
            if (this->data_ && this->data_->conn) {
                return error::status_ok();
            }

            return error::status_invalid_argument("pq:connection doesn't exists");
        }

        manapi::error::status_or<size_t> esc_to_buff (std::string_view text, char *buff) MANAPIHTTP_NOEXCEPT {
            int err{0};
            auto const copied{
                PQescapeStringConn(this->data_->conn.get(), buff, text.data(), text.size(), &err)};
            if (err) {
                manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s failed and returned %d for %.*s", "PQescapeStringConn", err, text.size(), text.data());
                return error::status_invalid_argument("PQescapeStringConn failed");
            }
            return copied;
        }

        future<manapi::error::status> flush () {
            int rhs = PQflush(this->data_->conn.get());

            if (rhs == -1) {
                co_return manapi::error::status_internal("pq:flush failed");
            }

            if (rhs == 0) {
                /* done */
                co_return manapi::error::status_ok();
            }

            this->data_->cancellation.reset();
            if (this->data_->timeoutms_) {
                this->data_->cancellation.timeout(this->data_->timeoutms_);
            }

            int revents = ev::READ|ev::WRITE;

            while (true) {
                if (revents & ev::READ) {
                    if (!PQconsumeInput(this->data_->conn.get())) {
                        co_return manapi::error::status_internal ("pq:Consume input error");
                    }
                }

                if (revents & ev::WRITE) {
                    rhs = PQflush(this->data_->conn.get());
                    if (rhs == -1) {
                        co_return manapi::error::status_internal("pq:flush failed");
                    }

                    if (rhs == 0) {
                        /* done */
                        break;
                    }
                }

                auto res = co_await async::custom_ready(ev::READ|ev::WRITE, PQsocket(this->data_->conn.get()), this->data_->cancellation.sub());

                if (!res) {
                    if (res.code() == ERR_CANCELLED)
                        co_return manapi::error::status_aborted("pq:timeout was reached");
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%s failed due to %.*s, %.*s", "pq", "io", res.message().size(), res.message().data(),
                        res.sysmsg().size(), res.sysmsg().data());
                    co_return manapi::error::status_internal(res.message());
                }

                revents = res.unwrap();
            }

            co_return manapi::error::status_ok();
        }

        future<pq::status_or<pq::result>> generic_single_result_query () {
            pq::status status;

            status = co_await this->flush();

            if (!status)
                co_return std::move(status);

            auto result_res = co_await this->receive_result();

            if (!result_res)
                co_return pq::status{result_res.err()};

            auto second_result_res = co_await this->receive_result();

            if (second_result_res && second_result_res.unwrap()) {
                co_return pq::status{manapi::error::status_internal("pq:unexpected non-null result")};
            }

            auto result = result_res.unwrap();
            auto err = connection::result_status_to_error_code(result);

            auto sqlstate = result.sqlstate();

            if (err != error_code::RESULT_STATUS_OK) {
                co_return pq::status (manapi::ERR_INVALID_ARGUMENT, "pq:requst failed", sqlstate, PQerrorMessage(this->data_->conn.get()));
            }

            co_return std::move(result);
        }

        future<manapi::error::status_or<pq::result>> receive_result () {
            while (PQisBusy(this->data_->conn.get())) {
                if (!PQconsumeInput(this->data_->conn.get())) {
                    co_return manapi::error::status_internal ("pq:consume input failed");
                }
                if (!PQisBusy(this->data_->conn.get())) {
                    break;
                }
                this->data_->fd_ = PQsocket(this->data_->conn.get());
                this->data_->cancellation.reset();
                if (this->data_->timeoutms_) {
                    this->data_->cancellation.timeout(this->data_->timeoutms_);
                }
                auto res = co_await async::read_ready(this->data_->fd_, manapi::async::cancellation_action::unit(this->data_->cancellation));
                if (!res) {
                    if (res.code() == ERR_CANCELLED) {
                        co_return manapi::error::status_internal ("pq:timeout was reached");
                    }

                    co_return manapi::error::status_internal(res.msg());
                }
            }

            auto lnk = PQgetResult(this->data_->conn.get());
            if (!lnk)
                co_return manapi::error::status_internal("pq:empty result");
            auto res = pq::result {lnk};
            co_await this->receive_notifications();

            co_return std::move(res);
        }

        static error_code result_status_to_error_code (result &result) MANAPIHTTP_NOEXCEPT {
            auto const hdl = result.native_handle();
            if (!hdl)
                return error_code::RESULT_STATUS_FATAL_ERROR;

            switch (PQresultStatus(hdl)) {
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

        MANAPIHTTP_NODISCARD PGconn * native_handle () const MANAPIHTTP_NOEXCEPT {
            auto const res = this->check_conn_();
            if (res)
                return this->data_->conn.get();
            return nullptr;
        }

        MANAPIHTTP_NODISCARD bool connected () const MANAPIHTTP_NOEXCEPT {
            if (this->data_ && this->data_->conn)
                return (PQstatus(this->data_->conn.get()) == ConnStatusType::CONNECTION_OK);
            return false;
        }

        manapi::future<> receive_notifications () {
            while (true) {
                notification notify {PQnotifies(this->data_->conn.get())};

                if (!notify) {
                    break;
                }

                if (this->data_->notify_cb_) {
                    try { co_await this->data_->notify_cb_(std::move(notify)); }
                    catch (std::exception const &e) { manapi_log_trace("%s failed due to %s", "pq:receive_notifications", e.what()); }
                }
            }

            co_return;
        }

        std::shared_ptr<data_t> data_;
    };
};