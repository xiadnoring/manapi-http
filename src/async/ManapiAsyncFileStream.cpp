#include "async/ManapiAsyncFileStream.hpp"

#include <fcntl.h>

#include "ManapiFilesystem.hpp"
#include "../include/ManapiWindows.hpp"

manapi::filesystem::fstream::fstream(std::string path, async::cancellation_action cancellation) {
    this->data = std::make_shared<fstream_data_t_>(
        std::move(path),
        std::move(cancellation),
        -1,
        0,
        0
    );
}

manapi::filesystem::fstream::fstream(fstream &&n) noexcept {
    this->data = std::move(n.data);
}

manapi::filesystem::fstream & manapi::filesystem::fstream::operator=(fstream &&n) noexcept {
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

manapi::future<> manapi::filesystem::fstream::open(int flags, int mode) {
    if ((mode & ev::FS_O_WRONLY) && !(mode & (ev::FS_O_RDONLY|ev::FS_O_RDWR))) {
        this->data->off_ = -1;
    }
    else {
        this->data->off_ = 0;
    }

    this->data->file = co_await manapi::filesystem::async_open(this->data->path, flags, mode,
        async::cancellation_action::unit(this->data->cancellation));
}

bool manapi::filesystem::fstream::is_open() const {
    return (this->data->file >= 0) && !(this->data->status & FILE_CLOSED);
}

manapi::filesystem::fstream::~fstream() {
    if (1 == this->data.use_count()) {
        manapi::async::run(fstream::close_(this->data->file));
    }
}

manapi::future<ssize_t> manapi::filesystem::fstream::read(void *buff, ssize_t buff_size) {
    try {
        while (true) {
            ssize_t rhs;

            rhs = co_await manapi::filesystem::async_read(this->data->file, buff, buff_size, this->data->off_,
                manapi::async::cancellation_action::unit(this->data->cancellation));


            if (rhs < 0) {
                break;
            }

            if (rhs == 0) {
                this->data->status |= FILE_EOF;
            }

            if (this->data->off_ >= 0) {
                this->data->off_ += rhs;
            }

            co_return rhs;
        }
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            ERR_FS_IO, "fs: read() failed due to {}", e.what());
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::write(const void *buff, ssize_t buff_size) {
    while (true) {
        ssize_t rhs;
        // auto rhs = static_cast<ssize_t> (::write(this->data->file, buff, buff_size));
        //
        // if (rhs < 0) {
        //     if (errno == EAGAIN || errno == EWOULDBLOCK) {
        //
        //
        //         continue;
        //     }
        //
        //     break;
        // }
        rhs = co_await manapi::filesystem::async_write(this->data->file, buff, buff_size, this->data->off_,
            manapi::async::cancellation_action::unit(this->data->cancellation));

        if (this->data->off_ >= 0) {
            this->data->off_ += rhs;
        }

        co_return rhs;
    }

    co_return -1;
}

manapi::future<> manapi::filesystem::fstream::fwrite(const void *buff, ssize_t buff_size) {
    while (buff_size > 0) {
        auto rhs = co_await this->write(buff, buff_size);

        if (rhs <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to write to a file. code: {}", rhs);
        }

        buff_size -= rhs;
    }
}

manapi::future<ssize_t> manapi::filesystem::fstream::fread(void *buff, ssize_t buff_size) {
    ssize_t total = 0;

    while (total < buff_size) {
        auto rhs = co_await this->read(static_cast<uint8_t *>(buff) + total, buff_size - total);
        if (rhs < 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to read from the file. rhs: {}", rhs);
        }
        if (rhs == 0 && this->eof()) {
            break;
        }
        total += rhs;
    }

    co_return total;
}

manapi::future<> manapi::filesystem::fstream::close() {
    if (this->data->status.fetch_or(FILE_CLOSED) & FILE_CLOSED) {
        return async::blank_future<void>();
    }
    return fstream::close_(std::exchange(this->data->file, -1));
}

ssize_t manapi::filesystem::fstream::tellg() const {
    return this->data->off_;
}

ssize_t manapi::filesystem::fstream::seekg(const ssize_t &pos, const seek_flag_t &flag) {
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

ssize_t manapi::filesystem::fstream::seekg_(const ssize_t &pos, const seek_flag_t &flag) const {
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
    if (fileno) {
        co_await manapi::filesystem::async_close(fileno);
    }
}
