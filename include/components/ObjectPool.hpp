#pragma once
#include <mutex>

#include "ManapiChain.hpp"

namespace manapi {
    template<typename T, typename ...Args>
    class object_pool;

    template<typename T, typename ...Args>
    class object_item_pool {
    public:
        struct data_t {
            std::tuple<Args...> args{};
            manapi::chain<std::unique_ptr<T>> objects{};
            std::mutex mx{};
        };

        object_item_pool () : data(nullptr), object(nullptr) {}

        object_item_pool (std::shared_ptr<data_t> data, std::unique_ptr<T> object)
            : data(std::move(data)), object(std::move(object)) {}

        object_item_pool (object_item_pool &&n)  noexcept {
            this->object = std::move(n.object);
            this->data = std::move(n.data);
        }

        object_item_pool &operator=(object_item_pool &&n) noexcept {
            this->object = std::move(n.object);
            this->data = std::move(n.data);
            return *this;
        }

        bool operator==(const nullptr_t &) const noexcept {
            return this->data == nullptr;
        }

        bool operator!=(const nullptr_t &) const noexcept {
            return !this->operator==(nullptr);
        }

        [[nodiscard]] operator bool () const noexcept {
            return this->operator!=(nullptr);
        }

        object_item_pool &operator=(nullptr_t);

        T *operator->() {
            return this->object.get();
        }

        T &operator*() {
            return *this->object.get();
        }

        ~object_item_pool();

    private:
        std::shared_ptr<data_t> data;
        std::unique_ptr<T> object;
    };

    template<typename T, typename ...Args>
    class object_pool {
    public:
        typedef std::unique_ptr<T> item;

        explicit object_pool() : data(nullptr) {}
        ~object_pool() = default;

        void init (Args &&...args) {
            this->data = std::make_shared<typename object_item_pool<T, Args...>::data_t>(std::tuple<Args...> (args...));
        }

        void reserve (const std::size_t &size) {
            constexpr size_t n = std::tuple_size_v<std::tuple<Args...>>;

            while (this->data->objects.size() < size) {
                this->data->objects.push_back(this->make_unique_(this->data->args, std::make_index_sequence<n>{}));
            }
        }

        object_item_pool<T, Args...> get () {
            std::unique_lock <std::mutex> lk (this->data->mx);

            if (this->data->objects.empty()) {
                lk.unlock();

                constexpr size_t n = std::tuple_size_v<std::tuple<Args...>>;
                return object_item_pool<T, Args...>{this->data, this->make_unique_(this->data->args, std::make_index_sequence<n>{})};
            }
            auto w = std::move(this->data->objects.back());
            this->data->objects.pop_back();
            lk.unlock();

            return object_item_pool<T, Args...>{this->data, std::move(w)};
        }

        void ret (std::unique_ptr<T> item) {
            std::lock_guard<std::mutex> lk (this->data->mx);
            this->data->objects.push_back(std::move(item));
        }

        static void internal_ret (std::shared_ptr<typename object_item_pool<T, Args...>::data_t> data, std::unique_ptr<T> item) {
            item->reinit();
            std::lock_guard<std::mutex> lk (data->mx);
            data->objects.push_back(std::move(item));
        }
    private:

        template <size_t... Idx>
        std::unique_ptr<T> make_unique_ (std::tuple<Args...> tuple, std::index_sequence<Idx...>) {
            return std::make_unique<T>(std::get<Idx>(tuple)...);
        }
        std::shared_ptr<typename object_item_pool<T, Args...>::data_t> data{nullptr};
    };

    template<typename T, typename ... Args>
    manapi::object_item_pool<T, Args...> & manapi::object_item_pool<T, Args...>::operator=(nullptr_t) {
        if (this->object && this->data) {
            object_pool<T, Args...>::internal_ret(std::move(this->data), std::move(this->object));
        }
        return *this;
    }

    template<typename T, typename ... Args>
    manapi::object_item_pool<T, Args...>::~object_item_pool() {
        if (this->object && this->data) {
            object_pool<T, Args...>::internal_ret(std::move(this->data), std::move(this->object));
        }
    }
}
