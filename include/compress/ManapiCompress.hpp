#pragma once

#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "../async/ManapiCancellation.hpp"

namespace manapi::compress {
#if MANAPIHTTP_ZLIB_DEPENDENCY

    future<manapi::error::status> deflate_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<manapi::error::status> deflate_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);

    manapi::error::status_or<std::string> deflate_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::error::status_or<std::string> deflate_decompress_string (std::string_view compressed);

    manapi::error::status_or<std::string> gzip_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::error::status_or<std::string> gzip_decompress_string (std::string_view compressed);

    future<manapi::error::status> gzip_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<manapi::error::status> gzip_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    manapi::error::status_or<std::string> brotli_decompress_string (std::string_view src);
    manapi::error::status_or<std::string> brotli_compress_string (std::string_view src, int quality, int window, int mode);

    future<manapi::error::status> brotli_compress_file (std::string src, std::string dest, int quality, int window, int mode, manapi::async::cancellation_action cancellation = nullptr);
    future<manapi::error::status> brotli_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    manapi::error::status_or<std::string> zstd_decompress_string (std::string_view src);
    manapi::error::status_or<std::string> zstd_compress_string (std::string_view src, int level);

    future<manapi::error::status> zstd_compress_file (std::string src, std::string dest, int level, int additional_threads = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<manapi::error::status> zstd_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif
}


