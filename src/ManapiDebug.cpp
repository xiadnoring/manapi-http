#include "./include/ManapiDebug.hpp"

void manapi::debug::do_log_(std::string_view file_name, std::string_view func, std::size_t line, err_num errnum, std::string_view data) MANAPIHTTP_NOEXCEPT {
    auto &ctx = manapi::async::internal::current_();
    if (ctx) {
        auto &logger = ctx->logger();
        auto msg = std::format ("{}() ({}:{}): {}", func, file_name, line, data);
        logger->debug(std::move(msg));
    }
    else {
        manapi_log_debug("%.*s() (%.*s:%zu): %.*s", func.size(), func.data(), file_name.size(), file_name.data(), line, data.size(), data.data());
    }
}
