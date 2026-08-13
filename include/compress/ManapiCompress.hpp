#pragma once

#include "../ManapiUtils.hpp"
#include "../std/ManapiSlice.hpp"
#include "../std/ManapiAsyncContext.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::compress {

    class compress_base {
        virtual manapi::status_or<manapi::slice> compress (manapi::slice_view input, bool finish) = 0;
    };

    class decompress_base {
        virtual manapi::status_or<manapi::slice> decompress (manapi::slice_view input, bool finish) = 0;
    };

#if MANAPIHTTP_ZLIB_DEPENDENCY

    future<manapi::status> deflate_compress_file(manapi::ev::file src, manapi::ev::file dest, int level = 0, int strategy = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> deflate_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);

    manapi::status_or<std::string> deflate_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::status_or<std::string> deflate_decompress_string (std::string_view compressed);

    manapi::status_or<std::string> gzip_compress_string (std::string_view original, int level = 0, int strategy = 0);
    manapi::status_or<std::string> gzip_decompress_string (std::string_view compressed);

    future<manapi::status> gzip_compress_file(manapi::ev::file src, manapi::ev::file dest, int level = 0, int strategy = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> gzip_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);

    class deflate_compress : public compress_base {
    protected:
        struct data_t;
    public:
        deflate_compress (int level, int strategy = 0);

        deflate_compress (int method, int window_bits, int mem_level, int level, int strategy);

        ~deflate_compress();

        manapi::status_or<manapi::slice> compress(manapi::slice_view input, bool finish) override;
    protected:
        std::unique_ptr <data_t> m_data;
    };

    class deflate_decompress : public decompress_base {
    protected:
        struct data_t;

    public:
        deflate_decompress (int window_bits);

        deflate_decompress ();

        ~deflate_decompress();

        manapi::status_or<manapi::slice> decompress(manapi::slice_view input, bool finish) override;
    protected:
        std::unique_ptr <data_t> m_data;
    };

    class gzip_compress : public deflate_compress {
    public:
        gzip_compress (int level, int strategy = 0);
    };

    class gzip_decompress : public deflate_decompress {
    public:
        gzip_decompress ();
    };
#endif

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY
    manapi::status_or<std::string> brotli_decompress_string (std::string_view src);
    manapi::status_or<std::string> brotli_compress_string (std::string_view src, uint32_t quality, uint32_t window, uint32_t mode);

    future<manapi::status> brotli_compress_file (manapi::ev::file src, manapi::ev::file dest, uint32_t quality, uint32_t window, uint32_t mode, manapi::ctoken cancellation = nullptr);
    future<manapi::status> brotli_decompress_file (manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    manapi::status_or<std::string> zstd_decompress_string (std::string_view src);
    manapi::status_or<std::string> zstd_compress_string (std::string_view src, int level);

    future<manapi::status> zstd_compress_file (manapi::ev::file src, manapi::ev::file dest, int level, int additional_threads = 0, manapi::ctoken cancellation = nullptr);
    future<manapi::status> zstd_decompress_file (manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);
#endif
}


