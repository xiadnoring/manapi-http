#include <cstdarg>
#include <utility>
#include <memory.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <random>
#include <mutex>
#include <fstream>
#include "ManapiUtils.h"
#include "ManapiFilesystem.h"

static std::random_device   random_dev;
static std::mt19937         random_ng (random_dev());

static std::mutex           log_mutex;

void manapi::toolbox::_log (const size_t &line, const char *file_name, const char *format, ...) {
    std::lock_guard <std::mutex> locker(log_mutex);
    
    va_list args;

    printf("%s(%zu): ", file_name, line);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}

/**
 * slow pow
 * @param x         the number
 * @param count     the power
 * @return
 */
size_t manapi::toolbox::pow (const size_t &x, const size_t &count) {
    size_t result = 1;

    for (size_t i = 0; i < count; i++)
        result *= x;

    return result;
}

bool manapi::toolbox::is_space_symbol (const char &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

/**
 * Fills the string on the right to the specified size of the string
 *
 * @param str   the link to string
 * @param size  the size of the string
 * @param c     the char which will be added at the end of the string
 */
[[maybe_unused]] void manapi::toolbox::rjust (std::string &str, const size_t &size, const char &c) {
    while (str.size() < size)
        str += c;
}

/**
 * Fills the string on the left to the specified size of the string
 *
 * @param str   the link to string
 * @param size  the size of the string
 * @param c     the char which will be added at the start of the string
 */
[[maybe_unused]] void manapi::toolbox::ljust (std::string &str, const size_t &size, const char &c) {
    if (str.size() >= size) return;

    std::string new_string;

    const size_t count = size - str.size();

    while (new_string.size() < count)
        new_string += '0';

    str.insert(0, new_string);
}

void manapi::toolbox::_log_error (const size_t &line, const char *file_name, const std::exception &e, const char *format,...) {
    va_list args;

    printf("%s(%zu): ", file_name, line);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n -> Message: %s\n", e.what());
}

char manapi::toolbox::hex2dec(const char &a) {
    return (char)(a >= 'A' ? a - 'A' + 10 : a - '0');
}


std::string manapi::toolbox::escape_string (const std::string &str) {
    std::string escaped;

    for (const auto &i : str) {
        if (escape_char_need(i)) {

            escaped.push_back('\\');
            escaped.push_back(i);

            continue;
        }

        escaped += i;
    }

    return escaped;
}

bool manapi::toolbox::escape_char_need (const char &ch) {
    return !(std::isalpha(ch) || std::isdigit(ch) || ch == '_');
}

bool manapi::toolbox::valid_special_symbol(const char &c) {
    /**
     * ascii
     * 0 - 128
     */
    return c >= 0;
}

std::string manapi::toolbox::random_string (const size_t &len) {
    const char ptr[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_-";
    const size_t back = sizeof(ptr) - 2;

    std::string result;
    result.resize(len);

    for (size_t i = 0; i < len; i++)
        result[i] = ptr[random(0, back)];

    return result;
}

std::string manapi::toolbox::generate_cache_name (const std::string &file, const std::string &ext) {
    std::string name = manapi::toolbox::filesystem::basename(std::forward<const std::string&> (file));

    name += manapi::toolbox::time ("-%Y_%m_%d_%H_%M_%S-", true) + random_string(25);
    name += '.';
    name += ext;

    return name;
}

size_t      manapi::toolbox::random (const size_t &_min, const size_t &_max) {
    std::uniform_int_distribution<std::mt19937::result_type > dist (_min, _max);

    return dist (random_ng);
}

// ====================[ Classes ]============================

manapi::toolbox::manapi_exception::manapi_exception(std::string message_): message(std::move(message_)) {}

const char *manapi::toolbox::manapi_exception::what() const noexcept {
    return message.data();
}

std::string manapi::toolbox::time (const std::string &fmt, bool local) {
    std::time_t now = std::time(0);
    std::tm *ltm;

    if (local)
        ltm = std::localtime(&now);
    else
        ltm = std::gmtime(&now);

    std::ostringstream oss;
    oss << std::put_time(ltm, fmt.data());

    return oss.str();
}

std::vector <manapi::toolbox::replace_founded_item> manapi::toolbox::found_replacers_in_file (const std::string &path, const size_t &start, const size_t &size, const MAP_STR_STR &replacers) {
#define BUFFER_SIZE 5
    // SPECIAL
    std::string special_key;
    bool opened = false;
    bool special = false;
    bool first_time = true;

    std::pair <ssize_t, ssize_t> pos;

    std::vector <replace_founded_item> founded;

    // find replacers
    std::ifstream f (path);

    if (!f.is_open())
        throw manapi_exception ("cannot open the file for finding replacers!");

    // while i < block_size or opened, bcz replacer can be on some blocks
    // i - index of the char of the packet block
    // j - index of the char of the file
    for (size_t j = start; j < size || special || opened; j++) {
        if (f.eof()) {
            special = false;
            opened = false;
            special_key = "";
            pos = {};

            break;
        }

        if (j == size && first_time)
            first_time = false;

        char c;

        f.read (&c, 1);

        if (opened) {
            if (special) {
                if (c == '}') {
                    // the end of the special string
                    special = false;
                    opened = false;

                    pos.second = j;

                    if (!special_key.empty() && replacers.contains(special_key)) {
                        founded.push_back({
                                                  .key = special_key,
                                                  .value = &replacers.at(special_key),
                                                  .pos = pos
                                          });
                    }

                    special_key = "";
                    pos = {};

                    // it is not first_time
                    if (!first_time)
                        break;

                    continue;
                }

                special_key += '}';
            }

            else if (c == '}') {
                special = true;

                continue;
            }

            special_key += c;

            continue;
        }

        if (special) {
            if (c != '{') {
                special = false;
                pos = {};
                // it is not first_time
                if (!first_time)
                    break;

                continue;
            }

            opened = true;
            special = false;

            continue;
        }

        else if (c == '{') {
            special = true;

            pos.first = j;
        }
    }

    f.close();

    return founded;
}