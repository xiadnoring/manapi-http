#include "components/ManapiObjectPool.hpp"

#include <assert.h>

#include "ManapiAsync.hpp"
#include "async/ManapiAsyncContext.hpp"

// static(soon) | 32 64 128 | 256 512 1024 | 2048 4096 8192 | 16384 32768 | 65536

// thread_local std::set<void *> pointers;

enum buffer_level {
    BUFF_LEVEL_32 = 0,
    BUFF_LEVEL_256,
    BUFF_LEVEL_4096,
    BUFF_LEVEL_16384,
    BUFF_LEVEL_65536,
    BUFF_LEVEL_MAX
};

constexpr int area_size = 4096;

struct manapi::internal::object_pool_data_t {
    manapi::chain<std::pair<void*, int>> buffers[BUFF_LEVEL_MAX + 1];
};

int bufflen2level (int len) {
    if (len <= 32) {
        return BUFF_LEVEL_32;
    }
    if (len <= 256) {
        return BUFF_LEVEL_256;
    }
    if (len <= 4096) {
        return BUFF_LEVEL_4096;
    }
    if (len <= 16384) {
        return BUFF_LEVEL_16384;
    }
    if (len <= 65536) {
        return BUFF_LEVEL_65536;
    }

    return BUFF_LEVEL_MAX;
}

int level2bufflen (int lvl) {
    switch (lvl) {
        case BUFF_LEVEL_32: return 32;
        case BUFF_LEVEL_256: return 256;
        case BUFF_LEVEL_4096: return 4096;
        case BUFF_LEVEL_16384: return 16384;
        case BUFF_LEVEL_65536: return 65536;
    }
    return 65536;
}

void manapi::internal::object_item_pool_return(const std::shared_ptr<internal::object_pool_data_t> &data, void *buffer, std::size_t size) {
    assert((buffer && size));
    //assert(pointers.contains(buffer));
    data->buffers[bufflen2level(size)].push_back({buffer, size});
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

                delete static_cast<char *>(pn.first);
            }
        }

        delete p;
    }
};

manapi::object_pool::object_pool() {
    this->data = decltype(this->data) (new internal::object_pool_data_t{}, object_pool_deleter{});
}

manapi::object_pool::~object_pool() = default;

void object_pool_malloc (manapi::internal::object_pool_data_t *data, void **ptr, std::size_t *ptr_size, std::size_t suggested) {
    auto const lvl = bufflen2level(static_cast<int>(suggested));
    auto size = level2bufflen(lvl);
    if (size < suggested) {
        size = static_cast<int>(suggested);
        auto m = manapi::memory::alloc<char>(size);
        assert(m && "buffer is null");
        *ptr = m;
        *ptr_size = size;
        return;
    }

    auto &bufferpool = data->buffers[lvl];
    if (!bufferpool.empty()) {
        auto it = bufferpool.back();
        bufferpool.pop_back();

        *ptr = it.first;
        *ptr_size = it.second;
        return;
    }

    auto m = manapi::memory::alloc<char>(size);
    assert(m && "buffer is null");
    //pointers.insert(m);

    *ptr = m;
    *ptr_size = size;
}

manapi::slice manapi::object_pool::slice(std::size_t suggested) {
    std::size_t cnt = suggested / area_size;
    std::size_t const left = suggested - cnt * area_size;

    std::unique_ptr<slice_part_t, slice::slice_part_deleter> buffs{};

    slice_part_t *cur{nullptr};

    for (std::size_t i =0 ; i < cnt; i++) {
        if (cur) {
            cur->next = new slice_part_t ({}, nullptr);
            cur = cur->next;
        }
        else {
            buffs.reset(new slice_part_t ({}, nullptr));
            buffs->buff.len = 0;
            cur = buffs.get();
        }

        void *buffptr;
        std::size_t buffsize;

        object_pool_malloc(this->data.get(), &buffptr, &buffsize, area_size);

        cur->buff.base = static_cast<char *>(buffptr);
        cur->buff.len = buffsize;
    }

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

        void *buffptr;
        std::size_t buffsize;

        object_pool_malloc(this->data.get(), &buffptr, &buffsize, left);

        cur->buff.base = static_cast<char *>(buffptr);
        cur->buff.len = buffsize;

        cnt ++;
    }

    return manapi::slice(std::move(buffs), cnt);
}

manapi::bytebuffer manapi::object_pool::buffer(std::size_t min, std::size_t max) {
    return this->buffer(max);
}

manapi::bytebuffer manapi::object_pool::buffer(std::size_t suggested) {
    void *buffer;
    std::size_t size;
    object_pool_malloc (this->data.get(), &buffer, &size, suggested);
    return this->buffer(buffer, size);
}

manapi::bytebuffer manapi::object_pool::buffer(void *pointer, std::size_t suggested) {
    return {pointer, suggested, bytebuffer::BYTEBUFFER_FLAG_OBJECT_POOL};
}

void manapi::object_pool::free(void *pointer, std::size_t size) {
    return manapi::internal::object_item_pool_return(this->data, pointer, size);
}

