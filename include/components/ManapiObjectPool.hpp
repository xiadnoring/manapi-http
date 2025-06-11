#pragma once
#include <mutex>

#include "Buffer.hpp"
#include "ManapiChain.hpp"

namespace manapi {
    namespace internal {
        struct object_pool_data_t;

        void object_item_pool_return (const std::shared_ptr<internal::object_pool_data_t> &, void *buffer, int real_size);
    }

    template<typename T>
    class object_item_pool {
    public:
        object_item_pool () : data(nullptr), object(nullptr) {}

        object_item_pool (std::shared_ptr<internal::object_pool_data_t> data, T *object, int real_size)
            : data(std::move(data)), object(object), real_size_(real_size) {}

        object_item_pool (object_item_pool &&n)  noexcept {
            this->object = std::move(n.object);
            this->data = std::move(n.data);
        }

        object_item_pool &operator=(object_item_pool &&n) noexcept {
            this->object = std::exchange(n.object, 0);
            this->data = std::move(n.data);
            this->real_size_ = std::exchange(n.real_size_, 0);
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
            return this->object;
        }

        T &operator*() {
            return *this->object;
        }

        std::pair<T *, int> release () {
            return {std::exchange(this->object, nullptr), std::exchange(this->real_size_, 0)};
        }

        ~object_item_pool() {
            if (this->object
                && this->data) {
                delete this->object;
                internal::object_item_pool_return (std::move(this->data),
                    std::exchange(this->object, nullptr), std::exchange(this->real_size_, 0));
            }
        }
    private:
        std::shared_ptr<internal::object_pool_data_t> data;
        T *object;
        int real_size_;
    };

    class object_pool {
        std::shared_ptr<internal::object_pool_data_t> data;
    public:
        typedef std::unique_ptr<bytebuffer> item;

        explicit object_pool();

        ~object_pool();

        bytebuffer slice (std::size_t min, std::size_t max);

        bytebuffer slice (std::size_t suggested);

        bytebuffer slice (void *pointer, std::size_t suggested);

        template<typename T, typename ...Args>
        std::enable_if<std::has_virtual_destructor_v<T>, object_item_pool<T>> get (Args&&...args) {
            auto b = this->slice (sizeof (T));
            auto const size = b.realsize();
            auto const data = new(b.release()) T (std::forward<decltype(args)>(args)...);
            return object_item_pool<T> (this->data, b.release(), size);
        }

        void unit (bytebuffer buffer);

        void object_item_pool_return (void *pointer, int size);
    };
}
