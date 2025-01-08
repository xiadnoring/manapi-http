#include "http/Utils.hpp"

std::pair<std::string, std::string> manapi::net::http::parse_header(const std::string &header) {
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

    return std::move(parsed);
}

std::string manapi::net::http::stringify_header (const std::pair<std::string, std::string> &header)
{
    return std::move(header.first + ": " + header.second);
}

std::vector <manapi::net::http::header_value_t> manapi::net::http::parse_header_value (const std::string &header_value) {
    std::vector <header_value_t> data;

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

            if (!opened_queues) {
                if (header_value[i] == '=')
                {
                    is_key = false;
                    continue;
                }
                if (header_value[i] == ';' || header_value[i] == ',')
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

    return std::move(data);
}

std::string manapi::net::http::stringify_header_value (const std::vector <header_value_t> &header_value) {
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

    return std::move(result);
}

std::vector<manapi::net::http::replace_founded_item> manapi::net::http::found_replacers_in_file(const std::string &path, const size_t &start, const size_t &size, const std::map<std::string, std::string> &replacers) {
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
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Could not open the following file for finding replacers ({})", path);
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

        if (c == '{') {
            special = true;

            pos.first = j;
        }
    }

    f.close();

    return std::move(founded);
}
