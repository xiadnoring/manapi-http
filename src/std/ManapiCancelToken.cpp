#include <memory>

#include "ManapiTimerObject.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiThreadPool.hpp"
#include "std/ManapiCancelToken.hpp"
#include "std/ManapiContext.hpp"
#include "../include/ManapiUtils.hpp"

enum manapi__ctoken_status_flags {
    MANAPI__CTOKEN_FLAG_CANCEL = 0x1,
    MANAPI__CTOKEN_FLAG_DISABLED = 0x2,
    MANAPI__CTOKEN_FLAG_ASK_CANCEL = 0x4,
};

struct manapi::ctoken_data_t {
    int status;
    manapi::timer timeout_struct;
    std::move_only_function<void()> cancel_sync_callback;
    manapi::chain<ctoken> unites;
    manapi::chain<ctoken>::iterator it = nullptr;
    ctoken_data_t *parent;

    ~ctoken_data_t () {
        if (this->cancel_sync_callback) manapi::async::etaskpool()->release_tasks( manapi::TASK_TYPE_FUNC, 1 );
    }
};

static void manapi__ctoken_stop_timeout( std::shared_ptr<manapi::ctoken_data_t> m_data) MANAPIHTTP_NOEXCEPT {
    if (m_data->timeout_struct) {
        m_data->timeout_struct.stop();
        m_data->timeout_struct = nullptr;
    }
}

static void manapi__ctoken_cancel( std::shared_ptr<manapi::ctoken_data_t> m_data) MANAPIHTTP_NOEXCEPT {
    manapi__ctoken_stop_timeout(m_data);

    if (m_data->cancel_sync_callback) {
        auto cb = std::move(m_data->cancel_sync_callback);
        m_data->cancel_sync_callback=nullptr;
        manapi::async::etaskpool()->release_tasks( manapi::TASK_TYPE_FUNC, 1 );
        if (!(m_data->status & MANAPI__CTOKEN_FLAG_DISABLED)) {
            manapi::async::etaskpool()->append_task(std::move(cb));
        }
    }

    while (!m_data->unites.empty()) {
        auto it = std::move(m_data->unites.back());
        m_data->unites.pop_back();

        auto &data = it.data();
        data->parent = nullptr;
        data->it = nullptr;

        it.cancel();
    }

}

static void manapi__ctoken_send_async(const std::shared_ptr<manapi::ctoken_data_t>& m_data) MANAPIHTTP_NOEXCEPT {
    if (m_data) {
        if (m_data->status & MANAPI__CTOKEN_FLAG_CANCEL) {
            manapi__ctoken_cancel((m_data));
        }
    }
}


manapi::ctoken::ctoken(std::nullptr_t) {
    this->m_data = nullptr;
}

manapi::ctoken::ctoken() {
    this->m_data = std::make_shared<ctoken_data_t>();
}

manapi::ctoken manapi::ctoken::unit(ctoken cancellation) {
    if (cancellation) {
        ctoken n;
        n.ask_cancel_callback();
        n.cancel_callback(std::move(cancellation));
        return std::move(n);
    }

    return {};
}

manapi::ctoken manapi::ctoken::sub() const {
    return manapi::ctoken::unit(*this);
}

manapi::ctoken::ctoken(ctoken &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
}

manapi::ctoken & manapi::ctoken::operator=(ctoken &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
    return *this;
}

manapi::ctoken::ctoken(const ctoken &n) {
    this->m_data = n.m_data;
}

manapi::ctoken & manapi::ctoken::operator=(const ctoken &n) {
    this->m_data = n.m_data;
    return *this;
}

manapi::ctoken::~ctoken() {
    if (this->m_data) {
        std::size_t usage = 1;
        if (this->m_data->parent)
            usage++;

        assert(static_cast<std::size_t>(this->m_data.use_count()) >= usage);
        if (static_cast<std::size_t>(this->m_data.use_count()) == usage) {
            if (this->m_data->parent) {
                assert(this->m_data->it);

                this->m_data->parent->unites.erase(this->m_data->it);
                this->m_data->it = nullptr;
                this->m_data->parent = nullptr;
            }

            while (!this->m_data->unites.empty()) {
                auto it = std::move(this->m_data->unites.back());
                this->m_data->unites.pop_back();

                it.m_data->it = nullptr;
                it.m_data->parent = nullptr;
            }

            this->m_data->timeout_struct.stop();
            this->m_data.reset();
        }
    }
}

