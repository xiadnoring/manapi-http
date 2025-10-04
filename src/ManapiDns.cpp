#include "ManapiDns.hpp"
#include "ManapiEventLoop.hpp"
#include "std/ManapiAsyncPromise.hpp"

manapi::future<int> manapi::dns::getaddrinfo(const char * node, const char* service, const addrinfo *hints, addrinfo **res, async::cancellation_action token) {
    try {
        using promise = manapi::async::promise_sync<int>;
        co_return co_await promise ([&] (promise::resolve_t resolve) -> void {
            manapi::async::current()->eventloop()->create_watcher_getaddrinfo(node, service, hints,
                [resolve = std::move(resolve), &res] (const std::shared_ptr<ev::getaddrinfo> & w, int status, addrinfo *rhs) -> void {
                    if (res)
                        std::swap(*res, rhs);

                    ev::getaddrinfo::free(rhs);

                    resolve(status);
            }, std::move(token));
        });
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "getaddrinfo:Something gets wrong", e.what());
    }
    co_return ev::ERR_AI_FAIL;
}
