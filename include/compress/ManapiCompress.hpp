#ifndef MANAPIHTTP_MANAPICOMPRESS_H
#define MANAPIHTTP_MANAPICOMPRESS_H

#include "../ManapiUtils.hpp"

#ifndef Z_DEFAULT_COMPRESSION
#   define Z_DEFAULT_COMPRESSION 0
#endif
#ifndef Z_DEFAULT_STRATEGY
#   define Z_DEFAULT_STRATEGY 0
#endif

namespace manapi::net::utils::compress {
    typedef std::string (*TEMPLATE_INTERFACE) (const std::string &, const std::string *);

    std::string deflate (const std::string &data, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY, const std::string *folder = nullptr);
    std::string deflate (const std::string &data,const std::string *folder = nullptr);

    bool deflate_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy);
    bool deflate_decompress_file(const std::string &src, const std::string &dest);

    std::string deflate_compress_string (const std::string &original, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY);
    std::string deflate_decompress_string (const std::string &compressed);

    std::string gzip (const std::string &data, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY, const std::string *folder = nullptr);
    std::string gzip (const std::string &data,const std::string *folder = nullptr);

    std::string gzip_compress_string (const std::string &original, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY);
    std::string gzip_decompress_string (const std::string &compressed);

    bool gzip_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy);
    bool gzip_decompress_file(const std::string &src, const std::string &dest);

    void throw_could_not_compress_file  (const std::string &name, const std::string &src, const std::string &dest);
    void throw_could_not_open_file      (const std::string &name, const std::string &path);
    void throw_file_exists              (const std::string &name, const std::string &path);
}

#endif //MANAPIHTTP_MANAPICOMPRESS_H