manapi::ctoken::operator bool() const {
    return this->m_data != nullptr;
}

manapi::ctoken &manapi::ctoken::operator=(nullptr_t) {
    this->m_data = nullptr;
    return *this;
}

void manapi::ctoken::reset() {
    this->m_data = std::make_shared<ctoken_data_t>();
}

void manapi::ctoken::cancel_callback (std::move_only_function<void()> callback) MANAPIHTTP_NOEXCEPT {
    if (this->m_data) {
        if (!this->m_data->cancel_sync_callback) manapi::async::etaskpool()->reserve_tasks( manapi::TASK_TYPE_FUNC, 1 );
        this->m_data->cancel_sync_callback = (std::move(callback));
    }
}

void manapi::ctoken::cancel_callback(ctoken cancellation) {
    if (this->m_data && cancellation) {
        if (this->m_data->parent) {
            assert(this->m_data);
            this->m_data->parent->unites.erase(this->m_data->it);
            this->m_data->parent = nullptr;
            this->m_data->it = nullptr;
        }

        if (cancellation.m_data->status & MANAPI__CTOKEN_FLAG_CANCEL) {
            /* already! BOOM! */
            this->ask_cancel_callback();
            this->m_data->status |= MANAPI__CTOKEN_FLAG_CANCEL;
        }
        else {

            this->ask_cancel_callback();

            cancellation.m_data->unites.push_back((*this));
            this->m_data->parent = cancellation.m_data.get();
            this->m_data->it = cancellation.m_data->unites.rbegin();
        }
    }
}

void manapi::ctoken::cancel() MANAPIHTTP_NOEXCEPT {
    if (this->m_data) {
        if ((this->m_data->status & (MANAPI__CTOKEN_FLAG_CANCEL|MANAPI__CTOKEN_FLAG_DISABLED))) {
            return;
        }

        this->m_data->status |= MANAPI__CTOKEN_FLAG_CANCEL;

        manapi__ctoken_send_async(this->m_data);
    }
}

void manapi::ctoken::ask_cancel_callback() MANAPIHTTP_NOEXCEPT {
    if (this->m_data) {
        this->m_data->status |= MANAPI__CTOKEN_FLAG_ASK_CANCEL;
    }
}

bool manapi::ctoken::is_cancelled() const MANAPIHTTP_NOEXCEPT {
    return this->m_data && (this->m_data->status & MANAPI__CTOKEN_FLAG_CANCEL);
}

manapi::status manapi::ctoken::timeout(size_t timeout) MANAPIHTTP_NOEXCEPT {
    if (this->m_data) {
        if (timeout > 0) {
            try {
                auto handler = manapi::async::current()->timerpool()
                    ->append_timer_sync(timeout, [wdata = std::weak_ptr (this->m_data)] (const manapi::timer& t) mutable
                    -> void {
                        auto data = wdata.lock();
                        assert ( !!data );

                        if ((data->status & (MANAPI__CTOKEN_FLAG_CANCEL|MANAPI__CTOKEN_FLAG_DISABLED))) {
                            return;
                        }

                        data->status |= MANAPI__CTOKEN_FLAG_CANCEL;

                        manapi__ctoken_cancel(std::move(data));
                }).unwrap();

                if (this->m_data->timeout_struct)
                    this->m_data->timeout_struct.stop();

                this->m_data->timeout_struct = std::move(handler);
                this->m_data->status |= MANAPI__CTOKEN_FLAG_ASK_CANCEL;
            }
            catch (std::bad_alloc const &) {
                return status_resource_exhausted();
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                return status_unknown();
            }
        }
    }

    return status_ok();
}

manapi::ctoken & manapi::ctoken::tm(size_t timeout) {
    this->timeout(timeout).unwrap();
    return *this;
}

bool manapi::ctoken::contains_cancel_callback() const {
    return this->m_data && (this->m_data->status & MANAPI__CTOKEN_FLAG_ASK_CANCEL);
}

void manapi::ctoken::disable() {
    if (this->m_data) {
        this->m_data->status |= (MANAPI__CTOKEN_FLAG_DISABLED);

        if (this->m_data->timeout_struct) {
            this->m_data->timeout_struct.stop();
            this->m_data->timeout_struct = nullptr;
        }
    }
}

const std::shared_ptr<manapi::ctoken_data_t> &manapi::ctoken::data() const MANAPIHTTP_NOEXCEPT {
    return this->m_data;
}