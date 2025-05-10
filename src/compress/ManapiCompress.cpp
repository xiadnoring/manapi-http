#include "compress/ManapiCompress.hpp"


#include <fstream>
#include <format>

#include <zlib.h>

#include "ManapiFilesystem.hpp"
#include "compress/ManapiCompress.hpp"
#include "ManapiBeforeDelete.hpp"
#include "ManapiString.hpp"
#include "async/ManapiAsyncFileStream.hpp"

#define CHUNK_SIZE 1024


void manapi::compress::throw_could_not_compress_file (const std::string &name, const std::string &src, const std::string &dest)
{
    THROW_MANAPIHTTP_EXCEPTION(ERR_COMPRESS_DATA, "Could not compress file with {}. src: {}, dest: {}", name, src, dest);
}

void manapi::compress::throw_could_not_open_file (const std::string &name, const std::string &path)
{
    THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "{}: Could not open file by location {}", name, path);
}

void manapi::compress::throw_file_exists (const std::string &name, const std::string &path) {
    THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_EXISTS, "{}: File by following path exists: {}", name, path);
}

#ifdef MANAPIHTTP_BROTLI_DEPENDENCY

#   include <brotli/encode.h>
#   include <brotli/decode.h>

std::string manapi::compress::brotli_decompress_string(std::string_view src) {
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
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "brotli: decompress failed");
}

std::string manapi::compress::brotli_compress_string(std::string_view src, int quality, int window, int mode) {
    std::string output;
    output.resize(src.size() * 2);

    if (mode > 2 || mode < 0) {
        goto err;
    }

    do {
        std::size_t output_size = output.size();
        BROTLI_BOOL rhs = BrotliEncoderCompress(
            quality, window, static_cast<BrotliEncoderMode>(mode), src.size(), reinterpret_cast<const uint8_t *>(src.data()), &output_size, reinterpret_cast<uint8_t *>(output.data()));

        if (!rhs) {
            goto err;
        }

        output.resize(output_size);

        return std::move(output);
    } while (0);
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_COMPRESS_DATA, "brotli: compress failed");
}

manapi::future<void> manapi::compress::brotli_compress_file(std::string src, std::string dest, int quality, int window, int mode, manapi::async::cancellation_action cancellation) {
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("brotli", src);
    }

    co_await output.open(ev::FS_O_CREAT|ev::FS_O_WRONLY);
    if (!output.is_open())
    {
        throw_could_not_open_file("brotli", dest);
    }

    BrotliEncoderState* const cctx = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    if (!cctx) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "brotli: BrotliEncoderCreateInstance(...) failed");
    }

    if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_MODE, mode)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "brotli: couldn't set the mode param");
    }

    if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_QUALITY, quality)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "brotli: couldn't set the quality param");
    }

    if (!BrotliEncoderSetParameter(cctx, BROTLI_PARAM_LGWIN, window)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "brotli: couldn't set the window param");
    }

    std::size_t buffInSize = BUFSIZ, buffOutSize = BUFSIZ;
    uint8_t buffIn[BUFSIZ], buffOut[BUFSIZ];

    std::size_t const toRead = buffInSize;
    for (;;) {
        const uint8_t *buffInNext = buffIn;
        uint8_t* buffOutNext = buffOut;

        buffOutSize = BUFSIZ;

        auto read = co_await input.read(buffIn, static_cast<ssize_t>(toRead));

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
        BrotliEncoderOperation const mode = lastChunk ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;

        do {
            /* Compress into the output buffer and write all of the output to
             * the file so we can reuse the buffer next iteration.
             */
            int const remaining = BrotliEncoderCompressStream(cctx, mode, &buffInSize, &buffInNext, &buffOutSize, &buffOutNext, nullptr);
            if (!remaining) {
                goto err;
            }
            auto written = BUFSIZ - buffOutSize;
            if (written) {
                co_await output.fwrite(buffOut, static_cast<ssize_t>(written));
                buffOutNext = buffOut;
            }
        }
        while (buffInSize);

        if (lastChunk) {
            break;
        }
    }
    BrotliEncoderDestroyInstance(cctx);
    co_return;
err:
    BrotliEncoderDestroyInstance(cctx);
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: compress failed");
}

manapi::future<void> manapi::compress::brotli_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation) {
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("brotli", src);
    }

    co_await output.open(ev::FS_O_CREAT|ev::FS_O_WRONLY);
    if (!output.is_open())
    {
        throw_could_not_open_file("brotli", dest);
    }

    perror("brotli_decompress_file(...) not supported");
}



#endif

#ifdef MANAPIHTTP_ZSTD_DEPENDENCY

#   include <zstd.h>

/**
 * Check the result
 * @param result result
 * @param is_compress compress/uncompress
 * @throws manapi::exception with @code ERR_COMPRESS_DATA@endcode
 */
