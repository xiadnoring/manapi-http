#pragma once
#include "ManapiCancellation.hpp"
#include "../ManapiUtils.hpp"
#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTask.hpp"
#include "../services/ManapiThreadPool.hpp"

namespace manapi::filesystem {
    class fstream {
        enum fstream_status_flags {
            FILE_READ = 0b1,
            FILE_WRITE  = 0b10,
            FILE_CLOSED = 0b100,
            FILE_EOF = 0b1000
        };
        struct fstream_data_t_ {
            async::shared_cthread ctx;
            std::string path;
            async::cancellation_action cancellation;
            ev::file file;
            std::atomic<int> status{0};
            off_t off_;
        };
    public:
        enum seek_flag_t {
            FILE_SEEK_START = 0,
            FILE_SEEK_CURRENT
        };

        fstream (async::shared_cthread ctx, std::string path, async::cancellation_action cancellation = nullptr);
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
        [[nodiscard]] ssize_t tellg() const;
        ssize_t seekg (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START);
        [[nodiscard]] manapi::future<ssize_t> size () const;
        [[nodiscard]] bool eof () const;
    private:
        ssize_t seekg_ (const ssize_t &pos, const seek_flag_t &flag = FILE_SEEK_START) const;

        static future<> close_(std::shared_ptr<fstream_data_t_> data);
        static void sync_close_ (std::shared_ptr<fstream_data_t_> data);

        std::shared_ptr<fstream_data_t_> data;
    };
}
