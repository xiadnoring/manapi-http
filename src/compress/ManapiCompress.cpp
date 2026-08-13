#include <fstream>
#include <format>

#include "compress/ManapiCompress.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "../include/ManapiUtils.hpp"

#define CHUNK_SIZE 65536


static manapi::future<manapi::status> manapi__compress_file_init (std::shared_ptr<manapi::fs::fstream> &input, std::shared_ptr<manapi::fs::fstream> &output, manapi::ev::file src, manapi::ev::file dest, manapi::ctoken &cancellation) {
    auto ires = manapi::fs::fstream::create (src, false, manapi::ctoken::unit(cancellation));
    auto ores = manapi::fs::fstream::create (dest, false, manapi::ctoken::unit(cancellation));

    if (!ires)
        co_return ires.err();

    if (!ores)
        co_return ores.err();

    input = ires.unwrap();
    output = ores.unwrap();

//    auto oires = co_await input->open(manapi::ev::FS_O_RDONLY);
//    if (!oires) {
//        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %.*s",
//            "compress:input open", oires.sysmsg().size(), oires.sysmsg().data());
//        co_return std::move(oires);
//    }
//    auto oores = co_await output->open(manapi::ev::FS_O_CREAT|manapi::ev::FS_O_TRUNC|manapi::ev::FS_O_WRONLY,
//        manapi::ev::IRUSR|manapi::ev::IWUSR|manapi::ev::IRGRP);
//    if (!oores) {
//        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %.*s",
//            "compress:output open", oores.sysmsg().size(), oores.sysmsg().data());
//        co_return std::move(oores);
//    }

    co_return manapi::status_ok();
}


#if MANAPIHTTP_BROTLI_DEPENDENCY

#   include <brotli/encode.h>
#   include <brotli/decode.h>

manapi::status_or<std::string> manapi::compress::brotli_decompress_string(std::string_view src) {
    std::string output;
    output.resize(src.size() * 2);
    std::size_t output_size = output.size();
    BROTLI_BOOL rhs = BrotliDecoderDecompress(src.size(), reinterpret_cast<const uint8_t *>(src.data()),
        &output_size, reinterpret_cast<uint8_t *>(output.data()));
    if (!rhs) {
        goto err;
    }
    output.resize(output_size);
    return std::move(output);
err:
    return status_internal("brotli: decompress failed");
}

manapi::status_or<std::string> manapi::compress::brotli_compress_string(std::string_view src, uint32_t quality, uint32_t window, uint32_t mode) {
    std::string output;
    output.resize(src.size() * 2);

    if (mode > 2 || mode < 0) {
        goto err;
    }

    do {
        std::size_t output_size = output.size();
        BROTLI_BOOL rhs = BrotliEncoderCompress(
            static_cast<int>(quality), static_cast<int>(window), static_cast<BrotliEncoderMode>(mode), src.size(), reinterpret_cast<const uint8_t *>(src.data()), &output_size, reinterpret_cast<uint8_t *>(output.data()));

        if (!rhs) {
            goto err;
        }

        output.resize(output_size);

        return std::move(output);
    } while (false);
err:
    return status_internal("brotli: compress failed");
}

