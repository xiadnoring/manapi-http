#include "./AsyncPostgrePool.hpp"
#include "./AsyncPostgreClient.hpp"
#include "./../../ManapiTimerPool.hpp"

#define PSQL_POOL_FLAG_ACTIVE 1<<0
#define PSQL_POOL_FLAG_STOP 1<<1
#define PSQL_POOL_FLAG_CONN_LOST 1<<2
#define PSQL_DATA_FLAG_INIT 1<<0

struct pgconn_deleter {
    void operator()(manapi::ext::pq::PGconn *p) {
        PQfinish(p);
    }
};

struct manapi::ext::pq::connection::data_t {
    int flags;
    std::move_only_function<manapi::future<>(notification notify)> notify_cb_{nullptr};
    manapi::socket_t fd_{-1};
    std::unique_ptr<PGconn, pgconn_deleter> conn{nullptr};
    async::mutex mx;
};

void ext_pq_item_waiting_update (std::size_t weight, std::size_t&waiting, std::size_t next_value, std::set<std::pair<std::size_t, std::shared_ptr<manapi::ext::pq::pool>>> &m_slaves, const std::shared_ptr<manapi::ext::pq::pool> &m_pool) {
    auto f = m_slaves.erase(std::make_pair(weight, m_pool));
    waiting = next_value;
    if (f) {
        MANAPIHTTP_MUST_ALLOC_START
        m_slaves.insert(std::make_pair(m_pool->waiting(), m_pool));
        MANAPIHTTP_MUST_ALLOC_END
    }
}

void ext_pq_item_waiting_update (std::size_t old_weight, std::size_t new_weight, std::set<std::pair<std::size_t, std::shared_ptr<manapi::ext::pq::pool>>> &m_slaves, const std::shared_ptr<manapi::ext::pq::pool> &m_pool) {
    if (new_weight == old_weight) {
        return;
    }
    auto f = m_slaves.erase(std::make_pair(old_weight, m_pool));
    if (f) {
        MANAPIHTTP_MUST_ALLOC_START
        m_slaves.insert(std::make_pair(new_weight, m_pool));
        MANAPIHTTP_MUST_ALLOC_END
    }
}

manapi::ext::pq::item::item(std::shared_ptr<pool> p, manapi::ext::pq::connection *conn) {
    this->m_pool = std::move(p);
    this->m_conn = conn;
}

manapi::ext::pq::item::~item() {
    this->release();
}

manapi::ext::pq::connection * manapi::ext::pq::item::operator->() MANAPIHTTP_NOEXCEPT {
    return this->m_conn;
}

const manapi::ext::pq::connection * manapi::ext::pq::item::operator->() const MANAPIHTTP_NOEXCEPT {
    return this->m_conn;
}

manapi::ext::pq::connection & manapi::ext::pq::item::operator*() MANAPIHTTP_NOEXCEPT {
    return *this->m_conn;
}

const manapi::ext::pq::connection & manapi::ext::pq::item::operator*() const MANAPIHTTP_NOEXCEPT {
    return *this->m_conn;
}

void manapi::ext::pq::item::release() MANAPIHTTP_NOEXCEPT {
    try {
        if (this->m_pool && this->m_conn) {
            // we suggest that |available| has
            // at least one free space
            this->m_pool->m_available.push(this->m_conn);
            if (this->m_pool->m_db) {
                ext_pq_item_waiting_update (this->m_pool->size(), this->m_pool->m_waiting, this->m_pool->m_waiting+1,
                    this->m_pool->m_db->m_slaves, this->m_pool);
            }
            else {

                this->m_pool->m_waiting++;
            }
            this->m_pool->m_mx.unlock();
        }

        this->m_pool = nullptr;
        this->m_conn = nullptr;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s:%s failed due to %s", "item",
            "release-we suggest that |available| has at least one free space", e.what());
        std::rethrow_exception(std::current_exception());
    }
}

