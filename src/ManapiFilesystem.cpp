#include <filesystem>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstdarg>
#include "ManapiFilesystem.hpp"

static const std::string folder_configs;
#define MANAPI_FILESYSTEM_COPY_BUFFER_SIZE 4096LL

std::string manapi::net::filesystem::basename(const std::string& path) {
    size_t pos = path.find_last_of(std::filesystem::path::preferred_separator);

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return path;
}

std::string manapi::net::filesystem::extension(const std::string& path) {
    size_t pos = path.find_last_of('.');

    if (pos != std::string::npos)
    {
        return path.substr(pos + 1);
    }

    return "";
}

bool manapi::net::filesystem::exists(const std::string& path) {
    std::filesystem::path f(path);

    return std::filesystem::exists(f);
}

void manapi::net::filesystem::config::write(const std::string &name, manapi::json &data) {
    manapi::net::filesystem::write(folder_configs + name, data.dump(4));
}

manapi::json manapi::net::filesystem::config::read(const std::string &name) {
    return json (manapi::net::filesystem::read (folder_configs + name), true);
}

std::string manapi::net::filesystem::last_time_write (const std::filesystem::path &f, bool time) {
    std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(f);

    auto tp = last_write_time;
    auto timer = std::chrono::clock_cast<std::chrono::system_clock>(tp);
    auto tmt = std::chrono::system_clock::to_time_t(timer);

    auto tm = std::localtime(&tmt);

    std::stringstream buffer;
    buffer << std::put_time(tm, time ? "%Y-%m-%d-%H-%M-%S" : "%Y-%m-%d");
    return buffer.str();
}

std::string manapi::net::filesystem::last_time_write (const std::string &path, bool time) {
    std::filesystem::path f (path);
    return last_time_write(f, time);
}

void manapi::net::filesystem::mkdir (const std::string &path, bool recursive) {
    if (recursive) {
        std::filesystem::create_directories(path);
        return;
    }

    std::filesystem::create_directory(path);
}

void manapi::net::filesystem::append_delimiter (std::string &path) {
    if (path.empty() || path.back() != std::filesystem::path::preferred_separator)
    {
        path.push_back(std::filesystem::path::preferred_separator);
    }
}

ssize_t manapi::net::filesystem::get_size (std::ifstream& f) {
    f.seekg(0, std::ifstream::end);
    const ssize_t fileSize = f.tellg();
    f.seekg(0, std::ifstream::beg);

    return fileSize;
}

ssize_t manapi::net::filesystem::get_size (const std::string& path) {
    std::ifstream f (path);
    if (!f.is_open())
    {
        THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "cannot open the file by following path: {}", path);
    }

    utils::before_delete close_ifstream ([&f] () { f.close(); });

    const ssize_t result = manapi::net::filesystem::get_size(f);

    return result;
}

void manapi::net::filesystem::write (const std::string &path, const std::string &data) {
    std::ofstream out (path);

    if (!out.is_open())
    {
        THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "cannot open config to write: {}", path);
    }

    utils::before_delete close_ofstream ([&out] () { out.close(); });

    out << data;
}

std::string manapi::net::filesystem::read (const std::string &path) {
    std::ifstream in (path);

    if (!in.is_open())
    {
        THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "cannot open config to read: {}", path);
    }

    utils::before_delete close_ofstream ([&in] () { in.close(); });
    const ssize_t size = get_size(in);

    // 20 MB
    if (size >= 20 * 1024 * 1024) {
        MANAPI_LOG("The size of the file: {} is so large for read with this function.", size);
    }

    std::string content;
    content.resize(size);

    in.read (content.data(), size);

    return content;
}

void manapi::net::filesystem::copy (std::ifstream &f, const ssize_t &start, const ssize_t &back, std::ofstream &o) {
    if (!f.is_open() || !o.is_open()) {
        f.close();
        o.close();

        THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "{}", "cannot open files for operations");
    }

    f.seekg (start);

    ssize_t size        = back - start + 1;

    char buff[MANAPI_FILESYSTEM_COPY_BUFFER_SIZE];

    while (size != 0) {
        const ssize_t block_size = size > MANAPI_FILESYSTEM_COPY_BUFFER_SIZE ? MANAPI_FILESYSTEM_COPY_BUFFER_SIZE : size;
        f.read(buff, block_size);

        o.write(buff, block_size);

        size -= block_size;
    }

    f.seekg(0);
}

std::string manapi::net::filesystem::back (std::string str) {
    size_t size = str.size();

    // clean delimiters at the end
    for (size_t i = size - 1; i > 1; i--) {
        if (delimiter == str[i]) {
            str.pop_back();
            size--;

            continue;
        }

        break;
    }

    bool delimiter_prev = false;
    for (size_t i = size - 1; i != 0; i--) {
        while (i >= 0 && str[i] == delimiter) {
            i--;

            str.pop_back();

            delimiter_prev = true;
        }


        if (delimiter_prev)
        {
            break;
        }

        else
        {
            str.pop_back();
        }
    }

    return str;
}

std::string manapi::net::filesystem::clean (const std::string &str) {
    std::string cleaned;
    size_t size = str.size();

    // clean delimiters at the end
    for (size_t i = size - 1; i > 1; i--) {
        if (delimiter == str[i]) {
            size --;
            continue;
        }

        break;
    }

    // skip double delimiters
    bool delimiter_prev = false;

    for (size_t i = 0; i < size; i++) {
        if (str[i] == delimiter) {
            if (i + 1 != size) {

                // check for . or ..
                if (str[i + 1] == '.') {
                    if (i + 2 != size) {
                        if (str[i + 2] == '/') {
                            i = i + 2 - 1;
                            continue;
                        }
                        else if (str[i + 2] == '.') {
                            bool can_back = cleaned.size() > 1;

                            if (i + 3 != size) {
                                if (str[i + 3] == '/' && can_back) {
                                    cleaned = back (cleaned);
                                    i = i + 3 - 1;

                                    continue;
                                }
                            }
                            else if (can_back) {
                                cleaned = back (cleaned);

                                break;
                            }
                        }
                    }
                    else {
                        break;
                    }
                }
            }

            if (delimiter_prev)
                continue;

            delimiter_prev = true;
        }
        else if (delimiter_prev)
            delimiter_prev = false;

        cleaned += str[i];
    }

    return cleaned;
}

bool manapi::net::filesystem::is_dir (const std::string &str) {
    std::filesystem::path p (str);

    return std::filesystem::is_directory(p);
}

bool manapi::net::filesystem::is_file (const std::string &str) {
    return !is_dir (str);
}