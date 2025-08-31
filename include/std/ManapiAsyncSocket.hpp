#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"
#include "./ManapiCancellation.hpp"

namespace manapi::async {
    DLLExportImport manapi::error::status_or<socket_t> create_socket (int family, int protocol, int socktype) MANAPIHTTP_NOEXCEPT;

    DLLExportImport void close_descriptor (socket_t fd) MANAPIHTTP_NOEXCEPT;

    DLLExportImport void set_non_blocking (socket_t fd) MANAPIHTTP_NOEXCEPT;

    DLLExportImport manapi::future<manapi::sys_error::status_or<int>> custom_ready (int flags, socket_t fd);

    DLLExportImport manapi::future<manapi::sys_error::status> read_ready (socket_t fd);

    DLLExportImport manapi::future<manapi::sys_error::status> write_ready (socket_t fd);

    DLLExportImport manapi::future<manapi::sys_error::status_or<int>> custom_ready (int flags, socket_t fd, cancellation_action cancellation);

    DLLExportImport manapi::future<manapi::sys_error::status> read_ready (socket_t fd, cancellation_action cancellation);

    DLLExportImport manapi::future<manapi::sys_error::status> write_ready (socket_t fd, cancellation_action cancellation);

    DLLExportImport socklen_t socklen (const sockaddr *addr) MANAPIHTTP_NOEXCEPT;
}
