#ifndef MANAPIHTTP_MANAPIFILESYSTEM_H
#define MANAPIHTTP_MANAPIFILESYSTEM_H

#include <filesystem>
#include "ManapiUtils.h"
#include "ManapiJson.h"

namespace manapi::toolbox::filesystem {
    static std::string delimiter (&std::filesystem::path::preferred_separator);

    std::string basename        (const std::string& path);
    bool        exists          (const std::string& path);
    std::string last_time_write (const std::filesystem::path &f,    bool time = false);
    std::string last_time_write (const std::string &path,           bool time = false);
    void        mkdir           (const std::string& path,           bool recursive = true);
    void        append_delimiter(std::string &path);
    ssize_t     get_size        (std::ifstream& f);
    ssize_t     get_size        (const std::string &path);


    void        write           (const std::string &path, const std::string &data);
    std::string read            (const std::string &path);
    void        copy            (std::ifstream &f, const ssize_t &start, const ssize_t &back, std::ofstream &o);

    template <class... Args>
    inline std::string join(const std::string &path, Args&&...args) {
        return (path + (... + (manapi::toolbox::filesystem::delimiter + std::forward<Args>(args))));
    }
}

namespace manapi::toolbox::filesystem::config {
    void write  (const std::string &name, json &data);
    json read   (const std::string &name);
}

#endif //MANAPIHTTP_MANAPIFILESYSTEM_H
