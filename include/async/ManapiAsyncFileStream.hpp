#pragma once
#include "../ManapiUtils.hpp"
#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTask.hpp"
#include "../services/ManapiThreadPool.hpp"

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#endif

namespace manapi::filesystem {
    class fstream {
        enum fstream_status_flags {
            FILE_READ = 0b1,
            FILE_WRITE  = 0b10,
            FILE_CLOSED = 0b100,
            FILE_EOF = 0b1000
        };
        struct fstream_data_t_ {
            std::string path;
            std::shared_ptr<threadpool<task>> taskpool;
            std::shared_ptr<event_loop> eventloop;
            std::shared_ptr<ev::io> watcher{nullptr};
            manapi::async::promise<void>::resolve_t w_resolve{nullptr};
            manapi::async::promise<void>::resolve_t r_resolve{nullptr};
            std::shared_ptr<ev::fs> open;
            std::shared_ptr<ev::fs> io;
            std::atomic<int> status{0};
            off_t off_;
        };
    public:
        enum seek_flag_t {
            FILE_SEEK_START = 0,
            FILE_SEEK_CURRENT
        };

        fstream (const std::shared_ptr<manapi::async::context> &ctx, std::string_view path);
        fstream (const std::shared_ptr<event_loop> &eventloop, std::string_view path);
        fstream (fstream &&n) noexcept;
        fstream &operator=(fstream &&n) noexcept;
        fstream (const fstream &n);
        fstream &operator=(const fstream &n);
        future<> open (int flags, int mode = 0644);
        [[nodiscard]] bool is_open () const;
        ~fstream();
        future<ssize_t> read (void *buff, ssize_t buff_size);
        future<ssize_t> write (const void *buff, ssize_t buff_size);
        future<> fwrite (const void *buff, ssize_t buff_size);
        future<ssize_t> fread (void *buff, ssize_t buff_size);
        future<> close ();
        void sync_close ();
        ssize_t seekg (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START);
        [[nodiscard]] ssize_t tellg () const;
        [[nodiscard]] manapi::future<ssize_t> total_size () const;
        [[nodiscard]] bool eof () const;
    private:
        ssize_t seekg_ (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START) const;
        static void event_ (std::shared_ptr<ev::io> &w, int status, int revents, const std::shared_ptr<fstream_data_t_> &data);

        static future<> close_(std::shared_ptr<fstream_data_t_> data);
        static void sync_close_ (std::shared_ptr<fstream_data_t_> data);

        std::shared_ptr<fstream_data_t_> data;
    };
}