manapi::future<manapi::status> manapi::compress::brotli_compress_file(manapi::ev::file src, manapi::ev::file dest, uint32_t quality, uint32_t window, uint32_t mode, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;

    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    BrotliEncoderState* const cctx = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    try {
        if (!cctx) {
            res = manapi::status_internal("brotli: BrotliEncoderCreateInstance(...) failed");
            goto err;
        }

        if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_MODE, mode)) {
            res = manapi::status_internal("brotli: couldn't set the mode param");
            goto err;
        }

        if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_QUALITY, quality)) {
            res = manapi::status_internal("brotli: couldn't set the quality param");
            goto err;
        }

        if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_LGWIN, window)) {
            res = manapi::status_internal("brotli: couldn't set the window param");
            goto err;
        }

        std::size_t buffInSize = CHUNK_SIZE, buffOutSize = CHUNK_SIZE;
        uint8_t buffIn[CHUNK_SIZE], buffOut[CHUNK_SIZE];

        std::size_t const toRead = buffInSize;
        uint8_t* buffOutNext = buffOut;

        for (;;) {
            const uint8_t *buffInNext = buffIn;

            auto read = co_await input->read(buffIn, (toRead));

            if (read < 0) {
                goto err;
            }

            buffInSize = static_cast<std::size_t>(read);

            /* Select the flush mode.
             * If the read may not be finished (read == toRead) we use
             * BROTLI_OPERATION_PROCESS. If this is the last chunk, we use BROTLI_OPERATION_FINISH.
             * brotli optimizes the case where the first flush mode is BROTLI_OPERATION_FINISH,
             * since it knows it is compressing the entire source in one pass.
             */
            int const lastChunk = read == 0;
            BrotliEncoderOperation const mmode = lastChunk ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;

            do {
                /* Compress into the output buffer and write all of the output to
                 * the file so we can reuse the buffer next iteration.
                 */
                int const remaining = BrotliEncoderCompressStream(cctx, mmode, &buffInSize, &buffInNext, &buffOutSize, &buffOutNext, nullptr);
                if (!remaining) {
                    goto err;
                }
                auto written = CHUNK_SIZE - buffOutSize;
                if (!buffOutSize||(mmode==BROTLI_OPERATION_FINISH&&written)) {

                    if (static_cast<ssize_t>(written) != co_await output->fwrite(buffOut, (written))) {
                        goto err;
                    }

                    buffOutNext = buffOut;
                    buffOutSize = CHUNK_SIZE;
                }
            }
            while (buffInSize || (mmode==BROTLI_OPERATION_FINISH&&::BrotliEncoderHasMoreOutput (cctx)));

            if (lastChunk) {
                break;
            }
        }
        BrotliEncoderDestroyInstance(cctx);
        co_return status_ok();
    }
    catch (...) {

    }
err:
    BrotliEncoderDestroyInstance(cctx);
    if (!res.ok())
        co_return status_internal("brotli: compress failed");
    co_return std::move(res);
}

manapi::future<manapi::status> manapi::compress::brotli_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    assert(false && "brotli_decompress_file(...) not supported");
}



#endif

#if MANAPIHTTP_ZSTD_DEPENDENCY

#   include <zstd.h>

// /**
//  * Check the result
//  * @param result result
//  * @param is_compress compress/uncompress
//  * @throws manapi::exception with @code ERR_INTERNAL@endcode
//  */
// void zstd_error_check (std::size_t result, bool is_compress) {
//     if (auto rhs = ZSTD_isError(result)) {
//         throw manapi::exception (manapi::ERR_INTERNAL,
//             std::format("zstd {}compress failed due to rhs = {}", is_compress ? "" : "un", rhs));
//     }
// }

manapi::status_or<std::string> manapi::compress::zstd_decompress_string(std::string_view src) {
    std::string dest;
    dest.resize(ZSTD_compressBound(src.size()));

    std::size_t const size = ZSTD_decompress(dest.data(), dest.size(), src.data(), src.size());

    if (ZSTD_isError(size))
        return status_internal("zstd:Decompress failed");

    dest.resize(size);
    return std::move(dest);
}

manapi::status_or<std::string> manapi::compress::zstd_compress_string(std::string_view src, int level) {
    std::string dest;
    dest.resize(ZSTD_compressBound(src.size()));

    std::size_t const size = ZSTD_compress(dest.data(), dest.size(), src.data(), src.size(), level);

    if (ZSTD_isError(size))
        return status_internal("zstd:Compress failed");

    dest.resize(size);
    return std::move(dest);
}

