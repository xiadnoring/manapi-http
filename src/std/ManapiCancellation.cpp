#include <memory>

#include "ManapiTimerObject.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiThreadPool.hpp"
#include "std/ManapiCancellation.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "../include/ManapiUtils.hpp"

enum status_flags {
    FLAG_CANCEL = 0x1,
    FLAG_DISABLED = 0x2,
    FLAG_ASK_CANCEL = 0x4,
};

struct manapi::async::cancellation_action::data_t {
    int status_;
    size_t timeout_; /* ms */
    manapi::timer timeout_struct_;
    std::move_only_function<void()> cancel_sync_callback_;
    std::unique_ptr<manapi::chain<cancellation_action>> unites;
    manapi::chain<cancellation_action>::iterator it;
    cancellation_action *parent;
};

manapi::async::cancellation_action::cancellation_action(std::nullptr_t) {
    this->data = nullptr;
}

manapi::async::cancellation_action::cancellation_action() {
    this->data = std::make_shared<data_t>(0, 0UL, manapi::timer{nullptr},
            nullptr, nullptr, nullptr, nullptr);
}

manapi::async::cancellation_action manapi::async::cancellation_action::unit(cancellation_action cancellation) {
    if (cancellation) {
        cancellation_action n;
        n.ask_cancel_callback();
        n.cancel_callback(std::move(cancellation));
        return std::move(n);
    }

    return {};
}

manapi::async::cancellation_action manapi::async::cancellation_action::sub() const {
    return manapi::async::cancellation_action::unit(*this);
}

manapi::async::cancellation_action::cancellation_action(cancellation_action &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
}

manapi::async::cancellation_action & manapi::async::cancellation_action::operator=(cancellation_action &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
    return *this;
}

manapi::async::cancellation_action::cancellation_action(const cancellation_action &n) {
    this->data = n.data;
}

manapi::async::cancellation_action & manapi::async::cancellation_action::operator=(const cancellation_action &n) {
    this->data = n.data;
    return *this;
}

manapi::async::cancellation_action::~cancellation_action() {
    if (this->data
        && this->data.use_count() == 1) {
        if (this->data->parent) {
            assert(this->data->it);

            this->data->parent->data->unites->erase(this->data->it);
            this->data->it = nullptr;
            this->data->parent = nullptr;
        }

        if (this->data->unites) {
            while (!this->data->unites->empty()) {
                auto it = std::move(this->data->unites->back());
                this->data->unites->pop_back();

                it.data->it = nullptr;
                it.data->parent = nullptr;
            }
        }
        this->data.reset();
    }
}

manapi::async::cancellation_action::operator bool() const {
    return this->data != nullptr;
}

manapi::async::cancellation_action &manapi::async::cancellation_action::operator=(nullptr_t) {
    this->data = nullptr;
    return *this;
}

void manapi::async::cancellation_action::reset() {
    this->data = std::make_shared<data_t>(0, 0UL, manapi::timer{nullptr},
            nullptr, nullptr, nullptr, nullptr);
}

void manapi::async::cancellation_action::cancel_callback (std::move_only_function<void()> callback) MANAPIHTTP_NOEXCEPT {
    if (this->data) {
        this->data->cancel_sync_callback_ = (std::move(callback));
    }
}

void manapi::async::cancellation_action::cancel_callback(cancellation_action cancellation) {
    if (this->data && cancellation) {
        if (this->data->parent) {
            assert(this->data);
            this->data->parent->data->unites->erase(this->data->it);
            this->data->parent = nullptr;
            this->data->it = nullptr;
        }

        if (cancellation.data->status_ & FLAG_CANCEL) {
            /* already! BOOM! */
            this->ask_cancel_callback();
            this->data->status_ |= FLAG_CANCEL;
        }
        else {
            if (!cancellation.data->unites) {
                cancellation.data->unites = std::make_unique<decltype(cancellation.data->unites)::element_type>();
            }

            this->ask_cancel_callback();

            cancellation.data->unites->push_back((*this));
            this->data->parent = this;
            this->data->it = cancellation.data->unites->rbegin();
        }
    }
}

