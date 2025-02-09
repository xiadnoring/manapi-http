#ifndef MANAPIHTTP_SMARTBUFFER_HPP
#define MANAPIHTTP_SMARTBUFFER_HPP

#include <string>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "../ManapiAsync.hpp"
#include "../async/ManapiAsyncConditionVariable.hpp"
#include "../async/ManapiAsyncMutex.hpp"

namespace manapi::net::worker {
    class smart_w_buffer {
    public:
        typedef std::function<future<ssize_t> (void *, ssize_t size, bool flag, std::atomic<bool> &disabled)> write_cb;

        smart_w_buffer (std::shared_ptr<threadpool<task>> taskpool, write_cb callback, size_t sent = 0, ssize_t buffer_size = 16384, ssize_t frame_size = 16384);
        ~smart_w_buffer();
        smart_w_buffer (smart_w_buffer &&n) noexcept;
        smart_w_buffer &operator= (smart_w_buffer &&n) noexcept;
        future<void> resize (ssize_t size);

        /**
         * add size of the bytes allow to send
         *
         * @param size of the bytes allow to send. set -1 for unlimited size
         * @return
         */
        future<void> add_allow_to_send (ssize_t size);
        future<ssize_t> add (const void *c, ssize_t len, bool flag = false);
        future<void> disable ();
    private:
        std::atomic<bool> disabled = false;
        write_cb callback;
        future<ssize_t> _work (bool flag);
        async::mutex gmx;
        async::condition_variable cv;
        std::string buffer{};
        size_t buffer_cursor = 0;
        size_t buffer_pos = 0;
        std::atomic<ssize_t> sent = 0;
        ssize_t frame_size;
        std::shared_ptr<threadpool<task>> taskpool;
        bool flag;
    };

    class smart_r_buffer {
    public:
        typedef std::function<future<void>(int)> read_cb;
        smart_r_buffer (std::shared_ptr<threadpool<task>> taskpool, read_cb callback, std::atomic<int> &want_read, int buffer_size = 16384);
        ~smart_r_buffer();
        smart_r_buffer (smart_r_buffer &&n) noexcept;
        smart_r_buffer &operator= (smart_r_buffer &&n) noexcept;
        future<void> resize (ssize_t buffer_size);
        future<ssize_t> add (const void *c, ssize_t len, bool flag = false);
        future<ssize_t> read (void *c, ssize_t len);
        future<void> disable ();
    private:
        bool available_read ();
        std::atomic<int> &want_read;
        int read_window = 0;
        std::string buffer{};
        size_t buffer_cursor = 0;
        size_t buffer_pos = 0;
        std::atomic<bool> disabled = false;
        read_cb callback;
        async::condition_variable cv;
        std::shared_ptr<threadpool<task>> taskpool;
        async::mutex gmx;
    };
}

#endif //MANAPIHTTP_SMARTBUFFER_HPP
