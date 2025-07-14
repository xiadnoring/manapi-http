#pragma once

#include <mutex>

#include "ManapiSlice.hpp"
#include "ManapiBuffer.hpp"
#include "ManapiChain.hpp"

namespace manapi {
    namespace internal {
        struct object_pool_data_t;

        void object_item_pool_return (const std::shared_ptr<internal::object_pool_data_t> &data, void *buffer, std::size_t size);

        void object_item_pool_return (void *buffer, std::size_t size);
    }

    template<typename T>
    class object_item_pool {
    public:
        object_item_pool () : object(nullptr) {}

        object_item_pool (T *object)
            :  object(object) {}

        object_item_pool (object_item_pool &&n)  noexcept {
            this->object = std::move(n.object);
        }

        object_item_pool &operator=(object_item_pool &&n) noexcept {
            this->object = std::exchange(n.object, 0);
            return *this;
        }

        ~object_item_pool() {
            if (this->object) {
                internal::object_item_pool_return(this->object, sizeof (T));
            }
        }

        bool operator==(const nullptr_t &) const noexcept {
            return this->object == nullptr;
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

        T *release () {
            return std::exchange(this->object, nullptr);
        }
    private:
        T *object;
    };

    class object_pool {
        std::shared_ptr<internal::object_pool_data_t> data;
    public:
        typedef std::unique_ptr<manapi::slice> item;

        explicit object_pool();

        ~object_pool();

        //manapi::slice slice (std::size_t min, std::size_t max);

        manapi::slice slice (std::size_t suggested);

        manapi::bytebuffer buffer (std::size_t min, std::size_t max);

        manapi::bytebuffer buffer (std::size_t suggested);

        manapi::bytebuffer buffer (void *pointer, std::size_t suggested);

        void *alloc (std::size_t size) noexcept(true);

        void *realloc (void *ptr, std::size_t) noexcept(true);

        void free (void *ptr) noexcept(true);

        void free (void *ptr, std::size_t size) noexcept(true);

        void clear ();

        template<typename T, typename ...Args>
        std::enable_if<std::has_virtual_destructor_v<T>, manapi::error::status_or<object_item_pool<T>>> get (Args&&...args) {
            auto b = this->alloc (sizeof (T));
            try {
                auto const data = new(b) T (std::forward<decltype(args)>(args)...);
                return object_item_pool<T> (this->data, b, sizeof (T));
            }
            catch (std::exception const &e) {
                this->free(b, sizeof (T));
                manapi_log_error("%s due to %s", "object init failed", e.what());
                return manapi::error::status_internal("object init failed");
            }
        }

        template<typename T>
        std::enable_if<!std::is_same_v<T, void>> free (T *pointer) {
            return this->free(pointer, sizeof (T));
        }
    };
}