manapi::future<manapi::status> manapi::compress::zstd_compress_file(manapi::ev::file src, manapi::ev::file dest, int level, int additional_threads, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    size_t const buffInSize = ZSTD_CStreamInSize();
    size_t const buffOutSize = ZSTD_CStreamOutSize();
    std::string buffIn, buffOut;
    buffIn.reserve(buffInSize);
    buffOut.reserve(buffOutSize);

    ZSTD_CCtx* const cctx = ZSTD_createCCtx();
    if (!cctx) {
        res = manapi::status_internal("zstd: ZSTD_createCCtx(...) failed");
        goto err;
    }
    try {
        if (!ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level)) {
            res = manapi::status_internal("zstd: couldn't set the compression level");
            goto err;
        }

        if (!ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1)) {
            res = manapi::status_internal("zstd: couldn't set the checksum flag");
            goto err;
        }

        if (additional_threads) {
            std::size_t const r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, additional_threads + 1);
            if (ZSTD_isError(r)) {
                res = manapi::status_internal("zstd: additional threads aren't supported");
                goto err;
            }
        }

        std::size_t const toRead = buffInSize;
        for (;;) {
            auto read = co_await input->read(buffIn.data(), (toRead));
            if (read < 0) {
                goto err;
            }
            /* Select the flush mode.
             * If the read may not be finished (read == toRead) we use
             * ZSTD_e_continue. If this is the last chunk, we use ZSTD_e_end.
             * Zstd optimizes the case where the first flush mode is ZSTD_e_end,
             * since it knows it is compressing the entire source in one pass.
             */
            int const lastChunk = read == 0;
            ZSTD_EndDirective const mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
            /* Set the input buffer to what we just read.
             * We compress until the input buffer is empty, each time flushing the
             * output.
             */
            ZSTD_inBuffer input_buffer = {buffIn.data(), static_cast<std::size_t>(read), 0};
            int finished;
            do {
                /* Compress into the output buffer and write all of the output to
                 * the file so we can reuse the buffer next iteration.
                 */
                ZSTD_outBuffer output_buffer = {buffOut.data(), buffOutSize, 0};
                std::size_t const remaining = ZSTD_compressStream2(cctx, &output_buffer, &input_buffer, mode);
                if (ZSTD_isError(remaining)) {
                    goto err;
                }
                if (static_cast<ssize_t>(output_buffer.pos) != co_await output->fwrite(buffOut.data(), (output_buffer.pos))) {
                    goto err;
                }
                /* If we're on the last chunk we're finished when zstd returns 0,
                 * which means its consumed all the input AND finished the frame.
                 * Otherwise, we're finished when we've consumed all the input.
                 */
                finished = lastChunk ? (remaining == 0) : (input_buffer.pos == input_buffer.size);
            }
            while (!finished);

            if (lastChunk) {
                break;
            }
        }
        ZSTD_freeCCtx(cctx);
        co_return status_ok();
    }
    catch (...) {

    }
err:
    ZSTD_freeCCtx(cctx);
    if (!res.ok())
        co_return status_internal("zstd: compress failed");
    co_return std::move(res);
}

manapi::future<manapi::status> manapi::compress::zstd_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    size_t const buffInSize = ZSTD_CStreamInSize();
    size_t const buffOutSize = ZSTD_CStreamOutSize();
    std::string buffIn, buffOut;
    buffIn.reserve(buffInSize);
    buffOut.reserve(buffOutSize);

    ZSTD_DCtx* const dctx = ZSTD_createDCtx();
    if (!dctx) {

        res = manapi::status_internal("zstd: ZSTD_createDCtx(...) failed");
        goto err;
    }
    try {
        /* This loop assumes that the input file is one or more concatenated zstd
         * streams. This example won't work if there is trailing non-zstd data at
         * the end, but streaming decompression in general handles this case.
         * ZSTD_decompressStream() returns 0 exactly when the frame is completed,
         * and doesn't consume input after the frame.
         */

        std::size_t const toRead = buffInSize;
        ssize_t read;
        std::size_t lastRet = 0;
        int isEmpty = 1;

        while (true) {
            read = co_await input->read(buffIn.data(),(toRead));

            if (read < 0) {
                goto err;
            }

            if (!read) {
                break;
            }

            isEmpty = 0;
            ZSTD_inBuffer input_buffer = {buffIn.data(), static_cast<std::size_t>(read), 0};
            /* Given a valid frame, zstd won't consume the last byte of the frame
             * until it has flushed all of the decompressed data of the frame.
             * Therefore, instead of checking if the return code is 0, we can
             * decompress just check if input.pos < input.size.
             */
            while (input_buffer.pos < input_buffer.size) {
                ZSTD_outBuffer output_buffer = {buffOut.data(), buffOutSize, 0};
                /* The return code is zero if the frame is complete, but there may
                 * be multiple frames concatenated together. Zstd will automatically
                 * reset the context when a frame is complete. Still, calling
                 * ZSTD_DCtx_reset() can be useful to reset the context to a clean
                 * state, for instance if the last decompression call returned an
                 * error.
                 */
                std::size_t const ret = ZSTD_decompressStream(dctx, &output_buffer, &input_buffer);
                if (ZSTD_isError(ret)) {
                    goto err;
                }
                if (static_cast<ssize_t>(output_buffer.pos) != co_await output->fwrite(buffOut.data(), (output_buffer.pos))) {
                    goto err;
                }
                lastRet = ret;
            }
        }

        if (isEmpty) {
            goto err;
        }

        if (lastRet) {
            /* The last return value from ZSTD_decompressStream did not end on a
             * frame, but we reached the end of the file! We assume this is an
             * error, and the input was truncated.
             */
            goto err;
        }

        ZSTD_freeDCtx(dctx);
        co_return status_ok();
    }
    catch (std::bad_alloc const &) {
        res = status_resource_exhausted("zstd:Bad alloc");
    }
    catch (...) {

    }
