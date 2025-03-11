#include "async/ManapiCancellation.hpp"

enum status_flags {
    FLAG_CANCEL = 0x1,
    FLAG_READY = 0x2
};

enum ask_flags {
    FLAG_ASK_CANCEL = 0x1,
    FLAG_ASK_TIMEOUT = 0x2
};

manapi::async::cancellation_action::cancellation_action() {
    this->data = nullptr;
}

manapi::async::cancellation_action::cancellation_action(std::shared_ptr<async::context> ctx) {
    this->data = std::make_shared<data_t>(0, 0, 0, nullptr, nullptr, nullptr, std::move(ctx));
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
    this->data = std::make_shared<data_t>(0, 0, 0, nullptr, nullptr, nullptr, std::move(ctx));
}

void manapi::async::cancellation_action::handle_ready(std::move_only_function<void()> callback) {
    this->data->ready_callback_ = std::make_unique<std::move_only_function<void()>>(std::move(callback));
}

void manapi::async::cancellation_action::set_cancel_callback(std::move_only_function<manapi::future<>()> callback) {
    this->data->cancel_callback_ = std::make_unique<std::move_only_function<manapi::future<>()>>(std::move(callback));
}

manapi::future<> manapi::async::cancellation_action::cancel() {
    if ((this->data->status_.fetch_or(FLAG_CANCEL) & FLAG_CANCEL)) {
        return async::blank_future();
    }
    return cancellation_action::cancel_(this->data);
}

void manapi::async::cancellation_action::sync_cancel() {
    manapi::async::run (this->data->ctx, this->cancel());
}

void manapi::async::cancellation_action::ready() {
    if ((this->data->status_.fetch_or(FLAG_READY) & FLAG_READY)) {
        return;
    }

    if (this->data->ready_callback_) {
        this->data->ready_callback_->operator()();
        this->data->ready_callback_.reset();
    }
}

void manapi::async::cancellation_action::ask_cancel_callback() {
    this->data->ask |= FLAG_ASK_CANCEL;
}

void manapi::async::cancellation_action::ask_timeout() {
    this->data->ask |= FLAG_ASK_TIMEOUT;
}

void manapi::async::cancellation_action::timeout(ssize_t timeout) {
    this->data->timeout_ = timeout;
}

bool manapi::async::cancellation_action::contains_cancel_callback() const {
    return (this->data->ask & FLAG_ASK_CANCEL);
}

bool manapi::async::cancellation_action::contains_timeout() const {
    return this->timeout() > 0 && (this->data->ask & FLAG_ASK_TIMEOUT);
}

ssize_t manapi::async::cancellation_action::timeout() const {
    return this->data->timeout_;
}

void manapi::async::cancellation_action::timeout_struct(manapi::timer timeout_struct_) {
    this->data->timeout_struct_ = std::move(timeout_struct_);
}

manapi::timer manapi::async::cancellation_action::timeout_struct() {
    return this->data->timeout_struct_;
}

void manapi::async::cancellation_action::timeout_received() {
    if ((this->data->status_.fetch_or(FLAG_CANCEL) & FLAG_CANCEL)) {
        return;
    }

    this->data->timeout_struct_ = nullptr;
    manapi::async::run(this->data->ctx, cancellation_action::cancel_(this->data));
}

void manapi::async::cancellation_action::disable_cancellation() {
    if ((this->data->status_.fetch_or(FLAG_CANCEL) & FLAG_CANCEL)) {
        return;
    }

    if (this->data->timeout_struct_) {
        manapi::async::run (this->data->ctx, stop_timeout_ (this->data));
    }

    this->data->cancel_callback_.reset();
}

manapi::future<> manapi::async::cancellation_action::stop_timeout_(std::shared_ptr<data_t> data) {
    if (!data->timeout_struct_) {
        co_return;
    }

    co_await data->timeout_struct_.async_stop(data->ctx);
    data->timeout_struct_ = nullptr;
}

manapi::future<> manapi::async::cancellation_action::cancel_(std::shared_ptr<data_t> data) {
    if (data->timeout_struct_) {
        manapi::async::run (data->ctx, stop_timeout_(data));
    }

    if (data->cancel_callback_) {
        co_await data->cancel_callback_->operator()();
        data->cancel_callback_.reset();
    }
}
