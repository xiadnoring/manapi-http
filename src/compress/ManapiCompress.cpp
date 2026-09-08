#include <fstream>
#include <format>

#include "compress/ManapiCompress.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "std/ManapiParallelRun.hpp"
#include "../include/ManapiUtils.hpp"

static manapi::future<manapi::status> manapi__compress_file_init (std::shared_ptr<manapi::fs::fstream> &input, std::shared_ptr<manapi::fs::fstream> &output, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken &cancellation) {
    auto ires = manapi::fs::fstream::create (src, false, manapi::ctoken::unit(cancellation));
    auto ores = manapi::fs::fstream::create (dest, false, manapi::ctoken::unit(cancellation));

    if (!ires) co_return ires.err();

    if (!ores) co_return ores.err();

    input = ires.unwrap();
    output = ores.unwrap();

    co_return manapi::status_ok();
}


#if MANAPIHTTP_BROTLI_DEPENDENCY

#   include <brotli/encode.h>
#   include <brotli/decode.h>


struct brotli_enc_state_deleter {
    void operator () (BrotliEncoderState *ctx) const {
        if (!ctx) return;
        BrotliEncoderDestroyInstance(ctx);
    }
};

struct brotli_dec_state_deleter {
    void operator () (BrotliDecoderState *ctx) const {
        if (!ctx) return;
        BrotliDecoderDestroyInstance(ctx);
    }
};

struct manapi::compress::brotli_compress::data_t {
    std::unique_ptr<BrotliEncoderState, brotli_enc_state_deleter> ctx;
    manapi::bytebuffer buffer;
    std::size_t cursor;
};

manapi::compress::brotli_compress::brotli_compress(uint32_t quality, uint32_t window, uint32_t mode) : m_data (std::make_unique<brotli_compress::data_t> ()) {
    this->m_data->ctx.reset(BrotliEncoderCreateInstance(nullptr, nullptr, nullptr));
    if (!this->m_data->ctx) throw std::bad_alloc ();

    if (!BrotliEncoderSetParameter(this->m_data->ctx.get(), BROTLI_PARAM_MODE, mode)
        || !BrotliEncoderSetParameter(this->m_data->ctx.get(), BROTLI_PARAM_QUALITY, quality)
        || !BrotliEncoderSetParameter(this->m_data->ctx.get(), BROTLI_PARAM_LGWIN, window)) {
        throw std::runtime_error ("BrotliEncoderSetParameter failed");
    }
}

