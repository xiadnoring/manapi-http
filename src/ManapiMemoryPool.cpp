#include <cassert>
#include <cstring>

#include "ManapiMemoryPool.hpp"
#include "ManapiAsync.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "./include/ManapiUtils.hpp"

#ifdef linux
#   include "malloc.h"
#endif

// static(soon) | 32 64 128 | 256 512 1024 | 2048 4096 8192 | 16384 32768 | 65536

// thread_local std::set<void *> pointers;

#define MEM_USED 1048576

enum buffer_level {
    BUFF_LEVEL_16 = 1,
    BUFF_LEVEL_36,
    BUFF_LEVEL_64,
    BUFF_LEVEL_256,
    BUFF_LEVEL_4096,
    BUFF_LEVEL_8192,
    BUFF_LEVEL_16384,
    BUFF_LEVEL_65536,
    BUFF_LEVEL_MAX
};

constexpr int area_size = 4096;

struct manapi::internal::object_pool_data_t {
    manapi::chain<std::pair<void*, std::size_t>> buffers[BUFF_LEVEL_MAX + 1];
    std::size_t used;
    std::size_t locked;
    std::size_t cnt;
};

static int bufflen2level (std::size_t len) {
    if (len <= 16) {
        return BUFF_LEVEL_16;
    }
    if (len <= 36) {
        return BUFF_LEVEL_36;
    }
    if (len <= 64) {
        return BUFF_LEVEL_64;
    }
    if (len <= 256) {
        return BUFF_LEVEL_256;
    }
    if (len <= 4096) {
        return BUFF_LEVEL_4096;
    }
    if (len <= 8192) {
        return BUFF_LEVEL_8192;
    }
    if (len <= 16384) {
        return BUFF_LEVEL_16384;
    }
    if (len <= 65536) {
        return BUFF_LEVEL_65536;
    }

    return BUFF_LEVEL_MAX;
}

static std::size_t level2bufflen (int lvl) {
    switch (lvl) {
        case BUFF_LEVEL_16: return 16;
        case BUFF_LEVEL_36: return 36;
        case BUFF_LEVEL_64: return 64;
        case BUFF_LEVEL_256: return 256;
        case BUFF_LEVEL_4096: return 4096;
        case BUFF_LEVEL_8192: return 8192;
        case BUFF_LEVEL_16384: return 16384;
        case BUFF_LEVEL_65536: return 65536;
    }
    return 65536;
}

void object_item_pool_clear (const std::shared_ptr<manapi::internal::object_pool_data_t> &data) {
    auto &locked = data->locked;
    auto &used = data->used;

    for (auto &n : data->buffers) {
        while (!n.empty()) {
            auto pn = std::move(n.back());
            n.pop_back();
            locked -= pn.second;
            delete[] static_cast<char *>(pn.first);
        }
    }

// #ifdef linux
//     malloc_trim(0);
// #endif
}

void manapi::internal::object_item_pool_return(const std::shared_ptr<internal::object_pool_data_t> &data, void *buffer, std::size_t size) {
    assert((buffer && size));
    assert((data->used >= size));
    data->used -= size;
    data->cnt--;
    //assert(pointers.contains(buffer));
    auto const lvl = bufflen2level(size);
    auto &l = data->buffers[lvl];
    l.push_back({buffer, level2bufflen(lvl)});
    if (data->used < data->locked / 10 && data->used > MEM_USED)
        object_item_pool_clear (data);

}

void manapi::internal::object_item_pool_return(void *buffer, std::size_t size) {
    manapi::async::current()->memory_fabric().free(buffer, size);
}

struct object_pool_deleter {
    void operator () (manapi::internal::object_pool_data_t *p) {
        for (auto &buffs : p->buffers) {
            while (!buffs.empty()) {
                auto pn = std::move(buffs.back());
                buffs.pop_back();
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "memory fabric:%p (%d) was freed",
                    pn.first, pn.second);
                delete[] static_cast<char *>(pn.first);
            }
        }

        delete p;
    }
};

manapi::object_pool::object_pool() {
    this->data = decltype(this->data) (new internal::object_pool_data_t{}, object_pool_deleter{});
}

manapi::object_pool::~object_pool() = default;

int object_pool_malloc (manapi::internal::object_pool_data_t *data, void **ptr, std::size_t *ptr_size, std::size_t suggested) {
    auto const lvl = bufflen2level(suggested);
    auto size = level2bufflen(lvl);
    data->cnt ++;

    if (size < suggested) {
        size = suggested;
        auto m = new (std::nothrow) char[size];
        if (!m)
            return manapi::ERR_RESOURCE_EXHAUSTED;
        *ptr = m;
        *ptr_size = size;
        data->used += size;
        assert((*ptr_size >= suggested));
        return manapi::ERR_OK;
    }

    auto &bufferpool = data->buffers[lvl];
    if (!bufferpool.empty()) {
        auto it = bufferpool.back();
        bufferpool.pop_back();

        *ptr = it.first;
        *ptr_size = it.second;
        data->used += it.second;
        assert((*ptr_size >= suggested));
        return manapi::ERR_OK;
    }

    auto m = new (std::nothrow) char[size];
    if (!m)
        return manapi::ERR_RESOURCE_EXHAUSTED;

    //pointers.insert(m);

    *ptr = m;
    *ptr_size = size;
    data->used += size;
    data->locked += size;

    assert((*ptr_size >= suggested));
    return manapi::ERR_OK;
}

