#pragma once
#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTask.hpp"
#include "../services/ManapiThreadPool.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#endif

namespace manapi::filesystem::async {
    struct fstream_data_t_ {
#ifdef _WIN32
        SOCKET fd;
#else
        int fd;
#endif
        std::string path;
        std::shared_ptr<threadpool<task>> taskpool;
        std::shared_ptr<event_loop> eventloop;
        std::shared_ptr<ev::io> watcher{nullptr};
        manapi::async::promise<void>::resolve_t w_resolve{nullptr};
        manapi::async::promise<void>::resolve_t r_resolve{nullptr};

        std::atomic<int> status{0};
    };

    class fstream {
    public:
        enum flags_t {
            FILE_RESERVED = 0b0,
            FILE_CREATE = 0b1,
            FILE_APPEND = 0b10,
            FILE_TRUNC = 0b100,
            FILE_READ = 0b1000,
            FILE_WRITE = 0b10000
        };

        enum seek_flag_t {
            FILE_SEEK_END = SEEK_END,
            FILE_SEEK_START = SEEK_SET,
            FILE_SEEK_CURRENT = SEEK_CUR
        };

        fstream (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path);
        fstream (const std::shared_ptr<event_loop> &eventloop, std::string_view path);
        future<> open (int flags = FILE_RESERVED, unsigned int mode = 0644);
        [[nodiscard]] bool is_open () const;
        ~fstream();
        future<ssize_t> read (void *buff, ssize_t buff_size);
        future<ssize_t> write (const void *buff, ssize_t buff_size);
        future<> fwrite (const void *buff, ssize_t buff_size);
        future<ssize_t> fread (void *buff, ssize_t buff_size);
        future<> close ();
        void seekg (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START);
        [[nodiscard]] ssize_t tellg () const;
        [[nodiscard]] ssize_t total_size () const;
        [[nodiscard]] bool eof () const;
    private:
        void _seekg (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START) const;
        static void _event (ev::io &w, int revents, const std::shared_ptr<fstream_data_t_> &data);
#ifdef _WIN32
        static future<> _close(std::shared_ptr<event_loop> ev, std::shared_ptr<ev::io> w, SOCKET fd);
#else
        static future<> _close(std::shared_ptr<event_loop> ev, std::shared_ptr<ev::io> w, int fd);
#endif

        std::shared_ptr<fstream_data_t_> data;
    };
}
