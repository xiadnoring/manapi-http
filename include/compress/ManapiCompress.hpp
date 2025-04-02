#pragma once

#include "../ManapiUtils.hpp"

#if MANAPIHTTP_ZLIB_DEPENDENCY

#include "../ManapiUtils.hpp"
#include "../compress/ManapiCompress.hpp"
#include "../async/ManapiAsyncContext.hpp"

#ifndef Z_DEFAULT_COMPRESSION
#   define Z_DEFAULT_COMPRESSION 0
#endif
#ifndef Z_DEFAULT_STRATEGY
#   define Z_DEFAULT_STRATEGY 0
#endif

namespace manapi::compress {
    future<bool> deflate_compress_file(const std::shared_ptr<manapi::async::context> &ctx, const std::string &src, const std::string &dest, int level = Z_DEFAULT_COMPRESSION, int strategy = Z_DEFAULT_STRATEGY);
    future<bool> deflate_decompress_file(const std::shared_ptr<manapi::async::context> &ctx, const std::string &src, const std::string &dest);

    std::string deflate_compress_string (const std::string &original, int level = Z_DEFAULT_COMPRESSION, int strategy = Z_DEFAULT_STRATEGY);
    std::string deflate_decompress_string (const std::string &compressed);

    std::string gzip_compress_string (const std::string &original, int level = Z_DEFAULT_COMPRESSION, int strategy = Z_DEFAULT_STRATEGY);
    std::string gzip_decompress_string (const std::string &compressed);

    future<bool> gzip_compress_file(const std::shared_ptr<manapi::async::context> &ctx, const std::string &src, const std::string &dest, int level = Z_DEFAULT_COMPRESSION, int strategy = Z_DEFAULT_STRATEGY);
    future<bool> gzip_decompress_file(const std::shared_ptr<manapi::async::context> &ctx, const std::string &src, const std::string &dest);

    void throw_could_not_compress_file (const std::string &name, const std::string &src, const std::string &dest);
    void throw_could_not_open_file (const std::string &name, const std::string &path);
    void throw_file_exists (const std::string &name, const std::string &path);
}

#endif