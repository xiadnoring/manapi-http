#pragma once

#include "../ManapiUtils.hpp"
#include "../std/ManapiSlice.hpp"
#include "../std/ManapiContext.hpp"
#include "../std/ManapiCancelToken.hpp"

namespace manapi::compress {

    class compress_base {
    public:
        virtual ~compress_base() = default;

        virtual manapi::status_or<manapi::slice> compress (manapi::slice_view input, bool finish) = 0;
    };

    class decompress_base {
    public:
        virtual ~decompress_base() = default;

        virtual manapi::status_or<manapi::slice> decompress (manapi::slice_view input, bool finish) = 0;

        virtual void max_size ( uint64_t max_size ) = 0;
    };

    future<manapi::status> compress_file(compress::compress_base *inst, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);
    future<manapi::status> decompress_file(compress::decompress_base *inst, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation = nullptr);

    manapi::status_or<manapi::slice> compress_string (compress::compress_base *inst, manapi::slice_view original);
    manapi::status_or<manapi::slice> decompress_string (compress::decompress_base *inst, manapi::slice_view compressed);

#if MANAPIHTTP_ZLIB_DEPENDENCY

    class deflate_compress : public compress_base {
    protected:
        struct data_t;
    public:
        deflate_compress (int level, int strategy = 0);

        deflate_compress (int method, int window_bits, int mem_level, int level, int strategy);

        ~deflate_compress() override;

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

        ~deflate_decompress() override;

        manapi::status_or<manapi::slice> decompress(manapi::slice_view input, bool finish) override;

        void max_size(uint64_t max_size) override;
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
    class brotli_compress : public compress_base {
        struct data_t;
    public:
        brotli_compress (uint32_t quality, uint32_t window, uint32_t mode);

        ~brotli_compress() override;

        manapi::status_or<manapi::slice> compress(manapi::slice_view input, bool finish) override;
    private:
        std::unique_ptr< data_t > m_data;
    };

    class brotli_decompress : public decompress_base {
        struct data_t;
    public:
        brotli_decompress ();

        ~brotli_decompress() override;

        manapi::status_or<manapi::slice> decompress(manapi::slice_view input, bool finish) override;

        void max_size(uint64_t max_size) override;
    private:
        std::unique_ptr< data_t > m_data;
    };
#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY
    class zstd_compress : public compress_base {
        struct data_t;
    public:
        zstd_compress (int level, int thrds);

        ~zstd_compress() override;

        manapi::status_or<manapi::slice> compress(manapi::slice_view input, bool finish) override;

    private:
        std::unique_ptr <data_t> m_data;
    };

    class zstd_decompress : public decompress_base {
        struct data_t;
    public:
        zstd_decompress();

        ~zstd_decompress() override;

        manapi::status_or<manapi::slice> decompress(manapi::slice_view input, bool finish) override;

        void max_size(uint64_t max_size) override;
    private:
        std::unique_ptr <data_t> m_data;
    };
#endif
}


