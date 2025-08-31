#pragma once

#include "../ManapiUtils.hpp"
#include "../std/ManapiAsyncContext.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::compress {
#if MANAPIHTTP_ZLIB_DEPENDENCY

    DLLExportImport future<manapi::error::status> deflate_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    DLLExportImport future<manapi::error::status> deflate_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);

    DLLExportImport manapi::error::status_or<std::string> deflate_compress_string (std::string_view original, int level = 0, int strategy = 0);
    DLLExportImport manapi::error::status_or<std::string> deflate_decompress_string (std::string_view compressed);

    DLLExportImport manapi::error::status_or<std::string> gzip_compress_string (std::string_view original, int level = 0, int strategy = 0);
    DLLExportImport manapi::error::status_or<std::string> gzip_decompress_string (std::string_view compressed);

    DLLExportImport future<manapi::error::status> gzip_compress_file(std::string src, std::string dest, int level = 0, int strategy = 0, manapi::async::cancellation_action cancellation = nullptr);
    DLLExportImport future<manapi::error::status> gzip_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    DLLExportImport manapi::error::status_or<std::string> brotli_decompress_string (std::string_view src);
    DLLExportImport manapi::error::status_or<std::string> brotli_compress_string (std::string_view src, int quality, int window, int mode);

    DLLExportImport future<manapi::error::status> brotli_compress_file (std::string src, std::string dest, int quality, int window, int mode, manapi::async::cancellation_action cancellation = nullptr);
    DLLExportImport future<manapi::error::status> brotli_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    DLLExportImport manapi::error::status_or<std::string> zstd_decompress_string (std::string_view src);
    DLLExportImport manapi::error::status_or<std::string> zstd_compress_string (std::string_view src, int level);

    DLLExportImport future<manapi::error::status> zstd_compress_file (std::string src, std::string dest, int level, int additional_threads = 0, manapi::async::cancellation_action cancellation = nullptr);
    DLLExportImport future<manapi::error::status> zstd_decompress_file (std::string src, std::string dest, manapi::async::cancellation_action cancellation = nullptr);
#endif
}


