#include <manapihttp/ManapiEventLoop.hpp>
#include <manapihttp/ManapiThreadPool.hpp>
#include <manapihttp/std/ManapiAsyncContext.hpp>

int main(int argc, char *argv[]) {
    manapi::async::context::threadpoolfs(4);
    auto blockedsignals = manapi::async::context::blockedsignals();
    auto ctx = manapi::async::context::create(/*threadnum*/ 4).unwrap();

    std::atomic<int> runs;
    ctx->run(4, [&runs](std::function<void()> bind) -> void {
        manapi::async::current()->etaskpool()->append_static_task([&runs]() -> void {
            manapi_log_debug("Hello from Context #%d", runs.fetch_add(1));
        });

        bind();
    });

    return 0;
}