err:
    ZSTD_freeDCtx(dctx);
    if (!res.ok())
        co_return status_internal("zstd: decompress failed");
    co_return std::move(res);
}

#endif

#if MANAPIHTTP_ZLIB_DEPENDENCY

#include <zlib.h>

manapi::future<manapi::status> manapi::compress::deflate_compress_file(manapi::ev::file src, manapi::ev::file dest, int level, int strategy, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit(&stream, level) != Z_OK) {
        manapi_log_error("deflate:failed");
        goto excep;
    }

    try {
        int flush;
        ssize_t rhs;
        do {
            rhs = co_await input->read(in_buff, CHUNK_SIZE);

            if (rhs < 0) {
                goto err;
            }

            if (rhs == 0) {
                flush = Z_FINISH;
            }
            else {
                flush =  Z_NO_FLUSH;
            }

            stream.avail_in = static_cast<uint32_t>(rhs);
            stream.next_in  = reinterpret_cast<Byte*>(in_buff);

            do {
                stream.avail_out    = CHUNK_SIZE;
                stream.next_out     = reinterpret_cast<Byte*>(out_buff);

                auto zrhs = deflate(&stream, flush);

                if (zrhs < 0) {
                    deflateEnd(&stream);
                    co_return manapi::status_unknown("deflate:failed");
                }

                assert(CHUNK_SIZE >= stream.avail_out);
                std::size_t bytes = CHUNK_SIZE - stream.avail_out;

                if (static_cast<ssize_t>(bytes) != co_await output->fwrite(out_buff, bytes)) {
                    goto err;
                }

                if (zrhs == Z_STREAM_END)
                    break;

            } while (stream.avail_out == 0);
        } while (flush != Z_FINISH);

        deflateEnd(&stream);

        co_return status_ok();
    }
    catch (...) {

    }
err:
    deflateEnd(&stream);
excep:
    co_return status_internal("deflate:failed");
}

/* decompress */
manapi::future<manapi::status> manapi::compress::deflate_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation){
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);


    int flush = Z_NO_FLUSH;
    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];
    z_stream stream = { nullptr };

    int result = inflateInit(&stream);
    if(result != Z_OK) {
        manapi_log_trace("deflate:failed");
        goto excep;
    }

    try {
        ssize_t rhs;

        do {
            rhs = co_await input->read(inbuff, CHUNK_SIZE);

            if (rhs < 0) {
                goto err;
            }

            if (rhs == 0) {
                flush = Z_FINISH;
            }

            stream.avail_in = static_cast<uint32_t>(rhs);
            stream.next_in = reinterpret_cast<Byte*>(inbuff);

            do {
                stream.avail_out = CHUNK_SIZE;
                stream.next_out = reinterpret_cast<Byte*>(outbuff);
                result = inflate(&stream, flush);

                if(result < 0) {
                    manapi_log_trace("deflate:failed");
                    goto err;
                }

                uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

                if (nbytes != co_await output->fwrite(outbuff, nbytes)) {
                    goto err;
                }

                if (result == Z_STREAM_END)
                    break;

            } while (stream.avail_out == 0);
        } while (flush != Z_FINISH);

        inflateEnd(&stream);

        co_return status_ok();
    }
    catch (...) {

    }