manapi::ext::pq::pool::pool() {
    this->m_flags = 0;
    this->m_waiting = 0;
}

manapi::ext::pq::pool::~pool() {
    if (this->m_ping) {
        this->m_ping.stop();
    }

    int i = 0;
    while (!this->m_mx.try_to_lock()) {
        manapi_log_error("%s: %s(%d)", "pool::~pool", "mutex is locked", i++);
        this->m_mx.unlock();
    }
    this->m_mx.unlock();
}

manapi::status_or<std::shared_ptr<manapi::ext::pq::pool>> manapi::ext::pq::pool::create() MANAPIHTTP_NOEXCEPT {
    std::shared_ptr<pq::pool> m (new (std::nothrow) pq::pool{});
    if (m) return std::move(m);
    return status_resource_exhausted();
}

manapi::future<manapi::ev::status> manapi::ext::pq::pool::connect(std::size_t size, std::string host, std::string port, std::string user, std::string password, std::string db, manapi::ctoken token) {
    auto params = manapi::json::object();
    params.insert("host", std::move(host));
    params.insert("port", std::move(port));
    params.insert("user", std::move(user));
    params.insert("password", std::move(password));
    params.insert("dbname", std::move(db));
    return this->connect(size, std::move(params), std::move(token));
}

manapi::future<manapi::ev::status> manapi::ext::pq::pool::connect(std::size_t size, manapi::json params, manapi::ctoken token) {
    if (this->m_flags & PSQL_POOL_FLAG_ACTIVE)
        co_return status_already_exists("psql:pool is active");

    manapi::ev::status status = status_ok();
    bool failed2connect = false;
    try {
        if (!this->m_clients.empty()) {
            manapi_log_warn("psql:clients aren't cleaned up");
        }
        this->m_clients.reserve(size);
        for (std::size_t i = 0; i < size; i++){
            ext::pq::connection conn = ext::pq::connection::create().unwrap();
            status = co_await conn.connect(params, token.sub());
            if (!status) {
                if (status.code() == ERR_INVALID_ARGUMENT || status.code() == ERR_ALREADY_EXISTS)
                    goto err;
                failed2connect = true;
                status = manapi::status_ok();
            }
            this->m_clients.push_back(std::move(conn));
        }
        for (auto &c : this->m_clients)
            this->m_available.push(&c);
        this->m_flags |= PSQL_POOL_FLAG_ACTIVE;
        for (auto &_ : this->m_clients)
            this->m_mx.unlock();

        if (failed2connect) {
            manapi::unwrap(this->connected(false));
        }

        co_return std::move(status);
    }
    catch (std::exception const &e) {
        status = manapi::status_internal("psql:connect failed");
        manapi_log_error("%s due to %s", "psql:connect failed", e.what());
    }
    err:
        this->m_clients.clear();
    while (!this->m_available.empty())
        this->m_available.pop();

    co_return std::move(status);
}

manapi::future<manapi::status> manapi::ext::pq::pool::stop() {
    struct psql_stop_unflag {
        uint8_t* flags;
        ~psql_stop_unflag() { (*this->flags) ^= PSQL_POOL_FLAG_STOP; }
    };

    auto s = this->shared_from_this();
    auto lk = co_await this->m_mx.lock_guard();
    if (!(this->m_flags & PSQL_POOL_FLAG_ACTIVE))
        co_return status_not_found("psql:pool isn't active");

    psql_stop_unflag unflag{&this->m_flags};
    this->m_flags |= PSQL_POOL_FLAG_STOP;

    while (!this->m_available.empty())
        this->m_available.pop();

    while (this->m_waiting != this->m_clients.size()) {
        while (!this->m_available.empty())
            this->m_available.pop();
        co_await this->m_mx.lock();
    }

    this->m_flags ^= PSQL_POOL_FLAG_ACTIVE;

    co_return status_ok();
}

