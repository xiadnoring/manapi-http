#include "async/ManapiCancellation.hpp"

#include <memory>

enum status_flags {
    FLAG_CANCEL = 0x1,
    FLAG_READY = 0x2,
    FLAG_DISABLED = 0x4
};

enum ask_flags {
    FLAG_ASK_CANCEL = 0x1,
    FLAG_ASK_TIMEOUT = 0x2
};

manapi::async::cancellation_action::cancellation_action() {
    this->data = nullptr;
}

manapi::async::cancellation_action::cancellation_action(nullptr_t) {
    this->data = nullptr;
}

manapi::async::cancellation_action::cancellation_action(std::shared_ptr<async::context> ctx) {
    if (ctx) {
        this->data = std::make_shared<data_t>(0, 0, 0,
            nullptr, nullptr, nullptr, ctx);
    }
    else {
        this->data = nullptr;
    }
}

manapi::async::cancellation_action::cancellation_action(std::shared_ptr<async::context> ctx, cancellation_action cancellation) {
    if (cancellation) {
        cancellation_action(std::move(ctx));
        this->ask_cancel_callback();
        this->cancel_callback(std::move(cancellation));
    }
    else {
        cancellation_action();
    }
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

manapi::async::cancellation_action::~cancellation_action() = default;

manapi::async::cancellation_action::operator bool() const {
    return this->data != nullptr;
}

manapi::async::cancellation_action &manapi::async::cancellation_action::operator=(nullptr_t) {
    this->data = nullptr;
    return *this;
}

void manapi::async::cancellation_action::reset(std::shared_ptr<async::context> ctx) {
    this->data = std::make_shared<data_t>(0, 0, 0, nullptr,
        nullptr, nullptr, ctx);
}

void manapi::async::cancellation_action::handle_ready(std::move_only_function<void()> callback) {
    if (this->data) {
        this->data->ready_callback_ = std::make_unique<std::move_only_function<void()>>(std::move(callback));
    }
}

void manapi::async::cancellation_action::cancel_callback (std::move_only_function<void(std::shared_ptr<async::context> ctx)> callback) {
    if (this->data) {
        if (this->data->want_to_unit) {
            auto cancellation = std::move(*this->data->want_to_unit);
            this->data->want_to_unit.reset();

            if (cancellation.data->status_ & FLAG_CANCEL) {
                this->cancel();
            }
            else {
                if (!cancellation.data->associated) {
                    cancellation.data->associated = std::make_unique<std::vector<cancellation_action>>();
                }

                cancellation.data->associated->push_back(*this);
            }
        }

        this->data->cancel_sync_callback_ = std::make_unique<decltype(callback)>(std::move(callback));

        if (this->data->ask & (FLAG_ASK_CANCEL|FLAG_ASK_TIMEOUT)) {
            this->data->watcher = this->data->ctx->eventloop()->create_watcher_async([data = this->data] (std::shared_ptr<ev::async> &w) mutable
                -> void {
                if (data->status_ & FLAG_CANCEL) {
                    cancellation_action::cancel_(std::move(data));
                }
            });
        }

        if (this->data->ask & (FLAG_ASK_TIMEOUT)) {
            this->data->timeout_struct_ = this->data->ctx->timerpool()->append_timer_sync(this->timeout(), [data = this->data] (manapi::timer t) mutable
                -> void { cancellation_action::cancel_(std::move(data)); });
        }

        this->ready();
    }
}

void manapi::async::cancellation_action::cancel_callback(cancellation_action cancellation) {
    if (this->data && cancellation) {
        this->ask_cancel_callback();
        if (this->data->want_to_unit) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_CANCELLATION_FAILED, "Already want to unit");
        }

        this->data->want_to_unit = std::make_unique<cancellation_action>(std::move(cancellation));
    }
}

void manapi::async::cancellation_action::cancel() {
    if (this->data) {
        if ((this->data->status_.fetch_or(FLAG_CANCEL) & FLAG_CANCEL)) {
            return;
        }

        this->send_async_();
    }
}

void manapi::async::cancellation_action::ready() {
    if (this->data) {
        if ((this->data->status_.fetch_or(FLAG_READY) & FLAG_READY)) {
            return;
        }

        if (this->data->ready_callback_) {
            this->data->ready_callback_->operator()();
            this->data->ready_callback_.reset();
        }

        if (this->data->status_ & (FLAG_CANCEL)) {
            this->cancel();
        }
    }
}

void manapi::async::cancellation_action::ask_cancel_callback() {
    if (this->data) {
        this->data->ask |= (FLAG_ASK_CANCEL|FLAG_ASK_TIMEOUT);
    }
}

void manapi::async::cancellation_action::timeout(ssize_t timeout) {
    if (this->data) {
        this->data->ask |= FLAG_ASK_TIMEOUT;
        this->data->timeout_ = timeout;
    }
}

bool manapi::async::cancellation_action::contains_cancel_callback() const {
    return this->data && (this->data->ask & FLAG_ASK_CANCEL);
}

bool manapi::async::cancellation_action::contains_timeout() const {
    return this->data && this->timeout() > 0 && (this->data->ask & FLAG_ASK_TIMEOUT);
}

ssize_t manapi::async::cancellation_action::timeout() const {
    return this->data ? this->data->timeout_ : 0;
}

void manapi::async::cancellation_action::disable_cancellation() {
    if (this->data) {
        this->data->status_.fetch_or(FLAG_DISABLED);
    }
}

void manapi::async::cancellation_action::send_async_() {
    if (this->data && this->data->status_ & (FLAG_READY)) {
        if (this->data->watcher->send()) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_CANCELLATION_FAILED, "failed to send signal");
        }
    }
}

void manapi::async::cancellation_action::stop_timeout_(std::shared_ptr<data_t> data) {
    if (data->timeout_struct_) {
        data->timeout_struct_.sync_stop(data->ctx);
        data->timeout_struct_ = nullptr;
    }
}

void manapi::async::cancellation_action::cancel_(std::shared_ptr<data_t> data) {
    cancellation_action::stop_timeout_(data);

    if (data->watcher) {
        data->ctx->eventloop()->stop_watcher(std::move(data->watcher));
    }

    if (data->cancel_sync_callback_) {
        auto cb = std::move(*data->cancel_sync_callback_);
        data->cancel_sync_callback_.reset();
        if (!(data->status_ & FLAG_DISABLED)) {
            cb(data->ctx);
        }
    }

    if (data->associated) {
        while (!data->associated->empty()) {
            auto it = std::move(data->associated->back());
            data->associated->pop_back();

            try {
                it.cancel();
            }
            catch (manapi::exception const &e) {
                MANAPIHTTP_LOG(data->ctx, "cancellation failed by error({}): {}", static_cast<int>(e.err_num()), e.what());
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG(data->ctx, "cancellation failed by error {}", e.what());
            }
        }
        data->associated.reset();
    }
}