void manapi::async::cancellation_action::cancel() MANAPIHTTP_NOEXCEPT {
    if (this->data) {
        if ((this->data->status_ & (FLAG_CANCEL|FLAG_DISABLED))) {
            return;
        }

        this->data->status_ |= FLAG_CANCEL;

        this->send_async_();
    }
}

void manapi::async::cancellation_action::ask_cancel_callback() MANAPIHTTP_NOEXCEPT {
    if (this->data) {
        this->data->status_ |= FLAG_ASK_CANCEL;
    }
}

manapi::error::status manapi::async::cancellation_action::timeout(size_t timeout) MANAPIHTTP_NOEXCEPT {
    if (this->data) {
        this->data->timeout_ = timeout;

        if (this->data->timeout_ > 0) {
            try {
                auto rhs = manapi::async::current()->timerpool()
                    ->append_timer_sync(this->timeout(), [data = this->data] (const manapi::timer& t) mutable
                    -> void {
                        if ((data->status_ & (FLAG_CANCEL|FLAG_DISABLED))) {
                            return;
                        }

                        data->status_ |= FLAG_CANCEL;

                        cancellation_action::cancel_(std::move(data));
                });

                if (!rhs.ok()) {
                    this->data->timeout_ = 0;
                    return rhs.err();
                }

                this->data->status_ |= FLAG_ASK_CANCEL;
                this->data->timeout_struct_ = rhs.unwrap();

            }
            catch (std::bad_alloc const &) {
                return error::status_resource_exhausted();
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                return error::status_internal();
            }
        }
    }

    return error::status_ok();
}

bool manapi::async::cancellation_action::contains_cancel_callback() const {
    return this->data && (this->data->status_ & FLAG_ASK_CANCEL);
}

size_t manapi::async::cancellation_action::timeout() const {
    return this->data ? this->data->timeout_ : 0;
}

void manapi::async::cancellation_action::disable() {
    if (this->data) {
        this->data->status_ |= (FLAG_DISABLED);

        if (data->timeout_struct_) {
            data->timeout_struct_.stop();
            data->timeout_struct_ = nullptr;
        }
    }
}

void manapi::async::cancellation_action::send_async_() MANAPIHTTP_NOEXCEPT {
    if (this->data) {
        if (this->data->status_ & FLAG_CANCEL) {
            cancellation_action::cancel_((this->data));
        }
    }
}

void manapi::async::cancellation_action::stop_timeout_(std::shared_ptr<data_t> data) MANAPIHTTP_NOEXCEPT {
    if (data->timeout_struct_) {
        data->timeout_struct_.stop();
        data->timeout_struct_ = nullptr;
    }
}

void manapi::async::cancellation_action::cancel_(std::shared_ptr<data_t> data) MANAPIHTTP_NOEXCEPT {
    cancellation_action::stop_timeout_(data);

    if (data->cancel_sync_callback_) {
        auto cb = std::move(data->cancel_sync_callback_);
        data->cancel_sync_callback_=nullptr;
        if (!(data->status_ & FLAG_DISABLED)) {
            std::move_only_function<void()> callback;

            MANAPIHTTP_MUST_ALLOC_START
            callback = [cb = std::move(cb)] () mutable -> void {
                try {
                    cb();
                }
                catch (manapi::exception const &e) {
                    manapi_log_error("%s failed due to %d:%s",
                        "cancellation", static_cast<int>(e.err_num()), e.what());
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s failed due to %s",
                        "cancellation", e.what());
                }
            };
            MANAPIHTTP_MUST_ALLOC_END

            manapi::async::current()->etaskpool()->append_task(std::move(callback));
        }
    }

    if (data->unites) {
        while (!data->unites->empty()) {
            auto it = std::move(data->unites->back());
            data->unites->pop_back();

            it.data->parent = nullptr;
            it.data->it = nullptr;

            it.cancel();
        }
    }
}