manapi::future<manapi::status_or<manapi::ext::pq::item>> manapi::ext::pq::pool::peer() {
    auto s = this->shared_from_this();
    while (true) {
        auto lk = co_await this->m_mx.lock_guard();

        while (this->m_available.empty()) {
            co_await this->m_mx.lock();
        }

        if (this->m_flags & PSQL_POOL_FLAG_STOP) {
            this->m_mx.unlock();
            continue;
        }

        auto item = this->m_available.top();
        this->m_available.pop();
        if (this->m_db) {
            auto f = this->m_db->m_slaves.erase(std::make_pair(this->waiting(), s));
            ext_pq_item_waiting_update (this->size(), this->m_waiting, this->m_waiting-1,
                this->m_db->m_slaves, this->shared_from_this());
            if (f) {
                this->m_db->m_slaves.insert(std::make_pair(this->waiting(), s));
            }
        }
        else {
            this->m_waiting--;
        }

        co_return pq::item (std::move(s), item);
    }
}

std::size_t manapi::ext::pq::pool::size() const MANAPIHTTP_NOEXCEPT {
    return this->m_clients.size();
}

bool manapi::ext::pq::pool::connected() const MANAPIHTTP_NOEXCEPT {
    return !(this->m_flags & PSQL_POOL_FLAG_CONN_LOST);
}

std::size_t manapi::ext::pq::pool::waiting() const MANAPIHTTP_NOEXCEPT {
    return this->size() - this->m_waiting + this->m_mx.waiting() + (this->m_flags & PSQL_POOL_FLAG_CONN_LOST ? 1000 : 0);
}

manapi::status manapi::ext::pq::pool::connected(bool active) MANAPIHTTP_NOEXCEPT {
    try {
        auto old_weight = this->size();

        if (this->m_ping) {
            this->m_ping.stop();
            this->m_ping=nullptr;
        }

        if (active) {
            if (this->m_flags & PSQL_POOL_FLAG_CONN_LOST)
                this->m_flags ^= PSQL_POOL_FLAG_CONN_LOST;
        }
        else {
            this->m_ping = manapi::async::current()->timerpool()->append_timer_async(500,
                [this] (manapi::timer t) -> manapi::future<> {
                if (!this->m_clients.empty()) {
                    auto wrk = this->m_clients.begin();
                    if (co_await wrk->ping()) {
                        auto res = this->connected(true);
                        if (!res) {
                            res.log();
                        }
                    }
                    else {
                        t.again(t.interval().count()).unwrap();
                    }
                }
            }).unwrap();

            this->m_flags |= PSQL_POOL_FLAG_CONN_LOST;
        }

        auto new_weight = this->size();

        ext_pq_item_waiting_update (old_weight, new_weight, this->m_db->m_slaves, this->shared_from_this());
        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_ferror(e.what());
        return status_internal("pool::connected() failed");
    }
}

