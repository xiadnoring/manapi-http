#include "fs/ManapiFileStream.hpp"

#include <fcntl.h>

#include "fs/ManapiFilesystem.hpp"
#include "../include/ManapiWindows.hpp"

manapi::filesystem::fstream::fstream() : data() {

}

manapi::status_or<manapi::filesystem::fstream> manapi::filesystem::fstream::create(std::string path, async::cancellation_action cancellation) MANAPIHTTP_NOEXCEPT {
    try {
        fstream f;
        f.data = std::make_shared<fstream_data_t_>(
            std::move(path),
            std::move(cancellation),
            -1,
            0,
            0
        );
        return std::move(f);
    }
    catch (std::exception const &e) {
        return manapi::status_resource_exhausted();
    }
}

manapi::filesystem::fstream::fstream(fstream &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
}

manapi::filesystem::fstream & manapi::filesystem::fstream::operator=(fstream &&n) MANAPIHTTP_NOEXCEPT {
    this->data = std::move(n.data);
    return *this;
}

manapi::filesystem::fstream::fstream(const fstream &n) {
    this->data = n.data;
}

manapi::filesystem::fstream & manapi::filesystem::fstream::operator=(const fstream &n) {
    this->data = n.data;
    return *this;
}

manapi::future<manapi::ev::status> manapi::filesystem::fstream::open(int flags, int mode) {
    if ((mode & ev::FS_O_WRONLY) && !(mode & (ev::FS_O_RDONLY|ev::FS_O_RDWR))) {
        this->data->off_ = -1;
    }
    else {
        this->data->off_ = 0;
    }

    try {
        auto res = co_await manapi::filesystem::async_open(this->data->path, flags, mode,
            async::cancellation_action::unit(this->data->cancellation));
        if (!res.ok())
            co_return res.err();

        this->data->file = res.unwrap();
    }
    catch (std::bad_alloc const  &) {
        this->data->file = -1;
        co_return manapi::ev::status_resource_exhausted();
    }
    catch (manapi::exception const &e) {
        manapi_log_error("%s due to %s", "file open failed", e.what());
        this->data->file = -1;
        co_return manapi::ev::status_internal("file open failed", ev::ERR_UNKNOWN);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "file open failed", e.what());
        this->data->file = -1;
        co_return manapi::ev::status_internal("file open failed", ev::ERR_UNKNOWN);
    }

    co_return manapi::ev::status_ok();
}

bool manapi::filesystem::fstream::is_open() const {
    return (this->data->file >= 0) && !(this->data->status & FILE_CLOSED);
}

manapi::filesystem::fstream::~fstream() {
    if (1 == this->data.use_count()) {
        MANAPIHTTP_MUST_ALLOC_START
        manapi::async::run(fstream::close_(this->data->file));
        MANAPIHTTP_MUST_ALLOC_END
    }
}

manapi::future<ssize_t> manapi::filesystem::fstream::read(void *buff, ssize_t buff_size) {
    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::filesystem::async_read(this->data->file, buff, buff_size, this->data->off_,
            manapi::async::cancellation_action::unit(this->data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (rhs < 0)
            break;

        if (rhs == 0)
            this->data->status |= FILE_EOF;

        if (this->data->off_ >= 0)
            this->data->off_ += rhs;

        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::write(const void *buff, ssize_t buff_size) {
    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::filesystem::async_write(this->data->file, buff, buff_size, this->data->off_,
            manapi::async::cancellation_action::unit(this->data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (this->data->off_ >= 0) {
            this->data->off_ += rhs;
        }

        co_return rhs;
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::fwrite(const void *buff, ssize_t buff_size) {
    ssize_t res = 0;
    while (res != buff_size) {
        auto const rhs = co_await this->write(static_cast<const char *>(buff) + res, buff_size - res);

        if (rhs <= 0) {
            co_return -1;
        }

        res += rhs;
    }
    co_return res;
}

manapi::future<ssize_t> manapi::filesystem::fstream::fread(void *buff, ssize_t buff_size) {
    ssize_t total = 0;

    while (total < buff_size) {
        auto rhs = co_await this->read(static_cast<uint8_t *>(buff) + total, buff_size - total);
        if (rhs < 0) {
            co_return -1;
        }
        if (rhs == 0 && this->eof()) {
            break;
        }
        total += rhs;
    }

    co_return total;
}

manapi::future<ssize_t> manapi::filesystem::fstream::read(manapi::slice_view slice) {
    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::filesystem::async_read(this->data->file, slice, this->data->off_,
            manapi::async::cancellation_action::unit(this->data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (rhs < 0)
            break;

        if (rhs == 0)
            this->data->status |= FILE_EOF;

        if (this->data->off_ >= 0)
            this->data->off_ += rhs;

        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::write(manapi::slice_view slice) {
    while (true) {
        ssize_t rhs;

        auto res = co_await manapi::filesystem::async_write(this->data->file, slice, this->data->off_,
            manapi::async::cancellation_action::unit(this->data->cancellation));

        if (!res.ok())
            co_return res.syserr();

        rhs = res.unwrap();

        if (this->data->off_ >= 0)
            this->data->off_ += rhs;

        co_return rhs;
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::fread(manapi::slice_view slice) {
    ssize_t total = 0;

    while (total != slice.size()) {
        auto rhs = co_await this->read(slice.subslice(total).unwrap());
        if (rhs < 0)
            co_return -1;

        if (rhs == 0
            && this->eof())
            break;

        total += rhs;
    }

    co_return total;
}

manapi::future<ssize_t> manapi::filesystem::fstream::fwrite(manapi::slice_view slice) {
    ssize_t res = 0;
    while (slice.size()) {
        auto const rhs = co_await this->write(slice);

        if (rhs <= 0)
            co_return -1;

        if (!slice.shift_add(rhs).ok())
            co_return -1;

        res += rhs;
    }
    co_return res;
}

manapi::future<> manapi::filesystem::fstream::close() {
    if (this->data->status.fetch_or(FILE_CLOSED) & FILE_CLOSED) {
        co_return;
    }
    co_return co_await fstream::close_(std::exchange(this->data->file, -1));
}

ssize_t manapi::filesystem::fstream::tellg() const {
    return this->data->off_;
}

ssize_t manapi::filesystem::fstream::seekg(ssize_t pos, seek_flag_t flag) {
    return this->seekg_(pos, flag);
}

manapi::future<ssize_t> manapi::filesystem::fstream::size() const {
    ssize_t size;
    co_await manapi::filesystem::async_fstat(this->data->file, [&size] (ev::stat_t *data)
        -> void {
        size = static_cast<ssize_t>(data->st_size);
    }, async::cancellation_action::unit(this->data->cancellation));
    co_return size;
}

bool manapi::filesystem::fstream::eof() const {
    return this->data->status & FILE_EOF;
}

ssize_t manapi::filesystem::fstream::seekg_(ssize_t pos, seek_flag_t flag) const {
    if (this->data->off_ < 0) {
        this->data->off_ = 0;
    }

    if ((this->data->status & FILE_EOF)) {
        this->data->status ^= FILE_EOF;
    }

    auto prev = this->data->off_;
    switch (flag) {
        case FILE_SEEK_START: this->data->off_ = pos; break;
        case FILE_SEEK_CURRENT: this->data->off_ += pos; break;
    }
    return prev;
}

manapi::future<> manapi::filesystem::fstream::close_(ev::file fileno) {
    if (fileno > 0) {
        co_await manapi::filesystem::async_close(fileno);
    }
}
