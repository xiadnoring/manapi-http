#include <mutex>
#include <stack>

#include "./include/ManapiUtils.hpp"

#if MANAPIHTTP_CPPTRACE_DEPENDENCY
#   include <cpptrace/cpptrace.hpp>
#endif

#if __cplusplus >= 202302L && MANAPIHTTP_STD_BACKTRACE_DEPENDENCY
#   include <stacktrace>
#endif


void manapi::memory::free(void *p) MANAPIHTTP_NOEXCEPT {
    ::free(p);
}

void *manapi::memory::internal::alloc(std::size_t size) MANAPIHTTP_NOEXCEPT {
    return ::malloc(size);
}

void *manapi::memory::internal::realloc(void *n,std::size_t size) MANAPIHTTP_NOEXCEPT {
    return ::realloc(n, size);
}

void manapi::print_stacktrace() MANAPIHTTP_NOEXCEPT {
    try {
#if MANAPIHTTP_CPPTRACE_DEPENDENCY
        cpptrace::generate_trace().print();
#elif __cplusplus >= 202302L && MANAPIHTTP_STD_BACKTRACE_DEPENDENCY
        auto stack = std::stacktrace::current();
        for (std::size_t i = 0; i < stack.size(); i++) {
            auto &it = stack[i];
            manapi_log_info("#%zu %p in %.*s at %.*s:%u", i, it.native_handle(),
                it.description().size(), it.description().data(),
                it.source_file().size(), it.source_file().data(),
                it.source_line());
        }
#else
        manapi_log_error("stack trace is disabled");
#endif
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stack trace print failed", e.what());
    }
}

void manapi::print_stacktrace(std::size_t pos) MANAPIHTTP_NOEXCEPT {
    try {
#if MANAPIHTTP_CPPTRACE_DEPENDENCY
        auto stack = cpptrace::stacktrace::current(pos);
        auto it = stack.begin();
        manapi_log_info("%p in %.*s at %.*s:%u", it->raw_address,
            it->symbol.size(), it->symbol.data(),
            it->filename.size(), it->filename.data(),
            it->line.value_or(0));
#elif __cplusplus >= 202302L && MANAPIHTTP_STD_BACKTRACE_DEPENDENCY
        auto stack = std::stacktrace::current();
        auto &it = stack[pos];
        manapi_log_info("%p in %.*s at %.*s:%u", it.native_handle(),
            it.description().size(), it.description().data(),
            it.source_file().size(), it.source_file().data(),
            it.source_line());
#else
        manapi_log_error("stack trace is disabled");
#endif
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stack trace print failed", e.what());
    }
}

