#include "async/ManapiAsyncFileStream.hpp"

#include <fcntl.h>

manapi::filesystem::async::fstream::fstream(const std::shared_ptr<manapi::async::context> &ctx, std::string_view path) : taskpool(ctx->taskpool()), eventloop(ctx->eventloop()) {
    this->path = path;
    this->fd = -1;
}

manapi::filesystem::async::fstream::fstream(const std::shared_ptr<event_loop> &eventloop, std::string_view path) : taskpool(eventloop->get_task_pool()), eventloop(eventloop) {
    this->path = path;
    this->fd = -1;
}

manapi::future<> manapi::filesystem::async::fstream::open(int flags, unsigned int mode) {
    int o_flags = O_NONBLOCK|O_RDWR;

    if (flags&FILE_TRUNC) {
        o_flags |= O_TRUNC;
    }
    if (flags&FILE_APPEND) {
        o_flags |= O_APPEND;
    }
    if (flags&FILE_CREATE) {
        o_flags |= O_CREAT;
    }

    this->fd = ::open(this->path.data(), o_flags, mode);

    int ev_flags = 0;

    if (flags&FILE_READ) {
        ev_flags|=ev::READ;
    }
    if (flags&FILE_WRITE) {
        ev_flags|=ev::WRITE;
    }

    if (this->fd >= 0) {
        this->watcher = co_await this->eventloop->watch_fd(this->fd, ev_flags, [this] (ev::io &w, int revents)
            -> void { this->_event(w, revents); }, -1);
    }
}

bool manapi::filesystem::async::fstream::is_open() const {
    return this->fd >= 0;
}

manapi::filesystem::async::fstream::~fstream() {
    manapi::async::run(this->taskpool, this->close());
}

manapi::future<ssize_t> manapi::filesystem::async::fstream::read(void *buff, ssize_t buff_size) {
    if (this->status & FILE_READ) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_THREAD_SAFE, "that function isn't thread safe");
    }

    while (true) {
        auto rhs = static_cast<ssize_t> (::read(this->fd, buff, buff_size));

        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await manapi::async::promise<void> (this->taskpool, [this] (manapi::async::promise<void>::resolve_t resolve, manapi::async::promise<void>::reject_t reject) -> future<> {
                    this->r_resolve = std::move(resolve);
                    this->status.fetch_or(FILE_READ);

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

manapi::future<ssize_t> manapi::filesystem::async::fstream::write(const void *buff, ssize_t buff_size) {
    if (this->status & FILE_WRITE) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_THREAD_SAFE, "that function isn't thread safe");
    }

    while (true) {
        auto rhs = static_cast<ssize_t> (::write(this->fd, buff, buff_size));

        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await manapi::async::promise<void> (this->taskpool, [this] (manapi::async::promise<void>::resolve_t resolve, manapi::async::promise<void>::reject_t reject) -> future<> {
                    this->w_resolve = std::move(resolve);
                    this->status.fetch_or(FILE_WRITE);

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

manapi::future<> manapi::filesystem::async::fstream::fwrite(const void *buff, ssize_t buff_size) {
    while (buff_size > 0) {
        auto rhs = co_await this->write(buff, buff_size);

        if (rhs <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to write to a file. code: {}", rhs);
        }

        buff_size -= rhs;
    }
}

manapi::future<> manapi::filesystem::async::fstream::close() {
    return fstream::_close(this->eventloop, std::move(this->watcher), std::exchange(this->fd, -1));
}

void manapi::filesystem::async::fstream::seekg(const ssize_t &pos, const seek_flag_t &flag) {
    this->_seekg(pos, flag);
}

ssize_t manapi::filesystem::async::fstream::tellg() const {
    return lseek64(this->fd, 0, FILE_SEEK_CURRENT);
}

ssize_t manapi::filesystem::async::fstream::total_size() const {
    auto current = this->tellg();
    this->_seekg(0, FILE_SEEK_END);
    auto size = this->tellg();
    this->_seekg(current, FILE_SEEK_START);
    return size;
}

bool manapi::filesystem::async::fstream::eof() const {
    auto current = this->tellg();
    this->_seekg(0, FILE_SEEK_END);
    auto size = this->tellg();
    this->_seekg(current, FILE_SEEK_START);
    return current==size;
}

void manapi::filesystem::async::fstream::_seekg(const ssize_t &pos, const seek_flag_t &flag) const {
    lseek64(this->fd, pos, static_cast<int>(flag));
}

void manapi::filesystem::async::fstream::_event(ev::io &w, int revents) {
    if ((revents & ev::READ) && (this->status & FILE_READ)) {
        this->status.fetch_xor(FILE_READ);
        this->r_resolve ();
    }

    if ((revents & ev::WRITE) && (this->status & FILE_WRITE)) {
        this->status.fetch_xor(FILE_WRITE);
        this->w_resolve ();
    }
}

manapi::future<> manapi::filesystem::async::fstream::_close(std::shared_ptr<event_loop> ev, std::shared_ptr<ev::io> w, int fd) {
    if (w) {
        co_await ev->unwatch_fd(std::move(w));
    }

    if (fd >= 0) {
        ::close(fd);
    }
}

