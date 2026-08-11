#include "fs/ManapiFileStream.hpp"

#include <fcntl.h>

#include "fs/ManapiFilesystem.hpp"
#include "../include/ManapiWindows.hpp"

enum fstream_status_flags {
    FILE_READ   = 1<<0,
    FILE_WRITE  = 1<<1,
    FILE_CLOSED = 1<<2,
    FILE_EOF    = 1<<3,
    FILE_OWN_FD = 1<<4,
    FILE_HAS_FD = 1<<5
};

struct manapi::fs::fstream::fstream_data_t {
    std::string path;
    manapi::ctoken cancellation;
    manapi::ev::file file;
    int status;
    off_t off_;
};

static bool manapi__check_fd (manapi::fs::fstream::fstream_data_t *data) {
    return (data->status & FILE_HAS_FD);
}

static ssize_t manapi__fstream_seekg(manapi::fs::fstream::fstream_data_t *data, ssize_t pos, manapi::fs::fstream::seek_flag_t flag) {
    if (data->off_ < 0) {
        data->off_ = 0;
    }

    if ((data->status & FILE_EOF)) {
        data->status ^= FILE_EOF;
    }

    auto prev = data->off_;
    switch (flag) {
        case manapi::fs::fstream::FILE_SEEK_START: data->off_ = static_cast<long>(pos); break;
        case manapi::fs::fstream::FILE_SEEK_CURRENT: data->off_ += static_cast<long>(pos); break;
    }
    return prev;
}

manapi::fs::fstream::fstream(std::string path, ctoken cancellation) : m_data(new fstream_data_t{}) {
    assert(this->m_data->status == 0);
    this->m_data->path = std::move(path);
    this->m_data->cancellation = std::move(cancellation);
}

manapi::fs::fstream::fstream(manapi::ev::unique_file path, manapi::ctoken cancellation) : m_data(new fstream_data_t{}) {
    assert(this->m_data->status == 0);
    this->m_data->file = path.release().unwrap();
    this->m_data->status |= FILE_OWN_FD|FILE_HAS_FD;
    this->m_data->cancellation = std::move(cancellation);
}

manapi::fs::fstream::fstream(manapi::ev::file path, bool own, manapi::ctoken cancellation) : m_data(new fstream_data_t{}) {
    assert(this->m_data->status == 0);
    this->m_data->file = path;
    this->m_data->status |= FILE_HAS_FD;
    this->m_data->cancellation = std::move(cancellation);

    if (own) this->m_data->status |= FILE_OWN_FD;
}

manapi::fs::fstream::operator bool() const MANAPIHTTP_NOEXCEPT {
    return !!this->m_data;
}

