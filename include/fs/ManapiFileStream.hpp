#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiEventLoop.hpp"
#include "../ManapiThreadPool.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::fs {
    class fstream {
    public:
        struct fstream_data_t;

        enum seek_flag_t {
            FILE_SEEK_START = 0,
            FILE_SEEK_CURRENT
        };

        fstream ();

        operator bool () const MANAPIHTTP_NOEXCEPT;

        static manapi::status_or<fstream> create (std::string path, ctoken cancellation = nullptr) MANAPIHTTP_NOEXCEPT;

        fstream (fstream &&n) MANAPIHTTP_NOEXCEPT;

        fstream &operator=(fstream &&n) MANAPIHTTP_NOEXCEPT;

        fstream (const fstream &n);

        fstream &operator=(const fstream &n);

        future<manapi::ev::status> open (int flags, int mode = 0644);

        MANAPIHTTP_NODISCARD bool is_open () const;

        ~fstream();

        future<ssize_t> read (void *buff, ssize_t buff_size);

        future<ssize_t> write (const void *buff, ssize_t buff_size);

        future<ssize_t> fwrite (const void *buff, ssize_t buff_size);

        future<ssize_t> fread (void *buff, ssize_t buff_size);

        future<ssize_t> read (manapi::slice_view slice);

        future<ssize_t> write (manapi::slice_view slice);

        future<ssize_t> fread (manapi::slice_view slice);

        future<ssize_t> fwrite (manapi::slice_view slice);

        future<ev::status> close_and_wait ();

        void close ();

        MANAPIHTTP_NODISCARD ssize_t tellg() const;

        ssize_t seekg (ssize_t pos, seek_flag_t flag = FILE_SEEK_START);

        MANAPIHTTP_NODISCARD manapi::future<ssize_t> size () const;

        MANAPIHTTP_NODISCARD bool eof () const;
    private:
        std::shared_ptr<fstream_data_t> m_data;
    };
}