manapi::status_or<manapi::slice> manapi::compress::brotli_compress::compress(manapi::slice_view input, bool finish) {
    BrotliEncoderOperation flush = BROTLI_OPERATION_PROCESS;

    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;

        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        std::size_t avail_in = z.size();
        auto next_in = reinterpret_cast <const uint8_t *>(z.data());

        for (;;) {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out_total = this->m_data->buffer.size() - this->m_data->cursor;

            std::size_t avail_out = avail_out_total;
            auto next_out = reinterpret_cast<uint8_t *>(this->m_data->buffer.data() + this->m_data->cursor);

            if (finish && indx == input.slices_size()) {
                flush = BROTLI_OPERATION_FINISH;
            }

            auto const rhs = BrotliEncoderCompressStream(this->m_data->ctx.get(), flush, &avail_in, &next_in, &avail_out, &next_out, nullptr);

            if (rhs == BROTLI_FALSE) {
                return manapi::status_unknown("brotli_compress:failed");
            }

            std::size_t bytes = avail_out_total - avail_out;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (BrotliEncoderHasMoreOutput (this->m_data->ctx.get()) == BROTLI_FALSE || avail_out != 0)
                break;

        };
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

manapi::compress::brotli_compress::~brotli_compress() = default;

struct manapi::compress::brotli_decompress::data_t {
    std::unique_ptr<BrotliDecoderState, brotli_dec_state_deleter> ctx;
    manapi::bytebuffer buffer;
    std::size_t cursor;
    uint64_t max_size;
};

manapi::compress::brotli_decompress::brotli_decompress() : m_data (std::make_unique<brotli_decompress::data_t> ()) {
    this->m_data->ctx.reset(BrotliDecoderCreateInstance(nullptr, nullptr, nullptr));
    this->m_data->max_size = std::numeric_limits<uint64_t >::max();
    if (!this->m_data->ctx) throw std::bad_alloc ();
}

manapi::status_or<manapi::slice> manapi::compress::brotli_decompress::decompress(manapi::slice_view input, bool finish) {
    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;
        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        auto avail_in = z.size();
        auto next_in = reinterpret_cast <const uint8_t *>(z.data());

        for (;;) {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out_total = this->m_data->buffer.size() - this->m_data->cursor;

            auto avail_out = avail_out_total;
            auto next_out = reinterpret_cast<uint8_t *>(this->m_data->buffer.data() + this->m_data->cursor);

            auto rhs = BrotliDecoderDecompressStream(this->m_data->ctx.get(), &avail_in, &next_in, &avail_out, &next_out, nullptr);

            if (rhs == BROTLI_DECODER_RESULT_ERROR) {
                return manapi::status_unknown("brotli_decompress:failed");
            }

            std::size_t bytes = avail_out_total - avail_out;

            if (this->m_data->max_size < bytes) {
                return manapi::status_aborted("brotli_decompress:limit is reached");
            }

            this->m_data->max_size -= bytes;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (rhs == BROTLI_DECODER_RESULT_SUCCESS || avail_out != 0)
                break;

        };
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

void manapi::compress::brotli_decompress::max_size(uint64_t max_size) {
    this->m_data->max_size = max_size;
}

manapi::compress::brotli_decompress::~brotli_decompress() = default;

#endif

#if MANAPIHTTP_ZSTD_DEPENDENCY

#   include <zstd.h>

struct zstd_enc_state_deleter {
    void operator () (ZSTD_CCtx *ctx) const {
        if (!ctx) return;
        ZSTD_freeCCtx(ctx);
    }
};

struct zstd_dec_state_deleter {
    void operator () (ZSTD_DCtx *ctx) const {
        if (!ctx) return;
        ZSTD_freeDCtx(ctx);
    }
};

struct manapi::compress::zstd_compress::data_t {
    std::unique_ptr <ZSTD_CCtx, zstd_enc_state_deleter> ctx;
    manapi::bytebuffer buffer;
    std::size_t cursor;
};

manapi::compress::zstd_compress::zstd_compress(int level, int thrds) : m_data (std::make_unique<zstd_compress::data_t> ()) {
    this->m_data->ctx.reset(ZSTD_createCCtx());
    if (!this->m_data->ctx) throw std::bad_alloc ();
    if (ZSTD_isError(ZSTD_CCtx_setParameter(this->m_data->ctx.get(), ZSTD_c_compressionLevel, level))
        || ZSTD_isError(ZSTD_CCtx_setParameter(this->m_data->ctx.get(), ZSTD_c_checksumFlag, 1))
        || (thrds && ZSTD_isError(ZSTD_CCtx_setParameter(this->m_data->ctx.get(), ZSTD_c_nbWorkers, thrds)))) {
        throw std::runtime_error ("zstd_compress:failed");
    }
}

manapi::status_or<manapi::slice> manapi::compress::zstd_compress::compress(manapi::slice_view input, bool finish) {
    ZSTD_EndDirective flush = ZSTD_e_continue;

    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;

        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        ZSTD_inBuffer input_buffer = {z.data(), z.size(), 0};

        for (;;) {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out_total = this->m_data->buffer.size() - this->m_data->cursor;

            if (finish && indx == input.slices_size()) {
                flush = ZSTD_e_end;
            }

            ZSTD_outBuffer output_buffer = {this->m_data->buffer.data() + this->m_data->cursor, avail_out_total, 0};
            auto const rhs = ZSTD_compressStream2(this->m_data->ctx.get(), &output_buffer, &input_buffer, flush);

            if (ZSTD_isError(rhs)) {
                return manapi::status_unknown("zstd_compress:failed");
            }

            std::size_t bytes = output_buffer.pos;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (!rhs || output_buffer.pos != avail_out_total)
                break;

        };
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

manapi::compress::zstd_compress::~zstd_compress() = default;

struct manapi::compress::zstd_decompress::data_t {
    std::unique_ptr <ZSTD_DCtx, zstd_dec_state_deleter> ctx;
    manapi::bytebuffer buffer;
    std::size_t cursor;
    uint64_t max_size;
};

manapi::compress::zstd_decompress::zstd_decompress() : m_data (std::make_unique<zstd_decompress::data_t> ()) {
    this->m_data->ctx.reset(ZSTD_createDCtx());
    this->m_data->max_size = std::numeric_limits<uint64_t >::max();
    if (!this->m_data->ctx) throw std::bad_alloc ();
}

manapi::status_or<manapi::slice> manapi::compress::zstd_decompress::decompress(manapi::slice_view input, bool finish) {
    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;
        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        ZSTD_inBuffer input_buffer = {z.data(), z.size(), 0};

        for (;;) {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out_total = this->m_data->buffer.size() - this->m_data->cursor;

            ZSTD_outBuffer output_buffer = {this->m_data->buffer.data() + this->m_data->cursor, avail_out_total, 0};
            auto const rhs = ZSTD_decompressStream(this->m_data->ctx.get(), &output_buffer, &input_buffer);

            if (ZSTD_isError(rhs)) {
                return manapi::status_unknown("zstd_decompress:failed");
            }

            std::size_t bytes = output_buffer.pos;

            if (this->m_data->max_size < bytes) {
                return manapi::status_aborted("zstd_decompress:limit is reached");
            }

            this->m_data->max_size -= bytes;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (rhs == 0 || output_buffer.pos != avail_out_total)
                break;

        };
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

void manapi::compress::zstd_decompress::max_size(uint64_t max_size) {
    this->m_data->max_size = max_size;
}

manapi::compress::zstd_decompress::~zstd_decompress() = default;

#endif

#if MANAPIHTTP_ZLIB_DEPENDENCY

#include <zlib.h>

struct manapi::compress::deflate_compress::data_t {
    z_stream stream = { nullptr };
    manapi::bytebuffer buffer;
    std::size_t cursor;
};

manapi::compress::deflate_compress::deflate_compress(int method, int window_bits, int mem_level, int level, int strategy)  {
    this->m_data = std::make_unique<data_t>();
    if (deflateInit2(&this->m_data->stream, level, method, window_bits, mem_level, strategy) != Z_OK) {
        throw manapi::exception(ERR_UNKNOWN, "gzip_compress:deflateInit2 failed");
    }
}

manapi::compress::deflate_compress::deflate_compress(int level, int strategy)  {
    this->m_data = std::make_unique<manapi::compress::deflate_compress::data_t>();
    if(deflateInit(&this->m_data->stream, level) != Z_OK) {
        throw manapi::exception ( ERR_UNKNOWN,  "deflate_compress:deflateInit failed");
    }
}

manapi::compress::deflate_compress::~deflate_compress() {
    if (this->m_data) {
        deflateEnd(&this->m_data->stream);
    }
}

manapi::status_or<manapi::slice> manapi::compress::deflate_compress::compress(manapi::slice_view input, bool finish) {
    int flush = Z_NO_FLUSH;

    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;

        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        this->m_data->stream.avail_in = static_cast<uint32_t>(z.size());
        this->m_data->stream.next_in = (Byte *)(z.data());

        do {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out = this->m_data->buffer.size() - this->m_data->cursor;

            this->m_data->stream.avail_out = static_cast<uint32_t>(avail_out);
            this->m_data->stream.next_out = reinterpret_cast<Byte *>(this->m_data->buffer.data() + this->m_data->cursor);

            if (finish && indx == input.slices_size()) {
                flush = Z_FINISH;
            }

            auto rhs = deflate(&this->m_data->stream, flush);

            if (rhs < 0 && rhs != Z_BUF_ERROR) {
                return manapi::status_unknown("deflate_compress:failed");
            }

            std::size_t bytes = avail_out - this->m_data->stream.avail_out;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (rhs == Z_STREAM_END)
                break;

        } while (this->m_data->stream.avail_out == 0);
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

struct manapi::compress::deflate_decompress::data_t {
    z_stream stream = { nullptr };
    manapi::bytebuffer buffer;
    std::size_t cursor;
    uint64_t max_size;
};

manapi::compress::deflate_decompress::deflate_decompress() {
    this->m_data = std::make_unique<data_t>();
    this->m_data->max_size = std::numeric_limits<uint64_t >::max();
    if (inflateInit(&this->m_data->stream) != Z_OK) {
        throw manapi::exception(ERR_UNKNOWN, "deflate_decompress:inflateInit failed");
    }
}

manapi::compress::deflate_decompress::~deflate_decompress() {
    if (this->m_data)
        inflateEnd(&this->m_data->stream);
}

manapi::status_or<manapi::slice> manapi::compress::deflate_decompress::decompress(manapi::slice_view input, bool finish) {
    int flush = Z_NO_FLUSH;

    manapi::slice out;
    std::size_t indx = 0;

    std::string_view z;
    auto it = input.begin();

    if ( it == input.end() ) {
        goto skip;
    }

    goto start;

    for (; it != input.end(); ) {
        it++;
start:
        if (it == input.end())
            break;
        z = std::string_view ( static_cast<char *>(it.buffer()), it.size() );
        indx++;
skip:

        this->m_data->stream.avail_in = static_cast<uint32_t>(z.size());
        this->m_data->stream.next_in = (Byte *)(z.data());

        do {
            if (this->m_data->buffer.empty()) {
                this->m_data->buffer = manapi::async::current()->memory_fabric().buffer(
                        manapi::object_pool::area_size()).unwrap();
                this->m_data->cursor = 0;
            }

            std::size_t avail_out = this->m_data->buffer.size() - this->m_data->cursor;

            this->m_data->stream.avail_out = static_cast<uint32_t>(avail_out);
            this->m_data->stream.next_out = reinterpret_cast<Byte *>(this->m_data->buffer.data() + this->m_data->cursor);

            if (finish && indx == input.slices_size()) {
                flush = Z_FINISH;
            }

            auto rhs = inflate(&this->m_data->stream, flush);

            if (rhs < 0&& rhs != Z_BUF_ERROR) {
                return manapi::status_unknown("deflate_decompress:failed");
            }

            std::size_t bytes = avail_out - this->m_data->stream.avail_out;

            if (this->m_data->max_size < bytes) {
                return manapi::status_aborted("deflate_decompress:limit is reached");
            }

            this->m_data->max_size -= bytes;

            this->m_data->cursor += bytes;

            if (this->m_data->cursor == this->m_data->buffer.size()) {
                out.push_back(std::move(this->m_data->buffer)).unwrap();
            }

            if (rhs == Z_STREAM_END)
                break;

        } while (this->m_data->stream.avail_out == 0);
    }

    if (!this->m_data->buffer.empty()) {
        auto sz = this->m_data->buffer.size();
        out.push_back(std::move(this->m_data->buffer)).unwrap();
        out.resize(out.size() - sz + this->m_data->cursor);
    }

    return std::move(out);
}

manapi::compress::deflate_decompress::deflate_decompress(int window_bits) {
    this->m_data = std::make_unique<data_t>();
    this->m_data->max_size = std::numeric_limits<uint64_t >::max();
    if(inflateInit2(&this->m_data->stream, window_bits) != Z_OK) {
        throw manapi::exception(ERR_UNKNOWN, "gzip_decompress:inflateInit2 failed");
    }

}

void manapi::compress::deflate_decompress::max_size(uint64_t max_size) {
    this->m_data->max_size = max_size;
}

manapi::compress::gzip_compress::gzip_compress(int level, int strategy) : deflate_compress( Z_DEFLATED, 15 | 16, 8, level, strategy) {
}

manapi::compress::gzip_decompress::gzip_decompress() : deflate_decompress (15 | 16) {
}

#endif

manapi::future<manapi::status>
manapi::compress::compress_file(manapi::compress::compress_base *inst, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation) {
    manapi::status st;
    manapi::unwrap(co_await manapi::async::eventloop()->wait_async_task ([&] ( const std::atomic<bool> &is_cancelled ) -> manapi::future<> {
        std::shared_ptr< manapi::fs::fstream  > fin, fout;

        if (! ( st = co_await manapi__compress_file_init (fin, fout, (src), (dest), cancellation) )) {
            co_return;
        }

        auto read_sv = manapi::async::memory_fabric()->slice(manapi::object_pool::area_size() * 16).unwrap();

        st = co_await manapi::async::parallel_wait_get<manapi::status> ([&] (manapi::reference<manapi::async::parallel_t> parallel_st)
                -> manapi::future<manapi::status> {

            bool finish = false;

            auto prun = manapi::async::parallel_run <ssize_t>::create(parallel_st).unwrap();

            ssize_t rhs = co_await fin->read(read_sv);

            while (!finish) {
                if (rhs < 0) {
                    co_return manapi::status_unknown("compress_file:read failed");
                }

                if (!rhs) {
                    finish = true;
                }


                auto write_sv = inst->compress(read_sv.subslice(0, static_cast<std::size_t>(rhs)).unwrap(),
                                               finish).unwrap();

                prun->run(fin->read (read_sv)).unwrap();

                rhs = co_await fout->fwrite(write_sv);

                write_sv.clear();

                if (rhs < 0) {
                    co_return manapi::status_unknown("compress_file:write failed");
                }

                if (!rhs && finish)
                    break;

                rhs = co_await prun->get_or (-1);

                if (rhs < 0) {
                    co_return manapi::status_unknown("compress_file:read failed");
                }
            }
            co_return manapi::status_ok();
        });
    }, std::move(cancellation)));
    co_return std::move(st);
}

manapi::future<manapi::status>
manapi::compress::decompress_file(manapi::compress::decompress_base *inst, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation) {
    manapi::status st;
    manapi::unwrap (co_await manapi::async::eventloop()->wait_async_task([&] (const std::atomic<bool> &is_cancelled) -> manapi::future<> {
        std::shared_ptr< manapi::fs::fstream  > fin, fout;

        if (! ( st = co_await manapi__compress_file_init (fin, fout, (src), (dest), cancellation) ))
            co_return;

        auto read_sv = manapi::async::memory_fabric()->slice(manapi::object_pool::area_size() * 16).unwrap();

        st = co_await manapi::async::parallel_wait_get<manapi::status> ([&] (manapi::reference<manapi::async::parallel_t> parallel_st)
                -> manapi::future<manapi::status> {

            bool finish = false;

            auto prun = manapi::async::parallel_run<ssize_t>::create(parallel_st).unwrap();

            ssize_t rhs = co_await fin->read(read_sv);

            while (!finish) {
                if (rhs < 0) {
                    co_return manapi::status_unknown("decompress_file:read failed");
                }

                if (!rhs) {
                    finish = true;
                }


                auto write_sv = inst->decompress(read_sv.subslice(0, static_cast<std::size_t>(rhs)).unwrap(),
                                                 finish).unwrap();

                prun->run(fin->read(read_sv)).unwrap();

                rhs = co_await fout->fwrite(write_sv);

                write_sv.clear();

                if (rhs < 0) {
                    co_return manapi::status_unknown("decompress_file:write failed");
                }

                if (!rhs && finish)
                    break;

                rhs = co_await prun->get_or (-1);

                if (rhs < 0) {
                    co_return manapi::status_unknown("decompress_file:read failed");
                }
            }

            co_return manapi::status_ok();
        });
    }));
    co_return std::move(st);
}

manapi::status_or<manapi::slice>
manapi::compress::compress_string(manapi::compress::compress_base *inst, manapi::slice_view original) {
    return inst->compress( original, true );
}

manapi::status_or<manapi::slice>
manapi::compress::decompress_string(manapi::compress::decompress_base *inst, manapi::slice_view compressed) {
    return inst->decompress( compressed, true );
}
