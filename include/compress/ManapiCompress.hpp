#pragma once

#include "../ManapiUtils.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "async/ManapiCancellation.hpp"

namespace manapi::compress {
#if MANAPIHTTP_ZLIB_DEPENDENCY

    future<void> deflate_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<void> deflate_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);

    std::string deflate_compress_string (std::string_view original, int level = 0, int strategy = 0);
    std::string deflate_decompress_string (std::string_view compressed);

    std::string gzip_compress_string (std::string_view original, int level = 0, int strategy = 0);
    std::string gzip_decompress_string (std::string_view compressed);

    future<void> gzip_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<void> gzip_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);

    void throw_could_not_compress_file (const std::string &name, const std::string &src, const std::string &dest);
    void throw_could_not_open_file (const std::string &name, const std::string &path);
    void throw_file_exists (const std::string &name, const std::string &path);
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    std::string brotli_decompress_string (std::string_view src);
    std::string brotli_compress_string (std::string_view src, int quality, int window, int mode);

    future<void> brotli_compress_file (std::string src, std::string dest, int quality, int window, int mode, manapi::async::cancellation_action cancellation = nullptr);
    future<void> brotli_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    std::string zstd_decompress_string (std::string_view src);
    std::string zstd_compress_string (std::string_view src, int level);

    future<void> zstd_compress_file (std::string src, std::string dest, int level, int additional_threads = 0, manapi::async::cancellation_action cancellation = nullptr);
    future<void> zstd_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif
}


