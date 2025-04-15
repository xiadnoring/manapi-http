#include "compress/ManapiCompress.hpp"

#if MANAPIHTTP_ZLIB_DEPENDENCY

#include <fstream>
#include <format>

#include <zlib.h>

#include "ManapiFilesystem.hpp"
#include "compress/ManapiCompress.hpp"
#include "ManapiBeforeDelete.hpp"
#include "ManapiString.hpp"
#include "async/ManapiAsyncFileStream.hpp"

#define CHUNK_SIZE 4096

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


manapi::future<bool> manapi::compress::deflate_compress_file(const std::shared_ptr<async::context> &ctx, const std::string &src, const std::string &dest, int level, int strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    filesystem::fstream input (ctx, src);
    filesystem::fstream output (ctx, dest);

    co_await input.open(manapi::filesystem::fstream::FILE_READ);
    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

    co_await output.open(manapi::filesystem::fstream::FILE_WRITE|manapi::filesystem::fstream::FILE_CREATE);
    if (!output.is_open())
    {
        throw_could_not_open_file("deflate", dest);
    }

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit(&stream, level) != Z_OK)
    {
        MANAPIHTTP_LOG(ctx, "defalte: {}", "deflateInit(...) failed!");

        co_return false;
    }

    int flush;
    do {
        auto rhs = co_await input.read(in_buff, CHUNK_SIZE);
        if (rhs < 0) {
            co_return false;
        }
        if (rhs == 0) {
            break;
        }

        stream.avail_in = rhs;

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            co_await output.fwrite(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    co_return true;
}

/* decompress */
manapi::future<bool> manapi::compress::deflate_decompress_file(const std::shared_ptr<async::context> &ctx, const std::string &src, const std::string &dest)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    filesystem::fstream input (ctx, src);
    filesystem::fstream output (ctx, dest);

    co_await input.open(manapi::filesystem::fstream::FILE_READ);
    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

    co_await output.open(manapi::filesystem::fstream::FILE_WRITE|manapi::filesystem::fstream::FILE_CREATE);
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
        MANAPIHTTP_LOG(ctx, "defalte: {}", "inflateInit(...) failed!");

        co_return false;
    }

    do {
        auto rhs = co_await input.read(inbuff, CHUNK_SIZE);
        if (rhs < 0) {
            co_return false;
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
                MANAPIHTTP_LOG(ctx, "deflate(...) failed! deflate() = {}", result);
                inflateEnd(&stream);
                co_return false;
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            co_await output.fwrite(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    co_return result == Z_STREAM_END;
}

std::string manapi::compress::deflate_compress_string(const std::string &original, int level, int strategy) {

    std::string buff;
    buff.resize(original.size() * 2);
    uLongf s;

    compress2(reinterpret_cast<Bytef*>(buff.data()), &s, reinterpret_cast<const Bytef*> (original.data()), original.size(), level);

    buff.resize(s);

    return std::move(buff);
}

std::string manapi::compress::deflate_decompress_string(const std::string &compressed) {
    std::stringstream input (compressed);
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

    if (result != Z_STREAM_END) { THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "result != Z_STREAM_END"); }

    return std::move(buff);
}

std::string manapi::compress::gzip_compress_string(const std::string &original, int level, int strategy) {
    std::stringstream input (original);
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

std::string manapi::compress::gzip_decompress_string(const std::string &compressed) {
    std::stringstream input (compressed);
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

    if (result != Z_STREAM_END) { THROW_MANAPIHTTP_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "result != Z_STREAM_END"); }

    return std::move(buff);
}

manapi::future<bool> manapi::compress::gzip_compress_file(const std::shared_ptr<async::context> &ctx, const std::string &src, const std::string &dest, int level, int strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("gzip", dest);
    }

    filesystem::fstream input (ctx, src);
    filesystem::fstream output (ctx, dest);

    co_await input.open(manapi::filesystem::fstream::FILE_READ);
    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    co_await output.open(manapi::filesystem::fstream::FILE_WRITE|manapi::filesystem::fstream::FILE_CREATE);
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
        co_return false;
    }

    int flush;
    do {
        auto rhs = co_await input.read(in_buff, CHUNK_SIZE);
        if (rhs < 0) {
            co_return false;
        }
        if (rhs == 0) {
            break;
        }

        stream.avail_in = rhs;

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = reinterpret_cast<Byte*>(in_buff);

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = reinterpret_cast<Byte*>(out_buff);

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            co_await output.fwrite(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);


    co_return true;
}

manapi::future<bool> manapi::compress::gzip_decompress_file(const std::shared_ptr<async::context> &ctx, const std::string &src, const std::string &dest) {
    if (filesystem::exists (dest))
    {
        throw_file_exists ("gzip", dest);
    }

    filesystem::fstream input (ctx, src);
    filesystem::fstream output (ctx, dest);

    co_await input.open(manapi::filesystem::fstream::FILE_READ);
    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    co_await output.open(manapi::filesystem::fstream::FILE_WRITE|manapi::filesystem::fstream::FILE_CREATE);
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

        co_return false;
    }

    do {
        auto rhs = co_await input.read(inbuff, CHUNK_SIZE);
        if (rhs < 0) {
            co_return false;
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

            co_await output.fwrite(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) { co_return false; }

    co_return true;
}

#endif