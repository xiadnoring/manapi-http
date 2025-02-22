#include <iostream>
#include <memory>

#include "worker/tools/OpenSSLTools.hpp"
#include "extensions/ev++.h"


static constexpr int prealloc_max{20};
static constexpr int prealloc_size{5 * 1024 * 1024};
static std::atomic<int> prealloc_index{0};

struct prealloc_deleter {
    void operator()(std::vector<void*> *cb) noexcept {
        for (int i = prealloc_index; i < prealloc_max; i++) {
            ::free(cb->at(i));
        }

        delete cb;
    }
};

static std::unique_ptr<std::vector <void*>, prealloc_deleter> prealloc{};

void manapi::net::worker::tools::ev_library_init() {
    prealloc.reset(new std::vector<void*>());

    for (int i = 0; i < prealloc_max; i++) {
        prealloc->push_back(::malloc(prealloc_size));
    }

    ev::set_allocator([] (void *ptr, long size) noexcept
        -> void * {
        if (ptr) {
            return ::realloc(ptr, size);
        }


        if (prealloc && size <= prealloc_size && prealloc_index < prealloc_max) {
            int index;
            if ((index = prealloc_index.fetch_add(1)) < prealloc_max) {
                return prealloc->at(index);
            }
        }

        return ::malloc(size);
    });
}