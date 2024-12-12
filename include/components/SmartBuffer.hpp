#ifndef MANAPIHTTP_SMARTBUFFER_HPP
#define MANAPIHTTP_SMARTBUFFER_HPP

#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "ManapiAsync.hpp"

namespace manapi::net::worker {
    class smart_w_buffer {
    public:
        smart_w_buffer (const std::function<future<ssize_t> (void *, ssize_t size, bool flag)> &callback, size_t sent = 0, ssize_t frame_size = 16384);
        ~smart_w_buffer();
        smart_w_buffer (smart_w_buffer &&n) noexcept;
        smart_w_buffer &operator= (smart_w_buffer &&n) noexcept;
        void resize (size_t size);
        void add_allow_to_sent (int size);
        future<size_t> add (const void *c, size_t len, bool flag = false);
        void disable ();
    private:
        bool disabled = false;
        std::function<future<ssize_t> (void *, ssize_t size, bool flag)> callback;
        future<ssize_t> _work (bool flag);
        std::mutex gmx;
        std::mutex mx;
        std::condition_variable cv;
        std::string buffer;
        size_t maxsize = 0;
        std::atomic<ssize_t> sent = 0;
        ssize_t frame_size = 0;
        bool flag;
    };

    class smart_r_buffer {
    public:
        smart_r_buffer (const std::function<future<void>(int)> &callback, int frame_size = 16384);
        ~smart_r_buffer();
        smart_r_buffer (smart_r_buffer &&n) noexcept;
        smart_r_buffer &operator= (smart_r_buffer &&n) noexcept;
        void resize (int frame_size);
        void add (const void *c, size_t len, bool flag = false);
        future<ssize_t> read (void *c, size_t len);
        void disable ();
    private:
        std::string buffer;
        bool disabled = false;
        std::function<future<void>(int)> callback;
        std::mutex mx;
        std::condition_variable cv;
        int frame_size = 0;
        std::mutex gmx;
        size_t i = 0;
    };
}

#endif //MANAPIHTTP_SMARTBUFFER_HPP
