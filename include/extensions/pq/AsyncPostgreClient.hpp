#pragma once


#include "../../async/ManapiAsyncContext.hpp"
#include "../../async/ManapiAsyncSocket.hpp"

#include "./AsyncPostgreResult.hpp"

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
    public:
        struct pgconn_deleter {
            void operator()(PGconn *p) {
                PQfinish(p);
            }
        };

        connection (std::shared_ptr<async::context> ctx) {
            this->ctx = std::move(ctx);
            this->conn = nullptr;
        }

        ~connection () = default;

        manapi::future<> connect (std::string_view host, std::string_view port, std::string_view username, std::string_view password, std::string_view database) {
            auto uri = std::format("postgresql://{}:{}@{}:{}/{}", username, password, host, port, database);
            this->conn.reset(PQconnectStart(uri.data()));
            if (std::exchange(this->init_, false)) {
                if (PQstatus(this->conn.get()) == CONNECTION_BAD) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "Connection bad");
                }

                if (PQsetnonblocking(this->conn.get(), 1)) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "set non-blocing failure");
                }

                PQsetNoticeProcessor(
                    this->conn.get(), +[](void *, const char *) -> void{}, nullptr);

                this->fd_ = PQsocket(this->conn.get());
            }

            while (true) {
                auto ret = PQconnectPoll(this->conn.get());

                switch (ret) {
                    case PGRES_POLLING_READING:
                        co_await async::read_ready(this->ctx, this->fd_);
                    continue;
                    case PGRES_POLLING_WRITING:
                        co_await async::write_ready(this->ctx, this->fd_);
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

        manapi::future<pq::result> exec (std::string_view sql) {
            if (!PQsendQueryParams(this->conn.get(), sql.data(), 0, nullptr, nullptr, nullptr, nullptr, 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send the query failed");
            }

            return this->generic_single_result_query();
        }

        template<typename ...Args>
        manapi::future<pq::result> exec (std::string_view sql, Args &&...args) {
            std::string buffer;
            auto [t, v, l, f] = pq::serialize(buffer, std::make_tuple(args...));
            if (!PQsendQueryParams(this->conn.get(), sql.data(), t.size(), t.data(), v.data(), l.data(), f.data(), 1)) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "send the query failed");
            }

            co_return std::move(co_await this->generic_single_result_query());
        }

        std::string esc (std::string_view text) {
            std::string buff;
            buff.resize(text.size() * 2 + 1);
            const auto copied = this->esc_to_buff(text, buff.data());
            buff.resize(copied);
            return std::move(buff);
        }

    private:
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

            co_await async::promise<void> (this->ctx, [this, fd = PQsocket(this->conn.get())] (async::promise<void>::resolve_t resolve, async::promise<void>::reject_t reject) -> future<> {
                auto w = co_await this->ctx->eventloop()->watch_fd(fd, ev::READ|ev::WRITE, [this, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
                    if (revents & ev::READ) {
                        if (!PQconsumeInput(this->conn.get())) {
                            auto _reject = std::move(reject);
                            this->ctx->eventloop()->stop_watcher(w);
                            _reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input error")));
                        }
                    }
                    if (revents & ev::WRITE) {
                        auto rhs = PQflush(this->conn.get());
                        if (rhs == -1) {
                            auto _reject = std::move(reject);
                            this->ctx->eventloop()->stop_watcher(w);
                            _reject(std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "flush failed")));
                        }
                        if (rhs == 0) {
                            /* done */
                            auto _resolve = std::move(resolve);
                            this->ctx->eventloop()->stop_watcher(w);
                            _resolve();
                        }
                    }
                });
            });
        }
        future<pq::result> generic_single_result_query () {
            co_await this->flush();
            auto result = co_await this->receive_result();
            if (co_await this->receive_result()) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "unexpected non-null result");
            }
            auto err = connection::result_status_to_error_code(result);
            if (err != error_code::RESULT_STATUS_OK) {
                THROW_MANAPIHTTP_EXCEPTION_WITH_CODE (ERR_POSTGRE_ERROR, err, "Failed to get result: {}", PQerrorMessage(this->conn.get()));
            }

            co_return std::move(result);
        }
        future<pq::result> receive_result () {
            while (PQisBusy(this->conn.get())) {
                co_await async::read_ready(this->ctx, this->fd_);
                if (!PQconsumeInput(this->conn.get())) {
                    THROW_MANAPIHTTP_EXCEPTION2 (ERR_POSTGRE_ERROR, "Consume input failed");
                }
            }

            co_return pq::result {PQgetResult(this->conn.get())};
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

        bool init_{true};
        int fd_{-1};
        std::unique_ptr<PGconn, pgconn_deleter> conn{nullptr};
        std::shared_ptr<async::context> ctx;
    };
};