manapi::future<> manapi::ext::pq::pool::ping(std::size_t timeoutms) {
    try {
        if (!this->m_clients.empty()) {
            auto it = manapi::unwrap(co_await this->peer());
            auto res = co_await it->ping(timeoutms);
            if (!res) {
                // failed to connect
                this->connected(false);
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_ferror(e.what());
    }
}

manapi::ext::pq::db::db() {
}

manapi::ext::pq::db::~db() = default;

manapi::status_or<std::shared_ptr<manapi::ext::pq::db>> manapi::ext::pq::db::create() MANAPIHTTP_NOEXCEPT {
    std::shared_ptr<db> m (new (std::nothrow) db);
    if (m)
        return std::move(m);
    return status_resource_exhausted();
}

manapi::status manapi::ext::pq::db::set_master(std::shared_ptr<pq::pool> master) {
    if (master && master->m_db) {
        return manapi::status_already_exists("already has db");
    }
    this->m_master = std::move(master);
    return manapi::status_ok();
}

void manapi::ext::pq::db::remove_master() {
    if (this->m_master) {
        this->m_master->m_db = nullptr;
        this->m_master = nullptr;
    }
}

manapi::status manapi::ext::pq::db::add_slave(std::shared_ptr<pq::pool> slave) MANAPIHTTP_NOEXCEPT {
    try {
        if (slave) {
            if (slave->m_db) {
                return manapi::status_already_exists("already has db");
            }
            this->m_slaves.insert(std::make_pair(slave->size(), slave));
        }
        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_ferror(e.what());
        return status_internal();
    }
}

void manapi::ext::pq::db::remove_slave(std::shared_ptr<pq::pool> slave) {
    if (this->m_slaves.erase(std::make_pair(slave->size(), slave))) {
        slave->m_db = nullptr;
    }
}

void manapi::ext::pq::db::remove_slaves() {
    for (auto &slave : this->m_slaves) {
        slave.second->m_db = nullptr;
    }
    this->m_slaves.clear();
}

manapi::future<manapi::status_or<manapi::ext::pq::item>> manapi::ext::pq::db::slave() {
    if (this->m_slaves.empty() || (this->m_master && this->m_slaves.begin()->second->waiting() > this->m_master->waiting())) {
        return this->master();
    }
    auto s = this->m_slaves.begin()->second;
    return s->peer();
}

manapi::future<manapi::status_or<manapi::ext::pq::item>> manapi::ext::pq::db::master() {
    if (this->m_master) {
        co_return co_await this->m_master->peer();
    }
    co_return status_not_found("db:not found");
}

bool manapi::ext::pq::db::has_master() const {
    return !!this->m_master;
}

bool manapi::ext::pq::db::has_slaves() const {
    return !!this->m_slaves.size();
}

manapi::future<manapi::ext::pq::status_or<manapi::ext::pq::result>> manapi::ext::pq::db::pexec(ktypes type, const char *command, int nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths,const int *paramFormats, ctoken token) {
    while (true) {
        auto wrk_res = type == kMaster ? (co_await this->master()) :(co_await this->slave());
        if (!wrk_res) {
            co_return ext::pq::status (wrk_res.err());
        }
        auto wrk = wrk_res.unwrap();

        if (!wrk.m_pool->connected()) {
            break;
        }

        auto result = co_await wrk->pexec(command, nParams, paramTypes, paramValues, paramLengths, paramFormats, 1, (token));

        if (!result.ok() && (result.sqlcode() == 17696780 || result.code() == ERR_ABORTED || result.code() == ERR_CANCELLED)) {
            manapi::async::run(wrk.m_pool->ping());
            continue;
        }

        if (!wrk.m_pool->connected()) {
            wrk.m_pool->connected(true);
        }

        token.cancel();
        if (result.ok()) {
            co_return result.unwrap().copy();
        }

        co_return std::move(result);
    }

    token.cancel();
    co_return ext::pq::status {manapi::status_not_found("pq:no active client")};
}

manapi::ext::pq::connection::connection() : data_(nullptr) {
}

manapi::ext::pq::connection::~connection() = default;

manapi::status_or<manapi::ext::pq::connection> manapi::ext::pq::connection::create() MANAPIHTTP_NOEXCEPT {
    try {
        pq::connection conn;
        conn.data_ = std::make_shared<data_t>(0, nullptr, 5000, nullptr);
        return std::move(conn);
    }
    catch (std::exception const &e) {
        return status_resource_exhausted();
    }
}

manapi::future<manapi::ev::status> manapi::ext::pq::connection::connect(std::string_view uri,
                                                                                      manapi::ctoken token) {
    manapi::ev::status status;
    try {
        if (!this->data_)
            co_return status_invalid_argument("pq:connection doesn't exist");

        if (this->data_->flags & PSQL_DATA_FLAG_INIT)
            co_return status_already_exists();

        this->data_->conn.reset(PQconnectStart(uri.data()));

        status = co_await connect_psql_ (token);
        if (!status) {
            goto err;
        }
        co_return std::move(status);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "pq::connect failed", e.what());
        status = status_internal("pq::connect failed");
    }

    err:
        this->close();
    co_return std::move(status);
}

manapi::future<manapi::status> manapi::ext::pq::connection::connect(std::string host, std::string port,
                                                                                  std::string username, std::string password, std::string database, manapi::ctoken token) {
    auto keywords = std::unique_ptr<const char *, ev::impl_array_deleter<const char *>>(new const char*[6]);
    auto values = std::unique_ptr<const char *, ev::impl_array_deleter<const char *>>(new const char*[6]);
    auto keys_ptr = keywords.get();
    auto values_ptr = values.get();
    keys_ptr[0] = "host";
    keys_ptr[1] = "port";
    keys_ptr[2] = "user";
    keys_ptr[3] = "password";
    keys_ptr[4] = "dbname";
    values_ptr[0] = host.data();
    values_ptr[1] = port.data();
    values_ptr[2] = username.data();
    values_ptr[3] = password.data();
    values_ptr[4] = database.data();
    keys_ptr[5] = nullptr; values_ptr[5] = nullptr;
    co_return co_await this->connect (keys_ptr, values_ptr, std::move(token));
}

manapi::future<manapi::status> manapi::ext::pq::connection::connect(manapi::json params,
                                                                                  manapi::ctoken token) {
    if (params.is_object()) {
        auto keywords = std::unique_ptr<const char *, ev::impl_array_deleter<const char *>>(new const char*[params.size() + 1]);
        auto values = std::unique_ptr<const char *, ev::impl_array_deleter<const char *>>(new const char*[params.size() + 1]);
        auto keys_ptr = keywords.get();
        auto values_ptr = values.get();
        for (auto &p : params.entries()) {
            *(keys_ptr++) = p.first.data();
            if (!p.second.is_string())
                goto err;
            *(values_ptr++) = p.second.as_string().data();
        }
        *keys_ptr = nullptr;
        *values_ptr = nullptr;
        co_return co_await this->connect (keywords.get(), values.get(), std::move(token));
    }
    err:
        co_return status_invalid_argument("pq:invalid params");
}

manapi::future<manapi::status> manapi::ext::pq::connection::connect(const char * const *keywords,
                                                                                  const char * const *values, manapi::ctoken token) {
    // auto uri = std::format("postgresql://{}:{}@{}:{}/{}", username, password, host, port, database);
    // co_return co_await this->connect(uri);

    manapi::ev::status status;
    try {
        if (!this->data_)
            co_return status_invalid_argument("pq:connection doesn't exist");

        if (this->data_->flags & PSQL_DATA_FLAG_INIT)
            co_return status_already_exists();

        this->data_->conn.reset(PQconnectdbParams(keywords, values, 1));

        status = co_await connect_psql_ (token);
        if (!status) {
            //goto err;
        }
        co_return std::move(status);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "pq::connect failed", e.what());
        status = status_internal("pq::connect failed");
    }
    //
    // err:
    //     this->close();
    co_return std::move(status);
}