manapi::status_or<manapi::slice> manapi::object_pool::slice(std::size_t suggested) {
    std::size_t cnt = suggested / area_size;
    std::size_t const left = suggested - cnt * area_size;

    std::unique_ptr<slice_part_t, slice::slice_part_deleter> buffs{};

    slice_part_t *cur{nullptr};

    for (std::size_t i =0 ; i < cnt; i++) {
        if (cur) {
            cur->next = new (std::nothrow) slice_part_t ({}, nullptr);
            if (!cur->next)
                return status_resource_exhausted();
            cur = cur->next;
        }
        else {
            buffs.reset(new (std::nothrow) slice_part_t ({}, nullptr));
            if (!buffs)
                return status_resource_exhausted();

            buffs->buff.len = 0;
            cur = buffs.get();
        }

        void *buffptr{nullptr};
        std::size_t buffsize;

        if (object_pool_malloc(this->data.get(), &buffptr, &buffsize, area_size)) {
            delete []static_cast<char*>(buffptr);
            return status_resource_exhausted();
        }

        assert((buffsize >= area_size));
        cur->buff.base = static_cast<char *>(buffptr);
        cur->buff.len = static_cast<decltype(cur->buff.len)>(buffsize);
    }

    std::size_t rshift = 0;

    if (left) {
        if (cur) {
            cur->next = new slice_part_t ({}, nullptr);
            cur = cur->next;
        }
        else {
            buffs.reset(new slice_part_t ({}, nullptr));
            buffs->buff.len = 0;
            cur = buffs.get();
        }

        void *buffptr{nullptr};
        std::size_t buffsize;

        if (object_pool_malloc(this->data.get(), &buffptr, &buffsize, left)) {
            delete []static_cast<char*>(buffptr);
            return status_resource_exhausted();
        }

        cur->buff.base = static_cast<char *>(buffptr);
        cur->buff.len = static_cast<decltype(cur->buff.len)>(buffsize);

        rshift = (buffsize - left);

        cnt ++;
    }

    auto b = manapi::slice(std::move(buffs), static_cast<uint32_t>(cnt), rshift);
    assert(b.size() == suggested);
    return std::move(b);
}

manapi::status_or<manapi::bytebuffer> manapi::object_pool::buffer(std::size_t min, std::size_t max) {
    return this->buffer(max);
}

manapi::status_or<manapi::bytebuffer> manapi::object_pool::buffer(std::size_t suggested) {
    void *buffer{nullptr};
    std::size_t size;
    if (object_pool_malloc (this->data.get(), &buffer, &size, suggested)) {
        delete []static_cast<char*>(buffer);
        return status_resource_exhausted();
    }
    return this->buffer(buffer, (size));
}

manapi::bytebuffer manapi::object_pool::buffer(void *pointer, std::size_t suggested) {
    return {pointer, suggested, bytebuffer::BYTEBUFFER_FLAG_OBJECT_POOL};
}

void * manapi::object_pool::alloc(std::size_t size) MANAPIHTTP_NOEXCEPT {
    void *buffer;
    std::size_t rhs;
    //assert(size <= 65536);
    if (object_pool_malloc (this->data.get(), &buffer, &rhs, size + 1)) {
        delete []static_cast<char*>(buffer);
        return nullptr;
    }
    auto const lvl = static_cast<char>(bufflen2level(rhs));
    *static_cast<char*>(buffer) = lvl;
    return static_cast<char*>(buffer) + 1;
}

void * manapi::object_pool::realloc(void *ptr, std::size_t size) MANAPIHTTP_NOEXCEPT {
    if (!ptr)
        return this->alloc(size);

    //assert(size <= 65536);
    auto const p = (static_cast<char *> (ptr) - 1);
    auto const lvl = static_cast<int>(static_cast<uint8_t>(*p));
    if (lvl) {
        auto const len = level2bufflen(lvl);
        if (size < len)
            return ptr;
        auto n = this->alloc(size);
        if (!n)
            return nullptr;
        memcpy (n, ptr, len - 1);
        this->free(p, len);
        return n;
    }
    else {
        auto n = this->alloc(size);
        if (!n)
            return nullptr;
        memcpy (n, ptr, size);
        delete []static_cast<char*>(p);
        return n;
    }
}

void manapi::object_pool::free(void *ptr) MANAPIHTTP_NOEXCEPT {
    if (!ptr)
        return;

    auto const p = (static_cast<char *> (ptr) - 1);
    auto const lvl = static_cast<int>(static_cast<uint8_t>(*p));
    if (!lvl || lvl == BUFF_LEVEL_MAX) {
        delete []static_cast<char*>(p);
        return;
    }
    auto const len = level2bufflen(lvl);
    this->free(p, len);
}

void manapi::object_pool::free(void *pointer, std::size_t size) MANAPIHTTP_NOEXCEPT {
    return manapi::internal::object_item_pool_return(this->data, pointer, size);
}

void manapi::object_pool::clear() {
    object_item_pool_clear (this->data);
}

