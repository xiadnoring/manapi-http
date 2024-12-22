#ifndef MANAPIHTTP_SMARTBUFFER_HPP
#define MANAPIHTTP_SMARTBUFFER_HPP

#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "ManapiAsync.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
#include "async/ManapiAsyncMutex.hpp"

namespace manapi::net::worker {
    class smart_w_buffer {
    public:
        smart_w_buffer (std::shared_ptr<threadpool<task>> taskpool, const std::function<future<ssize_t> (void *, ssize_t size, bool flag)> &callback, size_t sent = 0, ssize_t frame_size = 16384);
        ~smart_w_buffer();
        smart_w_buffer (smart_w_buffer &&n) noexcept;
        smart_w_buffer &operator= (smart_w_buffer &&n) noexcept;
        future<void> resize (size_t size);
        future<void> add_allow_to_sent (int size);
        future<size_t> add (const void *c, size_t len, bool flag = false);
        future<void> disable ();
    private:
        std::atomic<bool> disabled = false;
        std::function<future<ssize_t> (void *, ssize_t size, bool flag)> callback;
        future<ssize_t> _work (bool flag);
        async_mutex gmx;
        async_condition_variable cv;
        std::string buffer;
        size_t maxsize = 0;
        std::atomic<ssize_t> sent = 0;
        ssize_t frame_size = 0;
        std::shared_ptr<threadpool<task>> taskpool;
        bool flag;
    };

    class smart_r_buffer {
    public:
        smart_r_buffer (std::shared_ptr<threadpool<task>> taskpool, const std::function<future<void>(int)> &callback, int frame_size = 16384);
        ~smart_r_buffer();
        smart_r_buffer (smart_r_buffer &&n) noexcept;
        smart_r_buffer &operator= (smart_r_buffer &&n) noexcept;
        future<void> resize (int frame_size);
        future<void> add (const void *c, size_t len, bool flag = false);
        future<ssize_t> read (void *c, size_t len);
        future<void> disable ();
    private:
        std::string buffer;
        std::atomic<bool> disabled = false;
        std::function<future<void>(int)> callback;
        async_condition_variable cv;
        std::shared_ptr<threadpool<task>> taskpool;
        int frame_size = 0;
        async_mutex gmx;
        size_t i = 0;
    };
}

#endif //MANAPIHTTP_SMARTBUFFER_HPP