err:
    inflateEnd(&stream);
excep:
    co_return status_internal("deflate:failed");
}

manapi::status_or<std::string> manapi::compress::deflate_compress_string(std::string_view original, int level, int strategy) {

    std::string buff;
    buff.resize(original.size() * 2);
    uLongf s;

    compress2(reinterpret_cast<Bytef*>(buff.data()), &s, reinterpret_cast<const Bytef*> (original.data()), static_cast<unsigned long>(original.size()), level);

    buff.resize(s);

    return std::move(buff);
}

manapi::status_or<std::string> manapi::compress::deflate_decompress_string(std::string_view compressed) {
    std::stringstream input (compressed.data());
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int result = inflateInit(&stream);
    int flush = Z_NO_FLUSH;

    if(result != Z_OK) {
        return status_internal("deflate:failed");
    }

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = static_cast<uint32_t>(input.gcount());

        if(stream.avail_in == 0) {
            flush = Z_FINISH;
        }

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, flush);
            if(result < 0) {
                inflateEnd(&stream);
                return status_internal("deflate:failed");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            buff.append(outbuff, nbytes);

            if (result == Z_STREAM_END)
                break;

        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    inflateEnd(&stream);

    return std::move(buff);
}

manapi::status_or<std::string> manapi::compress::gzip_compress_string(std::string_view original, int level, int strategy) {
    std::stringstream input (original.data());
    std::string buff;

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
        return status_internal ("gzip:failed");

    int flush;
    do {
        input.read(in_buff, CHUNK_SIZE);

        stream.avail_in = static_cast<uint32_t>(input.gcount());

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            auto zres = deflate(&stream, flush);
            if (zres < 0) {
                deflateEnd (&stream);
                return manapi::status_unknown("gzip:failed");
            }
            assert(CHUNK_SIZE >= stream.avail_out);
            std::size_t bytes = CHUNK_SIZE - stream.avail_out;
            buff.append(out_buff, bytes);

            if (zres == Z_STREAM_END)
                break;

        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    return std::move(buff);
}

manapi::status_or<std::string> manapi::compress::gzip_decompress_string(std::string_view compressed) {
    std::stringstream input (compressed.data());
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int flush = Z_NO_FLUSH;
    int result = inflateInit2(&stream, 15 | 16);
    if(result != Z_OK)
        return status_internal ("gzip:failed");

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = static_cast<uint32_t>(input.gcount());

        if(stream.avail_in == 0) {
            flush = Z_FINISH;
        }

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, flush);
            if(result < 0) {
                inflateEnd(&stream);
                return status_internal ("gzip:failed");
            }
            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;
            buff.append(outbuff, nbytes);
            if (result == Z_STREAM_END)
                break;

        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    inflateEnd(&stream);

    return std::move(buff);
}

manapi::future<manapi::status> manapi::compress::gzip_compress_file(manapi::ev::file src, manapi::ev::file dest, int level, int strategy, manapi::ctoken cancellation)
{
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, (src), (dest), cancellation);
    if (!res)
        co_return std::move(res);

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
    {
        manapi_log_error("gzip:failed");
        goto excep;
    }
    try {
        int flush;
        ssize_t rhs;
        do {
            rhs = co_await input->read(in_buff, CHUNK_SIZE);

            if (rhs < 0) {
                goto err;
            }

            if (rhs == 0) {
                flush = Z_FINISH;
            }
            else {
                flush = Z_NO_FLUSH;
            }

            stream.avail_in = static_cast<uint32_t>(rhs);
            stream.next_in  = reinterpret_cast<Byte*>(in_buff);

            do {
                stream.avail_out    = CHUNK_SIZE;
                stream.next_out     = reinterpret_cast<Byte*>(out_buff);

                auto zres = deflate(&stream, flush); assert(CHUNK_SIZE >= stream.avail_out);
                if (zres < 0) {
                    goto err;
                }

                std::size_t bytes = CHUNK_SIZE - stream.avail_out;

                if (static_cast<ssize_t>(bytes) != co_await output->fwrite(out_buff, bytes)) {
                    goto err;
                }

                if (zres == Z_STREAM_END)
                    break;
            } while (stream.avail_out == 0);
        } while (flush != Z_FINISH);

        deflateEnd(&stream);

        co_return status_ok();
    }
    catch (...) {

    }
