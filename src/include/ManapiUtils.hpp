#pragma once

#define NOMINMAX

#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <stack>
#include <queue>
#include <list>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <forward_list>

#include "ManapiInt.hpp"
#include "ManapiParams.hpp"
#include "ManapiDebug.hpp"

#define REQ(_x) manapi::net::http::request &_x
#define RESP(_x) manapi::net::http::response &_x

#define HANDLER(_req, _resp) (REQ(_req), RESP(_resp))

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   define MANAPIHTTP_NONUNIX true
#else
#   define MANAPIHTTP_NONUNIX false
#endif

#define WORKER_MAX_CNT INT_MAX

namespace manapi::sockets {
    enum ip_version {
        IP_VERSION_4 = 4,
        IP_VERSION_6 = 6
    };
}

namespace manapi {
    void print_stacktrace () MANAPIHTTP_NOEXCEPT;
}

namespace manapi::memory {
    namespace internal {
        void *alloc (std::size_t size) MANAPIHTTP_NOEXCEPT;
        void *realloc (void *n, std::size_t size) MANAPIHTTP_NOEXCEPT;
    }

    template<typename T>
    T *alloc (std::size_t size) MANAPIHTTP_NOEXCEPT {
        return (T*)internal::alloc(size);
    }

    void free (void *p) MANAPIHTTP_NOEXCEPT;

    template<typename T>
    T *realloc (T *n, std::size_t size) MANAPIHTTP_NOEXCEPT {
        return (T*)internal::realloc(n, size);
    }
}

#ifdef MANAPIHTTP_DISABLE_TRACE
#   ifdef manapi_log_trace
#       undef manapi_log_trace
#       define manapi_log_trace(...)
#   endif
#endif

#ifdef MANAPIHTTP_DISABLE_TRACE_HARD
#   ifdef manapi_log_trace_hard
#       undef manapi_log_trace_hard
#       define manapi_log_trace_hard(...)
#   endif
#endif