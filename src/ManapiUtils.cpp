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
#include <unicode/utf32.h>
#include <unicode/utf16.h>
#include <unicode/utf8.h>

#include "ManapiUtils.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiHttpTypes.hpp"

static std::random_device   random_dev;
static std::mt19937         random_ng (random_dev());

static std::mutex           log_mutex;

// ============================================================ //
// ===================== [ Math ] ============================= //
// ============================================================ //

size_t      manapi::utils::random (const size_t &_min, const size_t &_max) {
    std::uniform_int_distribution<std::mt19937::result_type > dist (_min, _max);

    return dist (random_ng);
}

/**
 * slow pow
 * @param x         the number
 * @param count     the power
 * @return
 */
size_t manapi::utils::pow (const size_t &x, const size_t &count) {
    size_t result = 1;

    for (size_t i = 0; i < count; i++)
        result *= x;

    return result;
}

// ============================================================ //
// ===================== [ Strings ] ========================== //
// ============================================================ //

/**
 * Fills the string on the right to the specified size of the string
 *
 * @param str   the link to string
 * @param size  the size of the string
 * @param c     the char which will be added at the end of the string
 */
[[maybe_unused]] void manapi::utils::rjust (std::string &str, const size_t &size, const char &c) {
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
[[maybe_unused]] void manapi::utils::ljust (std::string &str, const size_t &size, const char &c) {
    if (str.size() >= size) return;

    std::string new_string;

    const size_t count = size - str.size();

    while (new_string.size() < count)
        new_string += '0';

    str.insert(0, new_string);
}

bool manapi::utils::is_space_symbol (const char &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::utils::is_space_symbol(const unsigned char &symbol) {
    return is_space_symbol(static_cast<char> (symbol));
}

bool manapi::utils::is_space_symbol (const wchar_t &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::utils::is_space_symbol (const char32_t &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

char manapi::utils::hex2dec(const char &a) {
    return (char)(a >= 'A' ? a - 'A' + 10 : a - '0');
}

char manapi::utils::dec2hex(const char &a) {
    return (char)(a >= 10 ? a - 10 + 'A' : a + '0');
}

std::string manapi::utils::escape_string (const std::string &str) {
    std::string escaped;

    for (const auto &i : str) {
        switch (i) {
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                if (escape_char_need(i)) {
                    escaped.push_back('\\');
                    escaped.push_back(i);

                    continue;
                }

                escaped += i;
        }
    }

    return escaped;
}

std::u32string manapi::utils::escape_string (const std::u32string &str) {
    std::u32string escaped;

    for (const auto &i : str) {
        char32_t a;
        bool is_special_char = true;

        switch (i) {
            case '\n':
                a = 'n';
                break;
            case '\r':
                a = 'r';
                break;
            case '\t':
                a = 't';
                break;
            case '\b':
                a = 'b';
                break;
            case '\f':
                a = 'f';
                break;
            default:
                is_special_char = false;

                if (escape_char_need(i)) {
                    escaped.push_back('\\');
                    escaped.push_back(i);

                    continue;
                }

                escaped += i;
        }
        if (is_special_char)
        {
            escaped.push_back('\\');
            escaped.push_back(a);
        }
    }

    return escaped;
}

bool manapi::utils::escape_char_need (const char &ch) {
    // if ch >= 128 -> non-ascii (maybe utf)
    return ch < 127 && ( ch == '"' || ch == '\\');
}

bool manapi::utils::escape_char_need (const wchar_t &ch) {
    return ch == '"' || ch == '\\';
}

bool manapi::utils::escape_char_need (const char32_t &ch) {
    return ch == '"' || ch == '\\';
}

bool manapi::utils::valid_special_symbol(const char &c) {
    /**
     * ascii
     * 0 - 127
     */
    return c >= 0;
}

std::string manapi::utils::random_string (const size_t &len) {
    const char ptr[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_-";
    const size_t back = sizeof(ptr) - 2;

    std::string result;
    result.resize(len);

    for (size_t i = 0; i < len; i++)
        result[i] = ptr[random(0, back)];

    return result;
}

std::string manapi::utils::generate_cache_name (const std::string &file, const std::string &ext) {
    std::string name = manapi::filesystem::basename(std::forward<const std::string&> (file));

    name += manapi::utils::time ("-%Y_%m_%d_%H_%M_%S-", true) + random_string(25);
    name += '.';
    name += ext;

    return name;
}

std::string manapi::utils::encode_url(const std::string &str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (const auto &c: str)
    {
        if (isalnum(c) || c == '_' || c == '-' || c == '.' || c == '~')
        {
            escaped << c;
            continue;
        }

        if (c == ' ')
        {
            escaped << '+';
            continue;
        }

        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int(static_cast<unsigned int> (c));
        escaped << std::nouppercase;
    }

    return escaped.str();
}

std::string manapi::utils::decode_url(const std::string &str) {}

std::string manapi::utils::json2form(const json &obj) {
    if (!obj.is_object())
    {
        THROW_MANAPI_EXCEPTION("{}", "is_object() -> false in json2form(...)");
    }

    std::string data;
    auto it = obj.begin<json::OBJECT>();
    goto loop;

    for (; it != obj.end<json::OBJECT>(); it ++)
    {
        data += '&';
        loop:
        if (it->second.is_string())
        {
            data += encode_url(it->first) + "=" + encode_url(it->second.get<std::string>());
        }
        else
        {
            data += encode_url(it->first) + "=" + encode_url(it->second.dump());
        }

    }
    return std::move(data);
}

const std::string &manapi::utils::mime_by_file_path(const std::string &path) {
    const std::string extension = manapi::filesystem::extension(path);
    manapi::net::mime_by_extension.lock();
    utils::before_delete unlock ([] () -> void { manapi::net::mime_by_extension.unlock(); });

    if (manapi::net::mime_by_extension.contains(extension))
    {
        return manapi::net::mime_by_extension.at(extension);
    }

    return manapi::net::mime_by_extension.at("bin");
}


// ============================================================ //
// ======================== [ Time ] ========================== //
// ============================================================ //

std::string manapi::utils::time (const std::string &fmt, bool local) {
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

std::vector <manapi::utils::replace_founded_item> manapi::utils::found_replacers_in_file
        (const std::string &path, const size_t &start, const size_t &size, const MAP_STR_STR &replacers) {
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
    {
        THROW_MANAPI_EXCEPTION("Could not open the following file for finding replacers ({})", escape_string(path));
    }

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

// ============================================================ //
// ===================== [ Classes ] ========================== //
// ============================================================ //

manapi::utils::exception::exception(std::string message_): message(std::move(message_)) {}

const char *manapi::utils::exception::what() const noexcept {
    return message.data();
}

manapi::utils::before_delete::before_delete(const std::function<void()> &f) {
    this->f = f;
}

manapi::utils::before_delete::~before_delete() {
    if (f != nullptr && autostart)
    {
        f();
    }
}

void manapi::utils::before_delete::call () {
    if (f != nullptr)
    {
        f();
    }

    disable();
}

void manapi::utils::before_delete::disable() {
    autostart = false;
}

void manapi::utils::before_delete::enable() {
    autostart = true;
}


std::pair<std::string, std::string> manapi::utils::parse_header(const std::string &header) {
    std::pair <std::string, std::string> parsed;

    bool is_key = true;

    std::string *ptr = &parsed.first;

    for (auto &c: header) {
        if (ptr->empty() && c == ' ')
            continue;

        if (is_key) {
            if (c == ':') {
                is_key  = false;
                ptr     = &parsed.second;

                continue;
            }

            *ptr += (char) std::tolower(c);

            continue;
        }

        *ptr += c;
    }

    return parsed;
}

std::string manapi::utils::stringify_header (const std::pair<std::string, std::string> &header)
{
    return header.first + ": " + header.second;
}

std::vector <manapi::net::header_value_t> manapi::utils::parse_header_value (const std::string &header_value) {
    std::vector <manapi::net::header_value_t> data;

    bool        opened_queues   = false;
    bool        is_key          = true;
    std::string key;
    std::string value;

    for (size_t i = 0; i <= header_value.size(); i++) {
        // if end -> append to map
        if (i == header_value.size())
        {
            goto p;
        }

        if (header_value[i] == '\\') {
            i++;

            if (i == header_value.size())
            {
                break;
            }
        }

        else
        {
            if (header_value[i] == '"')
            {
                opened_queues = !opened_queues;
                continue;
            }

            else if (!opened_queues) {
                if (header_value[i] == '=')
                {
                    is_key = false;
                    continue;
                }
                else if (header_value[i] == ';' || header_value[i] == ',')
                {
                    p:
                    if (is_key) {
                        data.push_back({key, {}});
                    }

                    else {
                        if (data.empty())
                            data.push_back({});

                        data.back().params.insert({key, value});
                    }

                    is_key  = true;
                    key     = "";
                    value   = "";

                    continue;
                }

                if (header_value[i] == ' ')
                {
                    continue;
                }
            }
        }

        if (is_key)
        {
            key     += header_value[i];
        }

        else
        {
            value   += header_value[i];
        }
    }

    return data;
}

std::string manapi::utils::stringify_header_value (const std::vector <manapi::net::header_value_t> &header_value) {
    std::string result;

    if (!header_value.empty()) {
        auto value = header_value.begin();

        // skip ','
        goto point_value;

        for (; value != header_value.end(); value++)
        {
            result += ',';

            point_value:

            result += value->value;

            auto param = value->params.begin();

            if (value->value.empty())
            {
                // skip ';'
                goto point_param;
            }

            for (; param != value->params.end(); param++)
            {
                result += ';';

                point_param:

                result += param->first + '=' + param->second;
            }
        }
    }

    return result;
}

// ============================================================ //
// ===================== [ Dumps ] ============================ //
// ============================================================ //


// ============================================================ //
// ===================== [ Unicode ] ========================== //
// ============================================================ //

std::string manapi::utils::str16to4 (const std::u16string &str16)
{
    return std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.to_bytes(str16);
}
std::string manapi::utils::str16to4 (const char16_t &str16)
{
    return std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.to_bytes(str16);
}
std::u16string manapi::utils::str4to16 (const std::string &str)
{
    return std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.from_bytes(str);
}

std::string manapi::utils::str32to4 (const std::u32string &str32)
{
    return std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.to_bytes(str32);
}
std::string manapi::utils::str32to4 (const char32_t &str32)
{
    return std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.to_bytes(str32);
}
std::u32string manapi::utils::str4to32 (const std::string &str)
{
    return std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.from_bytes(str);
}