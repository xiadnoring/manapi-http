#include <fstream>
#include <format>
#include "ManapiFilesystem.h"
#include "ManapiCompress.h"
#include <zlib.h>

#define CHUNK_SIZE 4096

void manapi::utils::compress::throw_could_not_compress_file (const std::string &name, const std::string &src, const std::string &dest)
{
    THROW_MANAPI_EXCEPTION("Could not compress file with {}. src: {}, dest: {}", name, escape_string(src), escape_string(dest));
}

void manapi::utils::compress::throw_could_not_open_file (const std::string &name, const std::string &path)
{
    THROW_MANAPI_EXCEPTION("{}: Could not open file by location {}", name, escape_string(path));
}

void manapi::utils::compress::throw_file_exists (const std::string &name, const std::string &path) {
    THROW_MANAPI_EXCEPTION("{}: File by following path exists: {}", name, path);
}

std::string manapi::utils::compress::deflate(const std::string &str, const int &level, const int &strategy, const std::string *folder) {
    if (folder != nullptr) {
        std::string dest = *folder + manapi::utils::generate_cache_name(str, "deflate");

        if (!deflate_compress_file(str, dest, level, strategy))
        {
            // throw
            throw_could_not_compress_file("deflate", str, dest);
        }

        return dest;
    }

    // is not file, it is a string!
    std::string buff;
    buff.resize(str.size() * 2);
    size_t  s;

    compress2((Bytef*)(buff.data()), &s, (const Bytef*)str.data(), str.size(), level);

    buff.resize(s);
    return buff;
}

std::string manapi::utils::compress::deflate (const std::string &str, const std::string *folder) {
    return std::move(manapi::utils::compress::deflate(str, Z_BEST_COMPRESSION, Z_BINARY, folder));
}

bool manapi::utils::compress::deflate_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    std::ifstream input (src, std::ios::binary);
    std::ofstream output (dest, std::ios::binary);

    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

    if (!output.is_open())
    {
        throw_could_not_open_file("deflate", dest);
    }

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit(&stream, level) != Z_OK)
    {
        input.close();
        output.close();

        MANAPI_LOG("defalte: {}", "deflateInit(...) failed!");

        return false;
    }

    int flush;
    do {
        input.read(in_buff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = (Byte*)in_buff;

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = (Byte*)out_buff;

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            output.write(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    input.close();
    output.close();
    return true;
}

/* decompress */
bool manapi::utils::compress::deflate_decompress_file(const std::string &src, const std::string &dest)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    std::ifstream input (src, std::ios::binary);
    std::ofstream output (dest, std::ios::binary);

    if (!input.is_open())
    {
        throw_could_not_open_file("deflate", src);
    }

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
        MANAPI_LOG("defalte: {}", "inflateInit(...) failed!");

        input.close();
        output.close();

        return false;
    }

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        if(stream.avail_in == 0)
            break;

        stream.next_in = (Byte*)inbuff;

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = (Byte*)outbuff;
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                MANAPI_LOG("defalte: {}", "inflateInit(...) failed! inflate() = {}", result);
                inflateEnd(&stream);
                return false;
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            output.write(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    input.close();
    output.close();

    return result == Z_STREAM_END;
}

std::string manapi::utils::compress::gzip(const std::string &str, const int &level, const int &strategy, const std::string *folder) {
    if (folder != nullptr) {
        std::string dest = *folder + manapi::utils::generate_cache_name(str, "gzip");

        if (!gzip_compress_file(str, dest, level, strategy))
        {
            throw_could_not_compress_file("gzip", str, dest);
        }

        return dest;
    }

    std::string output;
    output.resize(str.size() * 2);

    z_stream zs;
    zs.zalloc       = Z_NULL;
    zs.zfree        = Z_NULL;
    zs.opaque       = Z_NULL;
    zs.avail_in     = (uInt)    str.size();
    zs.next_in      = (Bytef *) str.data();
    zs.avail_out    = (uInt)    output.size();
    zs.next_out     = (Bytef *) output.data();

    // hard to believe they don't have a macro for gzip encoding, "Add 16" is the best thing zlib can do:
    // "Add 16 to windowBits to write a simple gzip header and trailer around the compressed data instead of a zlib wrapper"
    deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    deflate(&zs, Z_FINISH);
    deflateEnd(&zs);

    output.resize(zs.total_out);

    return output;
}

std::string manapi::utils::compress::gzip (const std::string &str, const std::string *folder) {
    return std::move(gzip(str, Z_BEST_COMPRESSION, Z_BINARY, folder));
}

bool manapi::utils::compress::gzip_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("gzip", dest);
    }

    std::ifstream input (src, std::ios::binary);
    std::ofstream output (dest, std::ios::binary);

    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    if (!output.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 31, 8, strategy) != Z_OK)
    {
        input.close();
        output.close();

        MANAPI_LOG("gzip: {}", "deflateInit(...) failed!");
        return false;
    }

    int flush;
    do {
        input.read(in_buff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        flush           = input.eof() ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in  = (Byte*)in_buff;

        do {
            stream.avail_out    = CHUNK_SIZE;
            stream.next_out     = (Byte*)out_buff;

            deflate(&stream, flush);
            ssize_t bytes = CHUNK_SIZE - stream.avail_out;

            output.write(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);

    input.close();
    output.close();

    return true;
}