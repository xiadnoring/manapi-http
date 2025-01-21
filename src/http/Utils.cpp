#include "http/Utils.hpp"

#include "ManapiFilesystem.hpp"
#include "async/ManapiAsyncFileStream.hpp"

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

void manapi::net::http::request_data_clear(request_data_t &data) {
    data.buffer = {};
    data.headers = {};
    data.http = {};
    data.method = {};
    data.params = {};
    data.uri = {};
    data.path = {};
    data.body_ptr = nullptr;
    data.body_index = 0;
    data.body_left = 0;
    data.body_part = 0;
    data.body_size = 0;
    data.has_body = false;
    data.headers_part = 0;
    data.headers_size = 0;
    data.divided = -1;
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

manapi::future<std::vector<manapi::net::http::replace_founded_item>> manapi::net::http::found_replacers_in_file(const std::shared_ptr<async::context> &ctx, const std::string &path, const ssize_t &start, const size_t &size, const std::map<std::string, std::string> &replacers) {
    // SPECIAL
    std::string special_key;
    bool opened = false;
    bool special = false;

    std::pair <ssize_t, ssize_t> pos;

    std::vector <replace_founded_item> founded;

    // find replacers
    filesystem::async::fstream f (ctx, path);
    co_await f.open(filesystem::async::fstream::FILE_READ);
    if (!f.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Could not open the following file for finding replacers ({})", path);
    }

    f.seekg(start);

    ssize_t fsize = f.total_size();

    std::string buffer;
    buffer.resize(BUFSIZ);

    while (fsize > 0) {
        auto rhs = co_await f.read (buffer.data(), static_cast<ssize_t>(buffer.size()));
        if (rhs <= 0) {
            co_await f.close();
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to read the file: {}", path);
        }
        fsize -= rhs;
        for (size_t j = 0; j < rhs; j++) {
            char &c = buffer[j];

            if (opened) {
                if (special) {
                    if (c == '}') {
                        // the end of the special string
                        special = false;
                        opened = false;

                        pos.second = j;

                        if (!special_key.empty() && replacers.contains(special_key)) {
                            auto value = &replacers.at(special_key);
                            founded.push_back({
                              .key = std::move(special_key),
                              .value = value,
                              .pos = pos
                            });
                        }

                        special_key = "";
                        pos = {};

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
    }

    co_await f.close();

    co_return std::move(founded);
}