void zstd_error_check (std::size_t result, bool is_compress) {
    if (auto rhs = ZSTD_isError(result)) {
        throw manapi::exception (manapi::ERR_COMPRESS_DATA, std::format("zstd {}compress failed due to rhs = {}", is_compress ? "" : "un", rhs),
            std::make_unique<manapi::json>(manapi::json{{"rhs", rhs}}));
    }
}

std::string manapi::compress::zstd_decompress_string(std::string_view src) {
    std::string dest;
    dest.resize(ZSTD_compressBound(src.size()));

    std::size_t const size = ZSTD_decompress(dest.data(), dest.size(), src.data(), src.size());

    zstd_error_check(size, false);

    dest.resize(size);
    return std::move(dest);
}

std::string manapi::compress::zstd_compress_string(std::string_view src, int level) {
    std::string dest;
    dest.resize(ZSTD_compressBound(src.size()));

    std::size_t const size = ZSTD_compress(dest.data(), dest.size(), src.data(), src.size(), level);

    zstd_error_check(size, false);

    dest.resize(size);
    return std::move(dest);
}

manapi::future<> manapi::compress::zstd_compress_file(std::string src, std::string dest, int level, int additional_threads, manapi::async::cancellation_action cancellation) {
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("zstd", src);
    }

    co_await output.open(ev::FS_O_CREAT|ev::FS_O_WRONLY);
    if (!output.is_open())
    {
        throw_could_not_open_file("zstd", dest);
    }

    size_t const buffInSize = ZSTD_CStreamInSize();
    size_t const buffOutSize = ZSTD_CStreamOutSize();
    std::string buffIn, buffOut;
    buffIn.reserve(buffInSize);
    buffOut.reserve(buffOutSize);

    ZSTD_CCtx* const cctx = ZSTD_createCCtx();
    if (!cctx) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: ZSTD_createCCtx(...) failed");
    }

    if (!ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: couldn't set the compression level");
    }

    if (!ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: couldn't set the checksum flag");
    }

    if (additional_threads) {
        std::size_t const r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, additional_threads + 1);
        if (ZSTD_isError(r)) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: additional threads aren't supported");
        }
    }

    std::size_t const toRead = buffInSize;
    for (;;) {
        auto read = co_await input.read(buffIn.data(), static_cast<ssize_t>(toRead));
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
            co_await output.fwrite(buffOut.data(), output_buffer.pos);
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
    co_return;
err:
    ZSTD_freeCCtx(cctx);
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: compress failed");
}

manapi::future<> manapi::compress::zstd_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation) {
filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("zstd", src);
    }

    co_await output.open(ev::FS_O_CREAT|ev::FS_O_WRONLY);
    if (!output.is_open())
    {
        throw_could_not_open_file("zstd", dest);
    }

    size_t const buffInSize = ZSTD_CStreamInSize();
    size_t const buffOutSize = ZSTD_CStreamOutSize();
    std::string buffIn, buffOut;
    buffIn.reserve(buffInSize);
    buffOut.reserve(buffOutSize);

    ZSTD_DCtx* const dctx = ZSTD_createDCtx();
    if (!dctx) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: ZSTD_createDCtx(...) failed");
    }
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
        read = co_await input.read(buffIn.data(), toRead);

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
            co_await output.fwrite(buffOut.data(), output_buffer.pos);
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
    co_return;
err:
    ZSTD_freeDCtx(dctx);
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "zstd: decompress failed");
}

#endif

#if MANAPIHTTP_ZLIB_DEPENDENCY

manapi::future<void> manapi::compress::deflate_compress_file(std::string src, std::string dest, int level, int strategy, manapi::async::cancellation_action cancellation) {
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

    co_await output.open(ev::FS_O_CREAT|ev::FS_O_WRONLY);
    if (!output.is_open())
    {
        throw_could_not_open_file("deflate", dest);
    }

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit(&stream, level) != Z_OK)
    {
        MANAPIHTTP_LOG("defalte: {}", "deflateInit(...) failed!");
        goto excep;
    }

    int flush;
    ssize_t rhs;
    do {
        try {
            rhs = co_await input.read(in_buff, CHUNK_SIZE);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("deflate compress failed by {}", e.what());
            goto err;
        }

        if (rhs < 0) {
            goto err;
        }

        if (rhs == 0) {
            flush = Z_FINISH;
        }
        else {
            flush =  Z_NO_FLUSH;
        }

        stream.avail_in = rhs;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            try {
                co_await output.fwrite(out_buff, bytes);
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("deflate compress failed by {}", e.what());
                goto err;
            }
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    co_return;
err:
    deflateEnd(&stream);
excep:
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "deflate compress failed");
}

