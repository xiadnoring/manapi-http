#pragma once

#include <mutex>

#include "./ManapiErrors.hpp"
#include "./ManapiAsync.hpp"
#include "./std/ManapiSlice.hpp"
#include "./std/ManapiBuffer.hpp"
#include "./std/ManapiChain.hpp"

namespace manapi {
    struct object_pool_data_t;

    class object_item_pool_root {
    public:
        object_item_pool_root ();

        object_item_pool_root (void *p);

        virtual ~object_item_pool_root ();

        object_item_pool_root (object_item_pool_root &&n) MANAPIHTTP_NOEXCEPT;

        object_item_pool_root& operator= (object_item_pool_root &&n) MANAPIHTTP_NOEXCEPT;

        operator bool() const MANAPIHTTP_NOEXCEPT {
            return !!this->object;
        }
    protected:
        void *object;
    };

    template<typename T>
    class object_item_pool : public object_item_pool_root {
    public:
        object_item_pool () : object_item_pool_root() {}

        ~object_item_pool() override;

        object_item_pool (object_item_pool &&n) MANAPIHTTP_NOEXCEPT = default;

        object_item_pool &operator=(object_item_pool &&n) MANAPIHTTP_NOEXCEPT = default;

        T *operator->() {
            return this->object;
        }

        T &operator*() {
            return *this->object;
        }

        T *release () {
            return std::exchange(this->object, nullptr);
        }
    };

    class object_pool {
        std::shared_ptr<object_pool_data_t> data;
    public:
        typedef std::unique_ptr<manapi::slice> item;

        object_pool();

        ~object_pool();

        //manapi::slice slice (std::size_t min, std::size_t max);

        manapi::status_or<manapi::slice> slice (std::size_t suggested);

        manapi::status_or<manapi::bytebuffer> buffer (std::size_t min, std::size_t max);

        manapi::status_or<manapi::bytebuffer> buffer (std::size_t suggested);

        manapi::bytebuffer buffer (void *pointer, std::size_t suggested);

        void *alloc (std::size_t size) MANAPIHTTP_NOEXCEPT;

        void *realloc (void *ptr, std::size_t) MANAPIHTTP_NOEXCEPT;

        void free (void *ptr, std::size_t size) MANAPIHTTP_NOEXCEPT;

        void clear ();

        static int mem_type (std::size_t size) MANAPIHTTP_NOEXCEPT;

        static constexpr std::size_t area_size () { return 4096; }

        template<typename T, typename ...Args>
        manapi::status_or<object_item_pool<T>> get (Args&&...args) {
            auto b = this->alloc (sizeof (T));
            try {
                auto const data = new(b) T (std::forward<decltype(args)>(args)...);
                return object_item_pool<T> (data);
            }
            catch (std::exception const &e) {
                this->free(b, sizeof (T));
                manapi_log_error("%s due to %s", "object init failed", e.what());
                return manapi::status_internal("object init failed");
            }
        }

        void free (void *pointer) MANAPIHTTP_NOEXCEPT;

        template<typename T = void>
        void free (T *pointer) MANAPIHTTP_NOEXCEPT {
            return this->free(pointer, sizeof (T));
        }
    };

    template<typename T>
    manapi::object_item_pool<T>::~object_item_pool() {
        manapi::async::memory_fabric()->free(this->object, sizeof (T));
    }
}