manapi::future<manapi::ext::pq::status_or<manapi::ext::pq::result>> manapi::ext::pq::connection::pexec(
    const char *command, int nParams, const Oid *paramTypes, const char * const *paramValues, const int *paramLengths,
    const int *paramFormats, int resultFormat, manapi::ctoken token) {
    pq::status status = this->check_conn_();
    if (!status)
        co_return std::move(status);

    try {
        auto lk = co_await this->data_->mx.lock_guard();

        if (!this->connected()) {
            manapi::unwrap(co_await this->connect_psql_(token));
        }

        if (!PQsendQueryParams(this->data_->conn.get(), command, nParams, paramTypes, paramValues, paramLengths, paramFormats, 1)) {
            token.cancel();
            co_return pq::status{status_internal("pq:send query failed")};
        }

        token.cancel();
        co_return co_await this->generic_single_result_query(token);
    }
    catch (std::bad_alloc const &) {
        token.cancel();
        co_return pq::status{status_resource_exhausted()};
    }
    catch (std::exception const &e) {
        token.cancel();
        manapi_log_error("%s failed due to %s", "pq:exec", e.what());
        co_return pq::status{status_internal("pq:exec")};
    }
}

manapi::future<bool> manapi::ext::pq::connection::ping(std::size_t timeoutms) {
    auto res = co_await this->execl("SELECT 1;", ctokens::timeout(timeoutms));
    if (!res.ok()) co_return false;
    co_return res.unwrap().size();
}

