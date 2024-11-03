#include <fstream>
#include <format>
#include "ManapiFilesystem.hpp"
#include "ManapiCompress.hpp"
#include "ManapiBeforeDelete.hpp"
#include <zlib.h>

#define CHUNK_SIZE 4096

void manapi::net::utils::compress::throw_could_not_compress_file (const std::string &name, const std::string &src, const std::string &dest)
{
    THROW_MANAPI_EXCEPTION(ERR_COMPRESS_DATA, "Could not compress file with {}. src: {}, dest: {}", name, escape_string(src), escape_string(dest));
}

void manapi::net::utils::compress::throw_could_not_open_file (const std::string &name, const std::string &path)
{
    THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "{}: Could not open file by location {}", name, escape_string(path));
}

void manapi::net::utils::compress::throw_file_exists (const std::string &name, const std::string &path) {
    THROW_MANAPI_EXCEPTION(ERR_FILE_EXISTS, "{}: File by following path exists: {}", name, path);
}

std::string manapi::net::utils::compress::deflate(const std::string &str, const int &level, const int &strategy, const std::string *folder) {
    if (folder != nullptr) {
        std::string dest = *folder + manapi::net::utils::generate_cache_name(str, "deflate");

        if (!deflate_compress_file(str, dest, level, strategy))
        {
            // throw
            throw_could_not_compress_file("deflate", str, dest);
        }

        return std::move(dest);
    }

    return std::move (deflate_compress_string (str, level, strategy));
}

std::string manapi::net::utils::compress::deflate (const std::string &str, const std::string *folder) {
    return std::move(manapi::net::utils::compress::deflate(str, Z_BEST_COMPRESSION, Z_BINARY, folder));
}

bool manapi::net::utils::compress::deflate_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    std::ifstream input (src, std::ios::binary | std::ios::in);
    std::ofstream output (dest, std::ios::binary | std::ios::out);

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
        MANAPI_LOG("defalte: {}", "deflateInit(...) failed!");

        return false;
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

            output.write(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    input.close();
    output.close();

    deflateEnd(&stream);

    return true;
}

/* decompress */
bool manapi::net::utils::compress::deflate_decompress_file(const std::string &src, const std::string &dest)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("deflate", dest);
    }

    std::ifstream input (src, std::ios::binary | std::ios::in);
    std::ofstream output (dest, std::ios::binary | std::ios::out);

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

        return false;
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
                MANAPI_LOG("defalte: {}", "inflate(...) failed! inflate() = {}", result);
                inflateEnd(&stream);
                return false;
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            output.write(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    input.close();
    output.close();

    inflateEnd(&stream);

    return result == Z_STREAM_END;
}

std::string manapi::net::utils::compress::deflate_compress_string(const std::string &original, const int &level, const int &strategy) {

    std::string buff;
    buff.resize(original.size() * 2);
    size_t  s;

    compress2(reinterpret_cast<Bytef*>(buff.data()), &s, reinterpret_cast<const Bytef*> (original.data()), original.size(), level);

    buff.resize(s);

    return std::move(buff);
}

std::string manapi::net::utils::compress::deflate_decompress_string(const std::string &compressed) {
    std::stringstream input (compressed);
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { nullptr };

    int result = inflateInit(&stream);
    if(result != Z_OK)
    {
        THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "inflateInit(...) failed!");
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
                THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            buff.append(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) { THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "defalte: {}", "result != Z_STREAM_END"); }

    return std::move(buff);
}

std::string manapi::net::utils::compress::gzip(const std::string &str, const int &level, const int &strategy, const std::string *folder) {
    if (folder != nullptr) {
        std::string dest = *folder + manapi::net::utils::generate_cache_name(str, "gzip");

        if (!gzip_compress_file(str, dest, level, strategy))
        {
            throw_could_not_compress_file("gzip", str, dest);
        }

        return dest;
    }
    return std::move(gzip_compress_string (str, level, strategy));
}

std::string manapi::net::utils::compress::gzip (const std::string &str, const std::string *folder) {
    return std::move(gzip(str, Z_BEST_COMPRESSION, Z_BINARY, folder));
}

std::string manapi::net::utils::compress::gzip_compress_string(const std::string &original, const int &level, const int &strategy) {
    std::stringstream input (original);
    std::string buff;

    char in_buff [CHUNK_SIZE];
    char out_buff[CHUNK_SIZE];
    z_stream stream = {nullptr};

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
    {
        THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "deflateInit(...) failed!");
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

std::string manapi::net::utils::compress::gzip_decompress_string(const std::string &compressed) {
    std::stringstream input (compressed);
    std::string buff;
    buff.reserve(compressed.size());

    char inbuff[CHUNK_SIZE];
    char outbuff[CHUNK_SIZE];

    z_stream stream = { 0 };

    int result = inflateInit2(&stream, 15 | 16);
    if(result != Z_OK)
    {
        THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflateInit(...) failed!");
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
                THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            buff.append(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);

    if (result != Z_STREAM_END) { THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "result != Z_STREAM_END"); }

    return std::move(buff);
}

bool manapi::net::utils::compress::gzip_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy)
{
    if (filesystem::exists (dest))
    {
        throw_file_exists ("gzip", dest);
    }

    std::ifstream input (src, std::ios::binary | std::ios::in);
    std::ofstream output (dest, std::ios::binary | std::ios::out);

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

    if(deflateInit2(&stream, level, Z_DEFLATED, 15 | 16, 8, strategy) != Z_OK)
    {
        MANAPI_LOG("gzip: {}", "deflateInit(...) failed!");
        return false;
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

            output.write(out_buff, bytes);
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    input.close();
    output.close();

    deflateEnd(&stream);


    return true;
}

bool manapi::net::utils::compress::gzip_decompress_file(const std::string &src, const std::string &dest) {
    if (filesystem::exists (dest))
    {
        throw_file_exists ("gzip", dest);
    }

    std::ifstream input (src, std::ios::binary | std::ios::in);
    std::ofstream output (dest, std::ios::binary | std::ios::out);

    if (!input.is_open())
    {
        throw_could_not_open_file("gzip", src);
    }

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
        MANAPI_LOG("gzip: {}", "inflateInit2(...) failed!");

        return false;
    }

    do {
        input.read(inbuff, CHUNK_SIZE);

        stream.avail_in = input.gcount();

        if(stream.avail_in == 0) {
            break;
        }

        stream.next_in = reinterpret_cast<Byte*>(inbuff);

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = reinterpret_cast<Byte*>(outbuff);
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
               result == Z_MEM_ERROR)
            {
                inflateEnd(&stream);
                THROW_MANAPI_EXCEPTION (ERR_COMPRESS_DATA, "gzip: {}", "inflate(...) failed!");
            }

            uint32_t nbytes = CHUNK_SIZE - stream.avail_out;

            output.write(outbuff, nbytes);
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    input.close();
    output.close();

    inflateEnd(&stream);

    if (result != Z_STREAM_END) { return false; }

    return true;
}