err:
    deflateEnd(&stream);
excep:
    co_return status_internal("gzip compress failed");
}

manapi::future<manapi::status> manapi::compress::gzip_decompress_file(manapi::ev::file src, manapi::ev::file dest, manapi::ctoken cancellation) {
    std::shared_ptr< manapi::fs::fstream  > input, output;
    auto res = co_await manapi__compress_file_init (input, output, std::move(src), std::move(dest), cancellation);
    if (!res)
        co_return std::move(res);

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int result = inflateInit2(&stream, 15 | 16);
    int flush = Z_NO_FLUSH;
    if(result != Z_OK) {
        goto excep;
    }

    try {
        ssize_t rhs;
        do {
            rhs = co_await input->read(inbuff, CHUNK_SIZE);

            if (rhs < 0) {
                goto err;
            }

            if (rhs == 0) {
                flush = Z_FINISH;
            }

            stream.avail_in = static_cast<uint32_t>(rhs);

            stream.next_in = reinterpret_cast<Byte*>(inbuff);

            do {
                stream.avail_out = CHUNK_SIZE;
                stream.next_out = reinterpret_cast<Byte*>(outbuff);
                result = inflate(&stream, flush);
                if(result < 0) {
                    goto err;
                }

                uint32_t nbytes = CHUNK_SIZE - stream.avail_out;
                if (nbytes != co_await output->fwrite(outbuff, nbytes)) {
                    goto err;
                }

                if (result == Z_STREAM_END)
                    break;
            } while (stream.avail_out == 0);
        } while (flush != Z_FINISH);

        inflateEnd(&stream);

        co_return status_ok();
    }
    catch (...) {

    }
err:
    inflateEnd(&stream);
excep:
    co_return status_internal("gzip decompress failed");
}

struct manapi::compress::deflate_compress::data_t {
    z_stream stream = { nullptr };
    manapi::bytebuffer buffer;
    std::size_t cursor;
};

manapi::compress::deflate_compress::deflate_compress(int level, int strategy) : m_data(std::make_unique<manapi::compress::deflate_compress::data_t>()) {
    this->init(level, strategy);
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

            if (rhs < 0) {
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

void manapi::compress::deflate_compress::init(int level, int strategy) {
    if(deflateInit(&this->m_data->stream, level) != Z_OK) {
        throw manapi::exception ( ERR_UNKNOWN,  "deflate_compress:deflateInit failed");
    }
}

struct manapi::compress::deflate_decompress::data_t {
    z_stream stream = { nullptr };
    manapi::bytebuffer buffer;
    std::size_t cursor;
};

manapi::compress::deflate_decompress::deflate_decompress() : m_data(std::make_unique<data_t>()) {
    this->init();
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

            if (rhs < 0) {
                return manapi::status_unknown("deflate_decompress:failed");
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

void manapi::compress::deflate_decompress::init() {
    if(inflateInit(&this->m_data->stream) != Z_OK) {
        throw manapi::exception ( ERR_UNKNOWN,  "deflate_decompress:inflateInit failed");
    }
}

manapi::compress::gzip_compress::gzip_compress(int level, int strategy) : deflate_compress(level, strategy) {
}

void manapi::compress::gzip_compress::init(int level, int strategy) {
    if(deflateInit2(&this->m_data->stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK) {
        throw manapi::exception ( ERR_UNKNOWN,  "gzip_compress:deflateInit2 failed");
    }
}

manapi::compress::gzip_decompress::gzip_decompress() : deflate_decompress () {

}

void manapi::compress::gzip_decompress::init() {
    if(inflateInit2(&this->m_data->stream, 15 | 16) != Z_OK) {
        throw manapi::exception(ERR_UNKNOWN, "gzip_decompress:inflateInit2 failed");
    }
}

#endif