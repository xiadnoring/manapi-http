#ifndef MANAPIHTTP_MANAPICOMPRESS_H
#define MANAPIHTTP_MANAPICOMPRESS_H

#include <zlib.h>
#include "ManapiUtils.h"

namespace manapi::utils::compress {
    typedef std::string (*TEMPLATE_INTERFACE) (const std::string &, const std::string *);

    std::string deflate (const std::string &data, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY, const std::string *folder = nullptr);
    std::string deflate (const std::string &data,const std::string *folder = nullptr);

    bool deflate_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy);
    bool deflate_decompress_file(const std::string &src, const std::string &dest);

    std::string gzip (const std::string &data, const int &level = Z_DEFAULT_COMPRESSION, const int &strategy = Z_DEFAULT_STRATEGY, const std::string *folder = nullptr);
    std::string gzip (const std::string &data,const std::string *folder = nullptr);

    bool gzip_compress_file(const std::string &src, const std::string &dest, const int &level, const int &strategy);
    bool gzip_decompress_file(const std::string &src, const std::string &dest);
}

#endif //MANAPIHTTP_MANAPICOMPRESS_H