/* decompress */
manapi::future<void> manapi::compress::deflate_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation){
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

    co_await output.open(ev::FS_O_WRONLY|ev::FS_O_CREAT);
    if (!output.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }


    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];
    z_stream stream = { 0 };

    int result = inflateInit(&stream);
    if(result != Z_OK)
    {
        MANAPIHTTP_LOG("defalte: {}", "inflateInit(...) failed!");
        goto excep;
    }

    ssize_t rhs;

    do {
        try {
            rhs = co_await input.read(inbuff, CHUNK_SIZE);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("deflate decompress failed by {}", e.what());
            goto err;
        }

        if (rhs < 0) {
            goto err;
        }
        if (rhs == 0) {
            break;
        }

        stream.avail_in = rhs;

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                MANAPIHTTP_LOG("deflate(...) failed! deflate() = {}", result);
                goto err;
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;
            try {
                co_await output.fwrite(outbuff, nbytes);
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("deflate decompress failed by {}", e.what());
                goto err;
            }
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        goto excep;
    }

    co_return;
err:
    inflateEnd(&stream);
excep:
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "deflate decompress failed");
}

std::string manapi::compress::deflate_compress_string(std::string_view original, int level, int strategy) {

    std::string buff;
    buff.resize(original.size() * 2);
    uLongf s;

    compress2(reinterpret_cast<Bytef*>(buff.data()), &s, reinterpret_cast<const Bytef*> (original.data()), original.size(), level);

    buff.resize(s);

    return std::move(buff);
}

std::string manapi::compress::deflate_decompress_string(std::string_view compressed) {
    std::stringstream input (compressed.data());
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int result = inflateInit(&stream);
    if(result != Z_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "inflateInit(...) failed!");
    }

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        if(stream.avail_in == 0)
            break;

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                inflateEnd(&stream);
                THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            buff.append(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "result != Z_STREAM_END");
    }

    return std::move(buff);
}

std::string manapi::compress::gzip_compress_string(std::string_view original, int level, int strategy) {
    std::stringstream input (original.data());
    std::string buff;

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "deflateInit(...) failed!");
    }

    int flush;
    do {
        input.read(in_buff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            buff.append(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    return std::move(buff);
}

std::string manapi::compress::gzip_decompress_string(std::string_view compressed) {
    std::stringstream input (compressed.data());
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { 0 };

    int result = inflateInit2(&stream, 15 | 16);
    if(result != Z_OK)
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflateInit(...) failed!");
    }

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        if(stream.avail_in == 0)
            break;

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                inflateEnd(&stream);
                THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            buff.append(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "result != Z_STREAM_END");
    }

    return std::move(buff);
}

manapi::future<void> manapi::compress::gzip_compress_file(std::string src, std::string dest, int level, int strategy, manapi::async::cancellation_action cancellation)
{
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    co_await output.open(ev::FS_O_WRONLY|ev::FS_O_CREAT);
    if (!output.is_open())
    {
        throw_could_not_open_file("gzip", dest);
    }

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
    {
        //MANAPIHTTP_LOG("gzip: {}", "deflateInit(...) failed!");
        goto excep;
    }

    int flush;
    ssize_t rhs;
    do {
        try {
            rhs = co_await input.read(in_buff, CHUNK_SIZE);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("gzip compress failed by {}", e.what());
            goto err;
        }

        if (rhs < 0) {
            goto err;
        }

        if (rhs == 0) {
            flush = Z_FINISH;
        }
        else {
            flush = Z_NO_FLUSH;
        }

        stream.avail_in = rhs;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            try {
                co_await output.fwrite(out_buff, bytes);
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("gzip compress failed by {}", e.what());
                goto err;
            }
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);


    co_return;
err:
    deflateEnd(&stream);
excep:
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "gzip compress failed");
}

manapi::future<void> manapi::compress::gzip_decompress_file(std::string src, std::string dest, manapi::async::cancellation_action cancellation) {
    filesystem::fstream input (src, manapi::async::cancellation_action(cancellation));
    filesystem::fstream output (dest, manapi::async::cancellation_action(cancellation));

    co_await input.open(ev::FS_O_RDONLY);
    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    co_await output.open(ev::FS_O_WRONLY|ev::FS_O_CREAT);
    if (!output.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int result = inflateInit2(&stream, 15 | 16);
    if(result != Z_OK)
    {
        //MANAPIHTTP_LOG("gzip: {}", "inflateInit2(...) failed!");

        goto excep;
    }

    ssize_t rhs;
    do {
        try {
            rhs = co_await input.read(inbuff, CHUNK_SIZE);
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("gzip decompress failed by {}", e.what());
            goto err;
        }
        if (rhs < 0) {
            goto err;
        }
        if (rhs == 0) {
            break;
        }

        stream.avail_in = rhs;

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                inflateEnd(&stream);
                THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;
            try {
                co_await output.fwrite(outbuff, nbytes);
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("gzip decompress failed by {}", e.what());
                goto err;
            }
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        goto excep;
    }

    co_return;
err:
    inflateEnd(&stream);
excep:
    THROW_MANAPIHTTP_EXCEPTION2(ERR_COMPRESS_DATA, "gzip decompress failed");
}

#endif

