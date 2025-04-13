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
        -1,
        std::string{path},
        ctx->taskpool(),
        ctx->eventloop()
    );
}

manapi::filesystem::fstream::fstream(const std::shared_ptr<event_loop> &eventloop, std::string_view path) {
    this->data = std::make_shared<fstream_data_t_>(
        -1,
        std::string{path},
        eventloop->get_task_pool(),
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

manapi::future<> manapi::filesystem::fstream::open(int flags, unsigned int mode) {
    int o_flags = O_RDWR;
#ifdef _WIN32
#else
    o_flags |= O_NONBLOCK;
#endif
    if (flags&FILE_TRUNC) {
        o_flags |= O_TRUNC;
    }
    if (flags&FILE_APPEND) {
        o_flags |= O_APPEND;
    }
    if (flags&FILE_CREATE) {
        o_flags |= O_CREAT;
    }

    this->data->fd = ::open(this->data->path.data(), o_flags, mode);

    int ev_flags = 0;

    if (flags&FILE_READ) {
        ev_flags|=ev::READ;
    }
    if (flags&FILE_WRITE) {
        ev_flags|=ev::WRITE;
    }

    if (this->data->fd >= 0) {
        this->data->watcher = co_await this->data->eventloop->watch_fd(this->data->fd, ev_flags, [data = this->data] (ev::io &w, int revents)
            -> void { fstream::_event(w, revents, data); }, -1);
    }
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
    return fstream::_close(this->data->eventloop, std::move(this->data->watcher), std::exchange(this->data->fd, -1));
}

void manapi::filesystem::fstream::seekg(const ssize_t &pos, const seek_flag_t &flag) {
    this->_seekg(pos, flag);
}

ssize_t manapi::filesystem::fstream::tellg() const {
#ifdef _WIN32
    return _lseeki64(this->fd, 0, FILE_SEEK_CURRENT);
#else
    return lseek64(this->data->fd, 0, FILE_SEEK_CURRENT);
#endif
}

ssize_t manapi::filesystem::fstream::total_size() const {
    auto current = this->tellg();
    this->_seekg(0, FILE_SEEK_END);
    auto size = this->tellg();
    this->_seekg(current, FILE_SEEK_START);
    return size;
}

bool manapi::filesystem::fstream::eof() const {
    auto current = this->tellg();
    this->_seekg(0, FILE_SEEK_END);
    auto size = this->tellg();
    this->_seekg(current, FILE_SEEK_START);
    return current==size;
}

void manapi::filesystem::fstream::_seekg(const ssize_t &pos, const seek_flag_t &flag) const {
#ifdef _WIN32
    _lseeki64(this->data->fd, pos, static_cast<int>(flag));
#else
    lseek64(this->data->fd, pos, static_cast<int>(flag));
#endif
}

void manapi::filesystem::fstream::_event(ev::io &w, int revents, const std::shared_ptr<fstream_data_t_> &data) {
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
#ifdef _WIN32
manapi::future<> manapi::filesystem::fstream::_close(std::shared_ptr<event_loop> ev, std::shared_ptr<ev::io> w, SOCKET fd) {
    if (w) {
        co_await ev->unwatch_fd(std::move(w));
    }

    if (fd >= 0) {
        ::closesocket(fd);
    }
}
#else
manapi::future<> manapi::filesystem::fstream::_close(std::shared_ptr<event_loop> ev, std::shared_ptr<ev::io> w, int fd) {
    if (w) {
        co_await ev->unwatch_fd(std::move(w));
    }

    if (fd >= 0) {
        ::close(fd);
    }
}
#endif

