#include "components/ManapiObjectPool.hpp"

#include <assert.h>

// static(soon) | 32 64 128 | 256 512 1024 | 2048 4096 8192 | 16384 32768 | 65536

enum buffer_level {
    BUFF_LEVEL_32 = 0,
    BUFF_LEVEL_256,
    BUFF_LEVEL_2048,
    BUFF_LEVEL_16384,
    BUFF_LEVEL_65536,
    BUFF_LEVEL_MAX
};

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
    if (len <= 2048) {
        return BUFF_LEVEL_2048;
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
        case BUFF_LEVEL_2048: return 2048;
        case BUFF_LEVEL_16384: return 16384;
        case BUFF_LEVEL_65536: return 65536;
    }
    return 65536;
}

void manapi::internal::object_item_pool_return(const std::shared_ptr<internal::object_pool_data_t> &data, void *buffer, int real_size) {
    assert((buffer && real_size));
    data->buffers[bufflen2level(real_size)].push_back({buffer, real_size});
}

manapi::object_pool::object_pool() {
    this->data = std::make_shared<internal::object_pool_data_t>();
}

manapi::bytebuffer manapi::object_pool::slice(std::size_t min, std::size_t max) {
    return this->slice(max);
}

manapi::bytebuffer manapi::object_pool::slice(std::size_t suggested) {
    auto const lvl = bufflen2level(static_cast<int>(suggested));
    auto size = level2bufflen(lvl);
    if (size < suggested) {
        size = static_cast<int>(suggested);
        auto m = manapi::memory::alloc<char>(size);
        assert(m && "buffer is null");
        return this->slice(m, size);
    }
    else {
        auto &bufferpool = this->data->buffers[lvl];
        if (!bufferpool.empty()) {
            auto it = bufferpool.back();
            bufferpool.pop_back();

            auto b = this->slice(it.first, it.second);
            b.resize(suggested);
            return std::move(b);
        }

        auto m = manapi::memory::alloc<char>(size);
        assert(m && "buffer is null");
        auto b = this->slice(m, size);
        b.resize(suggested);
        return std::move(b);
    }
}

manapi::bytebuffer manapi::object_pool::slice(void *pointer, std::size_t suggested) {
    return {pointer, suggested, BYTEBUFFER_FLAG_OBJECT_POOL};
}

void manapi::object_pool::unit(bytebuffer buffer) {
    auto const size = buffer.realsize();
    auto const pointer = buffer.release();
    this->object_item_pool_return(pointer, static_cast<int>(size));
}

void manapi::object_pool::object_item_pool_return(void *pointer, int size) {
    return manapi::internal::object_item_pool_return(this->data, pointer, size);
}