manapi::status manapi::ext::pq::connection::esc(char *dst, std::size_t *dst_size,
                                                              std::string_view text) MANAPIHTTP_NOEXCEPT {
    if (!dst || !dst_size)
        return status_invalid_argument("null");
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
        return status_data_loss("resize");
    }
    memcpy (dst, buff, size);
    return status_ok();
}

manapi::status_or<std::string> manapi::ext::pq::connection::esc(std::string_view text) MANAPIHTTP_NOEXCEPT {
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
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_internal();
    }
}

void manapi::ext::pq::connection::close() MANAPIHTTP_NOEXCEPT {
    if (!this->data_)
        return;

    this->data_->conn.reset();
    this->data_->fd_ = 0;

    if (this->data_->flags & PSQL_DATA_FLAG_INIT)
        this->data_->flags ^= PSQL_DATA_FLAG_INIT;
}

void manapi::ext::pq::connection::notify_cb(
std::move_only_function<manapi::future<>(notification notify)> cb) MANAPIHTTP_NOEXCEPT {
    if (this->data_)
        this->data_->notify_cb_ = std::move(cb);
}

manapi::future<manapi::ev::status> manapi::ext::pq::connection::connect_psql_(manapi::ctoken token) MANAPIHTTP_NOEXCEPT {
    manapi::ev::status status = manapi::ev::status_ok();
    try {
        if (!this->data_->conn) {
            status = status_resource_exhausted();
            goto err;
        }

        if (PQstatus(this->data_->conn.get()) == CONNECTION_BAD) {
            PQreset(this->data_->conn.get());
            if (PQstatus(this->data_->conn.get()) == CONNECTION_BAD)  {
                status = ev::status(ERR_ABORTED, std::string{std::format("pq::connection_bad msg={}",
                    PQerrorMessage(this->data_->conn.get()))}, 0);
                goto err;
            }
        }

        if (PQsetnonblocking(this->data_->conn.get(), 1)) {
            status = status_aborted("pq::nonblocking failed");
            goto err;
        }

        PQsetNoticeProcessor(
            this->data_->conn.get(), +[](void *, const char *) -> void{}, nullptr);

        this->data_->fd_ = PQsocket(this->data_->conn.get());

        while (true) {
            auto ret = PQconnectPoll(this->data_->conn.get());

            switch (ret) {
                case PGRES_POLLING_READING:
                    this->data_->fd_ = PQsocket(this->data_->conn.get());
                status = co_await async::read_ready(this->data_->fd_, token.sub());
                if (!status)
                    goto err;
                continue;
                case PGRES_POLLING_WRITING:
                    this->data_->fd_ = PQsocket(this->data_->conn.get());
                status = co_await async::write_ready(this->data_->fd_, token.sub());
                if (!status)
                    goto err;

                continue;
                case PGRES_POLLING_FAILED:
                    status = manapi::status_aborted("pq:polling failed");
                goto err;
                default:
                    break;
            }

            break;
        }

        this->data_->flags |= PSQL_DATA_FLAG_INIT;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "psql:check_conn failed", e.what());
        status = manapi::ev::status_internal("psql:check_conn failed", manapi::ev::ERR_UNKNOWN);
    }
err:
    co_return std::move(status);
}

