#include "async/ManapiAsyncFileStream.hpp"

#include <fcntl.h>
#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <io.h>
#   include <fcntl.h>
#   include <stdlib.h>
#   include <stdio.h>
#   include <share.h>
#endif

manapi::filesystem::fstream::fstream(const std::shared_ptr<manapi::async::context> &ctx, std::string_view path) {
    this->data = std::make_shared<fstream_data_t_>(
        std::string{path},
        ctx->taskpool(),
        ctx->eventloop()
    );
}

manapi::filesystem::fstream::fstream(const std::shared_ptr<event_loop> &eventloop, std::string_view path) {
    this->data = std::make_shared<fstream_data_t_>(
        std::string{path},
        eventloop->taskpool(),
        eventloop
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

    this->data->eventloop->fs_open(this->data->path, flags, mode, [] (std::shared_ptr<ev::fs> &w) -> void {

    });
}

bool manapi::filesystem::fstream::is_open() const {
    return this->data->fd >= 0;
}

manapi::filesystem::fstream::~fstream() {
    if (this->data) {
        manapi::async::run(this->data->taskpool, this->close());
    }
}

manapi::future<ssize_t> manapi::filesystem::fstream::read(void *buff, ssize_t buff_size) {
    while (true) {
        auto rhs = static_cast<ssize_t> (::read(this->data->fd, buff, buff_size));

        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await manapi::async::promise<void> (this->data->taskpool, [this] (manapi::async::promise<void>::resolve_t resolve, manapi::async::promise<void>::reject_t reject) -> future<> {
                    this->data->r_resolve = std::move(resolve);
                    this->data->status.fetch_or(FILE_READ);

                    co_return;
                });

                continue;
            }
            break;
        }

        co_return rhs;
    }

    co_return -1;
}

manapi::future<ssize_t> manapi::filesystem::fstream::write(const void *buff, ssize_t buff_size) {
    if (this->data->status & FILE_WRITE) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_THREAD_SAFE, "that function isn't thread safe");
    }

    while (true) {
        auto rhs = static_cast<ssize_t> (::write(this->data->fd, buff, buff_size));

        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await manapi::async::promise<void> (this->data->taskpool, [this] (manapi::async::promise<void>::resolve_t resolve, manapi::async::promise<void>::reject_t reject) -> future<> {
                    this->data->w_resolve = std::move(resolve);
                    this->data->status.fetch_or(FILE_WRITE);

                    co_return;
                });

                continue;
            }

            break;
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
        return async::blank_future();
    }
    return fstream::close_(this->data);
}

void manapi::filesystem::fstream::sync_close() {
    if (!(this->data->status.fetch_or(FILE_CLOSED) & FILE_CLOSED)) {
        this->sync_close_(this->data);
    }
}

ssize_t manapi::filesystem::fstream::seekg(const ssize_t &pos, const seek_flag_t &flag) {
    return this->seekg_(pos, flag);
}

ssize_t manapi::filesystem::fstream::tellg() const {
    return this->data->off_;
}

manapi::future<ssize_t> manapi::filesystem::fstream::total_size() const {

}

bool manapi::filesystem::fstream::eof() const {
    return this->data->status & FILE_EOF;
}

ssize_t manapi::filesystem::fstream::seekg_(const ssize_t &pos, const seek_flag_t &flag) const {
    auto prev = this->data->off_;
    switch (flag) {
        case FILE_SEEK_START: this->data->off_ = pos; break;
        case FILE_SEEK_CURRENT: this->data->off_ += pos; break;
    }
    return prev;
}

void manapi::filesystem::fstream::event_(std::shared_ptr<ev::io> &w, int status, int revents, const std::shared_ptr<fstream_data_t_> &data) {
    if ((revents & ev::READ) && (data->status & FILE_READ)) {
        data->status.fetch_xor(FILE_READ);
        data->taskpool->append_task([resolve = std::move(data->r_resolve)] ()
            -> void { resolve(); });
    }

    if ((revents & ev::WRITE) && (data->status & FILE_WRITE)) {
        data->status.fetch_xor(FILE_WRITE);
        data->taskpool->append_task([resolve = std::move(data->w_resolve)] ()
            -> void { resolve(); });
    }
}

void manapi::filesystem::fstream::sync_close_(std::shared_ptr<fstream_data_t_> data) {
    if (data->watcher) {
        data->eventloop->stop_watcher(data->watcher);
    }

    if (data->fd >= 0) {
        ::close(data->fd);
    }

    if (data->r_resolve) {
        data->r_resolve();
        data->r_resolve = nullptr;
    }

    if (data->w_resolve) {
        data->w_resolve();
        data->w_resolve = nullptr;
    }

}

manapi::future<> manapi::filesystem::fstream::close_(std::shared_ptr<fstream_data_t_> data) {
    return data->eventloop->custom_callback([data] (event_loop *ev1) mutable
        -> void { return fstream::sync_close_(std::move(data)); });
}
