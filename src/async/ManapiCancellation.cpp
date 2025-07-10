#include "async/ManapiCancellation.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "../include/ManapiUtils.hpp"

#include <memory>

enum status_flags {
    FLAG_CANCEL = 0x1,
    FLAG_DISABLED = 0x4,
    FLAG_ASK_CANCEL = 0x8,
};

struct manapi::async::cancellation_action::data_t {
    int status_;
    size_t timeout_; /* ms */
    manapi::timer timeout_struct_;
    std::unique_ptr<std::move_only_function<void()>> cancel_sync_callback_;
    std::unique_ptr<manapi::chain<cancellation_action>> unites;
    manapi::chain<cancellation_action>::iterator it;
    cancellation_action *parent;
};

manapi::async::cancellation_action::cancellation_action(nullptr_t) {
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
        cancellation.cancel_callback(n);
        return std::move(n);
    }

    return {};
}

manapi::async::cancellation_action manapi::async::cancellation_action::sub() const {
    return manapi::async::cancellation_action::unit(*this);
}

manapi::async::cancellation_action::cancellation_action(cancellation_action &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::async::cancellation_action & manapi::async::cancellation_action::operator=(cancellation_action &&n) noexcept {
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

void manapi::async::cancellation_action::cancel_callback (std::move_only_function<void()> callback) {
    if (this->data) {
        this->data->cancel_sync_callback_ = std::make_unique<decltype(callback)>(std::move(callback));
    }
}

void manapi::async::cancellation_action::cancel_callback(cancellation_action cancellation) {
    if (this->data && cancellation) {
        if (cancellation.data->parent) {
            assert(cancellation.data);
            cancellation.data->parent->data->unites->erase(cancellation.data->it);
            cancellation.data->parent = nullptr;
            cancellation.data->it = nullptr;
        }

        if (this->data->status_ & FLAG_CANCEL) {
            /* already! BOOM! */
            cancellation.ask_cancel_callback();
            cancellation.data->status_ |= FLAG_CANCEL;
        }
        else {
            if (!this->data->unites) {
                this->data->unites = std::make_unique<decltype(this->data->unites)::element_type>();
            }

            cancellation.ask_cancel_callback();

            cancellation.data->parent = this;
            cancellation.data->it = this->data->unites->rbegin();
            this->data->unites->push_back((cancellation));
        }
    }
}

void manapi::async::cancellation_action::cancel() {
    if (this->data) {
        if ((this->data->status_ & (FLAG_CANCEL|FLAG_DISABLED))) {
            return;
        }

        this->data->status_ |= FLAG_CANCEL;

        this->send_async_();
    }
}

void manapi::async::cancellation_action::ask_cancel_callback() {
    if (this->data) {
        this->data->status_ |= FLAG_ASK_CANCEL;
    }
}

void manapi::async::cancellation_action::timeout(size_t timeout) {
    if (this->data) {
        this->data->timeout_ = timeout;
        this->data->status_ |= FLAG_ASK_CANCEL;

        if (this->data->timeout_ > 0) {
            this->data->timeout_struct_ = manapi::async::current()->timerpool()
                ->append_timer_sync(this->timeout(), [data = this->data] (manapi::timer t) mutable
                -> void {
                    if ((data->status_ & (FLAG_CANCEL|FLAG_DISABLED))) {
                        return;
                    }

                    data->status_ |= FLAG_CANCEL;

                    cancellation_action::cancel_(std::move(data));
            });
        }
    }
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
    }
}

void manapi::async::cancellation_action::send_async_() {
    if (this->data) {
        if (this->data->status_ & FLAG_CANCEL) {
            cancellation_action::cancel_(std::move(this->data));
        }
    }
}

void manapi::async::cancellation_action::stop_timeout_(std::shared_ptr<data_t> data) {
    if (data->timeout_struct_) {
        data->timeout_struct_.stop();
        data->timeout_struct_ = nullptr;
    }
}

void manapi::async::cancellation_action::cancel_(std::shared_ptr<data_t> data) {
    cancellation_action::stop_timeout_(data);

    if (data->cancel_sync_callback_) {
        auto cb = std::move(*data->cancel_sync_callback_);
        data->cancel_sync_callback_.reset();
        if (!(data->status_ & FLAG_DISABLED)) {
            manapi::async::current()->etaskpool()->append_task(
                [cb = std::move(cb)] () mutable -> void {
                try {
                    cb();
                }
                catch (manapi::exception const &e) {
                    MANAPIHTTP_LOG("cancellation failed by error({}): {}", static_cast<int>(e.err_num()), e.what());
                }
                catch (std::exception const &e) {
                    MANAPIHTTP_LOG("cancellation failed by error {}", e.what());
                }
            });
        }
    }

    if (data->unites) {
        while (!data->unites->empty()) {
            auto it = std::move(data->unites->back());
            data->unites->pop_back();

            it.data->parent = nullptr;

            try {
                it.cancel();
            }
            catch (manapi::exception const &e) {
                MANAPIHTTP_LOG("cancellation failed by error({}): {}", static_cast<int>(e.err_num()), e.what());
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("cancellation failed by error {}", e.what());
            }
        }
    }
}