manapi::status manapi::ext::pq::connection::check_conn_() const MANAPIHTTP_NOEXCEPT {
    if (this->data_ && this->data_->conn) {
        return status_ok();
    }

    return status_invalid_argument("pq:connection doesn't exist");
}

manapi::status_or<size_t> manapi::ext::pq::connection::esc_to_buff(std::string_view text, char *buff) MANAPIHTTP_NOEXCEPT {
    int err{0};
    auto const copied{
        PQescapeStringConn(this->data_->conn.get(), buff, text.data(), text.size(), &err)};
    if (err) {
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s failed and returned %d for %.*s", "PQescapeStringConn", err, text.size(), text.data());
        return status_invalid_argument("PQescapeStringConn failed");
    }
    return copied;
}

manapi::future<manapi::status> manapi::ext::pq::connection::flush(manapi::ctoken token) {
    int rhs = PQflush(this->data_->conn.get());

    if (rhs == -1) {
        co_return manapi::status_internal("pq:flush failed");
    }

    if (rhs == 0) {
        /* done */
        co_return manapi::status_ok();
    }

    int revents = ev::READ|ev::WRITE;

    while (true) {
        if (revents & ev::READ) {
            if (!PQconsumeInput(this->data_->conn.get())) {
                co_return manapi::status_internal ("pq:Consume input error");
            }
        }

        if (revents & ev::WRITE) {
            rhs = PQflush(this->data_->conn.get());
            if (rhs == -1) {
                co_return manapi::status_internal("pq:flush failed");
            }

            if (rhs == 0) {
                /* done */
                break;
            }
        }

        auto res = co_await async::custom_ready(ev::READ|ev::WRITE, PQsocket(this->data_->conn.get()), token.sub());

        if (!res) {
            if (res.code() == ERR_CANCELLED)
                co_return manapi::status_aborted("pq:timeout was reached");
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s:%s failed due to %.*s, %.*s", "pq", "io", res.message().size(), res.message().data(),
                res.sysmsg().size(), res.sysmsg().data());
            co_return manapi::status_internal(res.message());
        }

        revents = res.unwrap();
    }

    co_return manapi::status_ok();
}

manapi::future<manapi::ext::pq::status_or<manapi::ext::pq::result>> manapi::ext::pq::connection::generic_single_result_query(manapi::ctoken token) {
    pq::status status;

    status = co_await this->flush(token);

    if (!status)
        co_return std::move(status);

    auto result_res = co_await this->receive_result(token);

    if (!result_res)
        co_return pq::status{result_res.err()};

    auto second_result_res = co_await this->receive_result(token);

    if (second_result_res && second_result_res.unwrap()) {
        co_return pq::status{manapi::status_internal("pq:unexpected non-null result")};
    }

    auto result = result_res.unwrap();
    auto err = connection::result_status_to_error_code(result);

    auto sqlstate = result.sqlstate();

    if (err != error_code::RESULT_STATUS_OK) {
        co_return pq::status (manapi::ERR_INVALID_ARGUMENT, "pq:requst failed", sqlstate, PQerrorMessage(this->data_->conn.get()));
    }

    co_return std::move(result);
}

manapi::future<manapi::status_or<manapi::ext::pq::result>> manapi::ext::pq::connection::receive_result(manapi::ctoken token) {
    while (PQisBusy(this->data_->conn.get())) {
        if (!PQconsumeInput(this->data_->conn.get())) {
            co_return manapi::status_internal ("pq:consume input failed");
        }
        if (!PQisBusy(this->data_->conn.get())) {
            break;
        }
        this->data_->fd_ = PQsocket(this->data_->conn.get());
        auto res = co_await async::read_ready(this->data_->fd_, token.sub());
        if (!res) {
            if (res.code() == ERR_CANCELLED) {
                co_return manapi::status_internal ("pq:timeout was reached");
            }

            auto data = res.data();
            data.errnum(ERR_INTERNAL);
            co_return manapi::status (std::move(data));
        }
    }

    auto lnk = PQgetResult(this->data_->conn.get());
    if (!lnk)
        co_return manapi::status_internal("pq:empty result");
    auto res = pq::result {lnk};
    co_await this->receive_notifications();

    co_return std::move(res);
}

