#pragma once

#include "./std/ManapiAsyncContext.hpp"

namespace manapi::dns {

    /**
     * Asynchronous getaddrinfo(3).
     *
     * Either node or service may be NULL but not both.
     *
     * hints is a pointer to a struct addrinfo with additional address type constraints, or NULL.
     * Consult man -s 3 getaddrinfo for more details.
     *
     * @param node Node
     * @param service Service
     * @param hints Hints
     * @param res Result
     * @param token Cancellation token
     * @return
     *
     * Errors:
     * -
     */
    DLLExportImport manapi::future<int> getaddrinfo (const char * node, const char* service, const addrinfo *hints, addrinfo **res, async::cancellation_action token = nullptr);
}