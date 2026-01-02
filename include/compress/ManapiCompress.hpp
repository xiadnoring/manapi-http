#pragma once

#include "../ManapiUtils.hpp"
#include "../std/ManapiAsyncContext.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::compress {
#if MANAPIHTTP_ZLIB_DEPENDENCY

    future<manapi::status> deflate_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> deflate_decompress_file(std::string src, std::string dest, manapi::ctoken cancellation = nullptr);

    manapi::status_or<std::string> deflate_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::status_or<std::string> deflate_decompress_string (std::string_view compressed);

    manapi::status_or<std::string> gzip_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::status_or<std::string> gzip_decompress_string (std::string_view compressed);

    future<manapi::status> gzip_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> gzip_decompress_file(std::string src, std::string dest, manapi::ctoken cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    manapi::status_or<std::string> brotli_decompress_string (std::string_view src);
    manapi::status_or<std::string> brotli_compress_string (std::string_view src, int quality, int window, int mode);

    future<manapi::status> brotli_compress_file (std::string src, std::string dest, int quality, int window, int mode, manapi::ctoken cancellation = nullptr);
    future<manapi::status> brotli_decompress_file (std::string src, std::string dest, manapi::ctoken cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    manapi::status_or<std::string> zstd_decompress_string (std::string_view src);
    manapi::status_or<std::string> zstd_compress_string (std::string_view src, int level);

    future<manapi::status> zstd_compress_file (std::string src, std::string dest, int level, int additional_threads = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> zstd_decompress_file (std::string src, std::string dest, manapi::ctoken cancellation = nullptr);
#endif
}