manapi::ext::pq::error_code manapi::ext::pq::connection::result_status_to_error_code(result &result) MANAPIHTTP_NOEXCEPT {
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

manapi::ext::pq::PGconn * manapi::ext::pq::connection::native_handle() const MANAPIHTTP_NOEXCEPT {
    auto const res = this->check_conn_();
    if (res)
        return this->data_->conn.get();
    return nullptr;
}

bool manapi::ext::pq::connection::connected() const MANAPIHTTP_NOEXCEPT {
    if (this->data_ && this->data_->conn)
        return (PQstatus(this->data_->conn.get()) == ConnStatusType::CONNECTION_OK);
    return false;
}

manapi::future<> manapi::ext::pq::connection::receive_notifications() {
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

manapi::ext::pq::status::status() {

}

manapi::ext::pq::status::~status()  = default;

manapi::ext::pq::status::status(manapi::err_num code, std::string_view msg, std::size_t sqlcode,
std::string sqlmsg) : manapi::status(code, msg) {
    this->m_sqlmsg = std::move(sqlmsg);
    this->m_sqlcode = sqlcode;
}

manapi::ext::pq::status::status(manapi::err_num code, std::string msg, std::size_t sqlcode, std::string sqlmsg) : manapi::status(code, std::move(msg)) {
    this->m_sqlmsg = std::move(sqlmsg);
    this->m_sqlcode = sqlcode;
}

manapi::ext::pq::status::status(manapi::err_num code, const char *msg, std::size_t sqlcode, std::string sqlmsg) : manapi::status(code, msg) {
    this->m_sqlmsg = std::move(sqlmsg);
    this->m_sqlcode = sqlcode;
}

manapi::ext::pq::status::status(status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_sqlcode = std::exchange(n.m_sqlcode, 0);
    this->m_sqlmsg = std::move(n.m_sqlmsg);
    manapi::status::operator=(std::forward<decltype(n)>(n));
}

manapi::ext::pq::status & manapi::ext::pq::status::operator=(status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_sqlcode = std::exchange(n.m_sqlcode, 0);
    this->m_sqlmsg = std::move(n.m_sqlmsg);
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;
}

manapi::ext::pq::status::status(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_sqlcode = 0;
    this->m_sqlmsg = "";
    manapi::status::operator=(std::forward<decltype(n)>(n));

}

manapi::ext::pq::status & manapi::ext::pq::status::operator=(manapi::status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_sqlcode = 0;
    this->m_sqlmsg = "";
    manapi::status::operator=(std::forward<decltype(n)>(n));
    return *this;

}

manapi::ext::pq::status::status(const status &n) : manapi::status(n) {
    this->m_sqlcode = 0;
}

std::string manapi::ext::pq::status::fullmsg() const {
    return std::format("{} sqlcode={} sqlmsg={}", manapi::status::fullmsg(), this->m_sqlcode, this->m_sqlmsg);
}

bool manapi::ext::pq::status::is_sqlerr() const MANAPIHTTP_NOEXCEPT {
    return !this->m_sqlmsg.empty();
}

std::string_view manapi::ext::pq::status::sqlmsg() const MANAPIHTTP_NOEXCEPT{
    return this->m_sqlmsg;
}

manapi::ext::pq::sql_states manapi::ext::pq::status::sqlcode() const MANAPIHTTP_NOEXCEPT {
    return static_cast<sql_states>(this->m_sqlcode);
}


#undef PSQL_POOL_FLAG_ACTIVE
#undef PSQL_DATA_FLAG_INIT
#undef PSQL_POOL_FLAG_STOP