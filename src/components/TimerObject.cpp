#include "./components/TimerObject.hpp"
#include "async/ManapiAsyncContext.hpp"

manapi::timer::timer() {
    this->data = nullptr;
}

manapi::timer::timer(nullptr_t) {
    this->data = nullptr;
}

manapi::timer::timer(std::shared_ptr<timer_data_t> data) {
    this->data = std::move(data);
}

manapi::timer::timer(std::move_only_function<void(manapi::timer t)> sync_cb) {
    this->data = std::make_shared<timer_data_t>();
    this->data->sync_cb = std::make_unique<decltype(this->data->sync_cb)::element_type>([cb = std::move(sync_cb)] (const std::shared_ptr<timer_data_t> &data) mutable
            -> void { cb(timer{data}); });
}

manapi::timer::timer(std::move_only_function<manapi::future<>(manapi::timer t)> async_cb) {
    this->data = std::make_shared<timer_data_t>();
    this->data->async_cb = std::make_unique<decltype(this->data->async_cb)::element_type>([cb = std::move(async_cb)] (std::shared_ptr<timer_data_t> data) mutable
        -> manapi::future<> {
        try {
            co_await cb (timer{data});
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->debug(manapi::logger::default_service, "timer: User Callback Error: {}", e.what());
        }
        manapi::async::current()->timerpool()->update_interval_state(manapi::timer::id_(data));
    });
}

manapi::timer::timer(const timer &n) {
    this->data = n.data;
}

manapi::timer::timer(timer &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::timer & manapi::timer::operator=(timer &&n) noexcept {
    this->data = std::move(n.data);
    return *this;
}

manapi::timer & manapi::timer::operator=(const timer &n) {
    this->data = n.data;
    return *this;
}

manapi::timer & manapi::timer::operator=(nullptr_t) {
    this->data = nullptr;
    return *this;
}

manapi::timer::~timer() = default;

manapi::timer::operator bool() const {
    return this->data != nullptr;
}

size_t manapi::timer::id() const {
    return manapi::timer::id_(this->data);
}

void manapi::timer::call_() {
    if (!this->data->enabled) {
        return;
    }

    if (this->data->sync_cb) {
        try {
            this->data->sync_cb->operator()(this->data);
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->debug(manapi::logger::default_service, "timer: User Callback Error: {}", e.what());
        }
    }

    if (this->data->async_cb) {
        manapi::async::run (this->data->async_cb->operator()(this->data));
    }
}

void manapi::timer::clear_() {
    this->data->async_cb = {};
    this->data->sync_cb = {};
}

void manapi::timer::stop() {
    if (!this->data->enabled) {
        return;
    }

    this->data->enabled = false;

    manapi::async::current()->timerpool()->remove_timer(this->id());
    this->clear_();
}

bool manapi::timer::is_async() const {
    return !!this->data->async_cb;
}

bool manapi::timer::is_sync() const {
    return !!this->data->sync_cb;
}

bool manapi::timer::enabled() const {
    return this->data->enabled;
}


size_t manapi::timer::id_(const std::shared_ptr<timer_data_t> &data) {
    return reinterpret_cast<size_t>(data.get());
}





