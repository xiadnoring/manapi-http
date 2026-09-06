#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiEventLoop.hpp"
#include "../ManapiThreadPool.hpp"
#include "../std/ManapiCancelToken.hpp"

namespace manapi::fs {
    class fstream : public std::enable_shared_from_this<fstream> {
        fstream (std::string path, ctoken cancellation);

        fstream (ev::unique_file path, ctoken cancellation);

        fstream (ev::file path, bool own, ctoken cancellation);
    public:
        struct fstream_data_t;

        enum seek_flag_t {
            FILE_SEEK_START = 0,
            FILE_SEEK_CURRENT
        };


        operator bool () const MANAPIHTTP_NOEXCEPT;

        static manapi::status_or<std::shared_ptr<fstream>> create (std::string path, ctoken cancellation = nullptr) MANAPIHTTP_NOEXCEPT;

        static manapi::status_or<std::shared_ptr<fstream>> create (ev::unique_file fd, ctoken cancellation = nullptr) MANAPIHTTP_NOEXCEPT;

        static manapi::status_or<std::shared_ptr<fstream>> create (ev::file fd, bool own, ctoken cancellation = nullptr) MANAPIHTTP_NOEXCEPT;

        fstream (fstream &&n) MANAPIHTTP_NOEXCEPT = delete;

        fstream &operator=(fstream &&n) MANAPIHTTP_NOEXCEPT = delete;

        fstream (const fstream &n) = delete;

        fstream &operator=(const fstream &n) = delete;

        future<manapi::ev::status> open (int flags, int mode = 0644);

        MANAPIHTTP_NODISCARD bool is_open () const;

        ~fstream();

        future<ssize_t> read (void *buff, std::size_t buff_size);

        future<ssize_t> write (const void *buff, std::size_t buff_size);

        future<ssize_t> fwrite (const void *buff, std::size_t buff_size);

        future<ssize_t> fread (void *buff, std::size_t buff_size);

        future<ssize_t> read (manapi::slice_view slice);

        future<ssize_t> write (manapi::slice_view slice);

        future<ssize_t> fread (manapi::slice_view slice);

        future<ssize_t> fwrite (manapi::slice_view slice);

        void close ();

        MANAPIHTTP_NODISCARD ssize_t tellg() const;

        ssize_t seekg (ssize_t pos, seek_flag_t flag = FILE_SEEK_START);

        MANAPIHTTP_NODISCARD manapi::future<manapi::ev::status_or<std::size_t>> size () const;

        MANAPIHTTP_NODISCARD bool eof () const;
    private:
        fstream_data_t* m_data;
    };
}