manapi::status_or<std::shared_ptr<manapi::fs::fstream>> manapi::fs::fstream::create(std::string path, ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    try {
        return std::shared_ptr<manapi::fs::fstream> (new manapi::fs::fstream(std::move(path), std::move(cancellation)));
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
}

manapi::future<manapi::ev::status> manapi::fs::fstream::open(int flags, int mode) {
    if (! (this->m_data->status & FILE_HAS_FD)) {

        if ((mode & ev::FS_O_WRONLY) && !(mode & (ev::FS_O_RDONLY | ev::FS_O_RDWR))) {
            this->m_data->off_ = -1;
        } else {
            this->m_data->off_ = 0;
        }

        try {
            auto res = co_await manapi::fs::async_open(this->m_data->path, flags, mode,
                                                       ctoken::unit(this->m_data->cancellation));
            if (!res.ok())
                co_return res.err();

            this->m_data->file = res.unwrap().release();
            this->m_data->status |= FILE_OWN_FD|FILE_HAS_FD;
        }
        catch (std::bad_alloc const &) {
            co_return manapi::ev::status_resource_exhausted();
        }
        catch (manapi::exception const &e) {
            manapi_log_error("%s due to %s", "file open failed", e.what());
            co_return manapi::ev::status_internal("file open failed", ev::ERR_UNKNOWN);
        }
        catch (std::exception const &e) {
            manapi_log_error("%s due to %s", "file open failed", e.what());
            co_return manapi::ev::status_internal("file open failed", ev::ERR_UNKNOWN);
        }
    }

    co_return manapi::ev::status_ok();
}

bool manapi::fs::fstream::is_open() const {
    return (this->m_data->status & FILE_HAS_FD) && !(this->m_data->status & FILE_CLOSED);
}

manapi::fs::fstream::~fstream() {
    this->close();

    delete this->m_data;
}

manapi::future<ssize_t> manapi::fs::fstream::read(void *buff, std::size_t buff_size) {
    if (!::manapi__check_fd (this->m_data)) co_return -1;

    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::fs::async_read(this->m_data->file, buff, buff_size, this->m_data->off_,
            manapi::ctoken::unit(this->m_data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (rhs < 0)
            break;

        if (rhs == 0)
            this->m_data->status |= FILE_EOF;

        if (this->m_data->off_ >= 0)
            this->m_data->off_ += static_cast<long>(rhs);

        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::fs::fstream::write(const void *buff, std::size_t buff_size) {
    if (!::manapi__check_fd (this->m_data)) co_return -1;

    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::fs::async_write(this->m_data->file, buff, buff_size, this->m_data->off_,
            manapi::ctoken::unit(this->m_data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (this->m_data->off_ >= 0) {
            this->m_data->off_ += static_cast<long>(rhs);
        }

        co_return rhs;
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::fs::fstream::fwrite(const void *buff, std::size_t buff_size) {
    std::size_t res = 0;
    while (res != buff_size) {
        auto const rhs = co_await this->write(static_cast<const char *>(buff) + res, buff_size - res);

        if (rhs <= 0) {
            co_return -1;
        }

        res += static_cast<std::size_t>(rhs);
    }
    co_return static_cast<ssize_t>(res);
}

manapi::future<ssize_t> manapi::fs::fstream::fread(void *buff, std::size_t buff_size) {
    std::size_t total = 0;

    while (total < buff_size) {
        auto rhs = co_await this->read(static_cast<uint8_t *>(buff) + total, buff_size - total);
        if (rhs < 0) {
            co_return -1;
        }
        if (rhs == 0 && this->eof()) {
            break;
        }
        total += static_cast<std::size_t>(rhs);
    }

    co_return static_cast<ssize_t>(total);
}

manapi::future<ssize_t> manapi::fs::fstream::read(manapi::slice_view slice) {
    if (!::manapi__check_fd (this->m_data)) co_return -1;

    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::fs::async_read(this->m_data->file, slice, this->m_data->off_,
            manapi::ctoken::unit(this->m_data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (rhs < 0)
            break;

        if (rhs == 0)
            this->m_data->status |= FILE_EOF;

        if (this->m_data->off_ >= 0)
            this->m_data->off_ += static_cast<long>(rhs);

        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::fs::fstream::write(manapi::slice_view slice) {
    if (!::manapi__check_fd (this->m_data)) co_return -1;

    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::fs::async_write(this->m_data->file, slice, this->m_data->off_,
            manapi::ctoken::unit(this->m_data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (this->m_data->off_ >= 0)
            this->m_data->off_ += static_cast<long>(rhs);

        co_return rhs;
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::fs::fstream::fread(manapi::slice_view slice) {
    std::size_t total = 0;

    while (total != slice.size()) {
        auto rhs = co_await this->read(slice.subslice(total).unwrap());
        if (rhs < 0)
            co_return -1;

        if (rhs == 0
            && this->eof())
            break;

        total += static_cast<std::size_t>(rhs);
    }

    co_return static_cast<ssize_t>(total);
}

manapi::future<ssize_t> manapi::fs::fstream::fwrite(manapi::slice_view slice) {
    ssize_t res = 0;
    while (slice.size()) {
        auto const rhs = co_await this->write(slice);

        if (rhs <= 0)
            co_return -1;

        // if (!slice.shift_add(static_cast<std::size_t>(rhs)).ok())
        //     co_return -1;

        auto z = slice.subslice(static_cast<std::size_t>(rhs));
        if (!z)
            co_return -1;

        slice = z.unwrap();

        res += rhs;
    }
    co_return res;
}

void manapi::fs::fstream::close() {
    if (this->m_data->status & FILE_HAS_FD) {
        if (this->m_data->status & FILE_OWN_FD) {
            manapi::ev::unique_file fd(this->m_data->file);
            this->m_data->status ^= FILE_OWN_FD;
        }

        this->m_data->status ^= FILE_HAS_FD;
        this->m_data->file = 0;
    }
}

ssize_t manapi::fs::fstream::tellg() const {
    return this->m_data->off_;
}

ssize_t manapi::fs::fstream::seekg(ssize_t pos, seek_flag_t flag) {
    return ::manapi__fstream_seekg(this->m_data, pos, flag);
}

manapi::future<manapi::ev::status_or<std::size_t>> manapi::fs::fstream::size() const {
    if (!::manapi__check_fd (this->m_data))
        co_return manapi::ev::status_not_found("fstream:failed");

    std::size_t size;
    bool failed{true};
    auto res = co_await manapi::fs::async_fstat(this->m_data->file, [&size, &failed] (ev::stat_t *data)
        -> void {
        if (data) {
            size = static_cast<std::size_t>(data->st_size);
            failed = false;
        }
    }, ctoken::unit(this->m_data->cancellation));
    if (!res)
        co_return std::move(res);
    if (failed)
        co_return manapi::ev::status_not_found("fstream:file not found");
    co_return size;
}

bool manapi::fs::fstream::eof() const {
    return this->m_data->status & FILE_EOF;
}

manapi::status_or<std::shared_ptr<manapi::fs::fstream>>
manapi::fs::fstream::create(manapi::ev::unique_file fd, manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    try {
        return std::shared_ptr<manapi::fs::fstream> (new manapi::fs::fstream(std::move(fd), std::move(cancellation)));
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
}

manapi::status_or<std::shared_ptr<manapi::fs::fstream>>
manapi::fs::fstream::create(manapi::ev::file fd, bool own, manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    try {
        return std::shared_ptr<manapi::fs::fstream> (new manapi::fs::fstream(fd, own, std::move(cancellation)));
    }
    catch (std::exception const &) {
        return manapi::status_resource_exhausted();
    }
}
