#include "http/Utils.hpp"

#include <cstring>

#include "ManapiFilesystem.hpp"
#include "ManapiHttpConfig.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "async/ManapiAsyncFileStream.hpp"

std::pair<std::string_view, std::string_view> manapi::net::http::parse_header(std::string_view header) {
    std::pair <std::string_view, std::string_view> parsed;
    auto const pos = header.find(':');

    if (std::string::npos == pos) {
        THROW_MANAPIHTTP_EXCEPTION2 (manapi::ERR_INVALID_ARGUMENT, "invalid header: semicolon is missing");
    }

    parsed.first = header.substr(0, pos);
    parsed.second = header.substr(pos + 1);

    if (!parsed.second.empty()
        && parsed.second[0] == ' ') {
        parsed.second = parsed.second.substr(1);
    }

    if (!parsed.second.empty()
        && (*parsed.second.rbegin()) == ' ') {
        parsed.second = parsed.second.substr(0, parsed.second.size() - 1);
    }

    return std::move(parsed);
}

std::string manapi::net::http::stringify_header (const std::pair<std::string, std::string> &header)
{
    return std::move(header.first + ": " + header.second);
}

void manapi::net::http::request_data_clear(request_data_t &data) {
    data.buffer.clear();
    data.headers = {};
    data.http = {};
    data.method = {};
    data.params = {};
    data.uri = {};
    data.path = {};
    data.body_size = 0;
    data.flags = 0;
    data.divided = -1;
}

std::vector <manapi::net::http::header_value_t> manapi::net::http::parse_header_value (std::string_view header_value) {
    enum header_value_parse_states {
        HTTP_HV_FIELD_START = 0,
        HTTP_HV_FIELD,
        HTTP_HV_FIELD_QUOTES,
        HTTP_HV_KEY_START,
        HTTP_HV_KEY,
        HTTP_HV_KEY_QUOTES,
        HTTP_HV_VALUE_START,
        HTTP_HV_VALUE,
        HTTP_HV_VALUE_QUOTES,
        HTTP_HV_SKIP,
        HTTP_HV_SKIP2,
        HTTP_HV_ERR
    };

    enum header_value_parse_flags {
        HTTP_HV_FLAGS_ESCAPED = 1
    };

    std::vector <header_value_t> data;


    int flags = 0;
    std::size_t rhs = 0;
    std::string key{};
    std::string value{};
    header_value_parse_states state{HTTP_HV_FIELD_START}, next{HTTP_HV_ERR};


    finish: while (rhs != header_value.size()) {
        switch (state) {
            case HTTP_HV_FIELD_START: {
                if (header_value[rhs] == '"') {
                    rhs++;
                    state = HTTP_HV_FIELD_QUOTES;
                }
                else
                    state = HTTP_HV_FIELD;
                break;
            }
            case HTTP_HV_FIELD: {
                size_t i = 0;

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == ';') {
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        data.push_back({std::move(key), {}});
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_KEY_START;
                        goto finish;
                    }
                    if (c == ',') {
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        data.push_back({std::move(key), {}});
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_FIELD_START;
                        goto finish;
                    }
                    if (c == '=') {
                        /* it's a key 😲 */
                        data.push_back({{}, {}});
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_VALUE_START;
                        goto finish;
                    }
                }

                if (rhs != i) {
                    key.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_FIELD_QUOTES: {
                size_t i = 0;

                if (flags & HTTP_HV_FLAGS_ESCAPED) {
                    flags ^= HTTP_HV_FLAGS_ESCAPED;
                    key.push_back(header_value[rhs++]);
                }

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == '"') {
                        key.append(header_value.data() + rhs, i - rhs);
                        rhs = (++i);
                        data.push_back({std::move(key), {}});
                        state = HTTP_HV_SKIP;
                        next = HTTP_HV_FIELD_START;
                        goto finish;
                    }
                    if (c == '\\') {
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        flags |= HTTP_HV_FLAGS_ESCAPED;
                        break;
                    }
                }

                if (rhs != i) {
                    key.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_KEY_START: {
                if (header_value[rhs] == '"') {
                    state = HTTP_HV_KEY_QUOTES;
                    rhs++;
                }
                else {
                    state = HTTP_HV_KEY;
                }
                break;
            }
            case HTTP_HV_KEY: {
                size_t i = 0;

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == '=') {
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_VALUE_START;
                        goto finish;
                    }
                }

                if (rhs != i) {
                    key.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_KEY_QUOTES: {
                size_t i = 0;

                if (flags & HTTP_HV_FLAGS_ESCAPED) {
                    flags ^= HTTP_HV_FLAGS_ESCAPED;
                    key.push_back(header_value[rhs++]);
                }

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == '"') {
                        key.append(header_value.data() + rhs, i - rhs);
                        rhs = (++i);
                        state = HTTP_HV_SKIP;
                        next = HTTP_HV_KEY_START;
                        goto finish;
                    }
                    if (c == '\\') {
                        key.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        flags |= HTTP_HV_FLAGS_ESCAPED;
                        break;
                    }
                }

                if (rhs != i) {
                    key.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_VALUE_START: {
                if (header_value[rhs] == '"') {
                    state = HTTP_HV_VALUE_QUOTES;
                    rhs++;
                }
                else {
                    state = HTTP_HV_VALUE;
                }
                break;
            }
            case HTTP_HV_VALUE: {
                size_t i = 0;

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == ';') {
                        value.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_KEY_START;
                        data.rbegin()->params.insert({std::move(key), std::move(value)});
                        goto finish;
                    }
                    if (c == ',') {
                        value.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        state = HTTP_HV_SKIP2;
                        next = HTTP_HV_FIELD_START;
                        data.rbegin()->params.insert({std::move(key), std::move(value)});
                        goto finish;
                    }
                }

                if (rhs != i) {
                    value.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_VALUE_QUOTES: {
                size_t i = 0;

                if (flags & HTTP_HV_FLAGS_ESCAPED) {
                    flags ^= HTTP_HV_FLAGS_ESCAPED;
                    value.push_back(header_value[rhs++]);
                }

                for (i = rhs; i < header_value.size(); i++) {
                    auto &c = header_value[i];
                    if (c == '"') {
                        value.append(header_value.data() + rhs, i - rhs);
                        rhs = (++i);
                        state = HTTP_HV_SKIP;
                        next = HTTP_HV_VALUE_START;
                        data.rbegin()->params.insert({std::move(key), std::move(value)});
                        goto finish;
                    }
                    if (c == '\\') {
                        value.append(header_value.data() + rhs, i - rhs);
                        i++;
                        rhs = i;
                        flags |= HTTP_HV_FLAGS_ESCAPED;
                        break;
                    }
                }

                if (rhs != i) {
                    value.append(header_value.data() + rhs, i - rhs);
                    rhs = i;
                }
                break;
            }
            case HTTP_HV_SKIP: {
                while (rhs != header_value.size()) {
                    switch (header_value[rhs]) {
                        case ' ': rhs++; break;
                        case ',': {
                            if (next != HTTP_HV_FIELD_START
                                && next != HTTP_HV_VALUE_START)
                                goto err;
                            rhs++;
                            state = HTTP_HV_FIELD_START;
                            next = HTTP_HV_ERR;
                            goto finish;
                        }
                        case ';': {
                            if (next != HTTP_HV_VALUE_START
                                && next != HTTP_HV_FIELD_START)
                                goto err;
                            rhs++;
                            state = HTTP_HV_KEY_START;
                            next = HTTP_HV_ERR;
                            goto finish;
                        }
                        case '=': {
                            if (next == HTTP_HV_KEY_START) {
                                rhs++;
                                state = HTTP_HV_VALUE_START;
                                next = HTTP_HV_ERR;
                                goto finish;
                            }
                            if (next == HTTP_HV_FIELD_START) {
                                rhs++;
                                key = std::move(data.rbegin()->value);
                                state = HTTP_HV_VALUE_START;
                                next = HTTP_HV_ERR;
                                goto finish;
                            }

                            goto err;
                        }
                        default: {
                            goto err;
                        }
                    }
                }
                break;
            }
            case HTTP_HV_SKIP2: {
                while (rhs != header_value.size()) {
                    switch (header_value[rhs]) {
                        case ' ': rhs++; break;
                        default: {
                            state = next;
                            next = HTTP_HV_ERR;
                            goto finish;
                        }
                    }
                }
                break;
            }
            default:
                goto err;
        }
    }

    switch (state) {
        case HTTP_HV_FIELD: {
            data.push_back({std::move(key), {}});
            break;
        }
        case HTTP_HV_VALUE: {
            data.rbegin()->params.insert({std::move(key), std::move(value)});
            break;
        }
        case HTTP_HV_SKIP: {
            break;
        }
        default: {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "header value: unexpected end");
        }
    }

    return std::move(data);

err: THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "error was occurred");
}

std::string manapi::net::http::stringify_header_value (const std::vector <header_value_t> &header_value) {
    std::string result;

    if (!header_value.empty()) {
        auto value = header_value.begin();

        // skip ','
        goto point_value;

        for (; value != header_value.end(); ++value)
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

            for (; param != value->params.end(); ++param)
            {
                result += ';';

                point_param:

                result += param->first + '=' + param->second;
            }
        }
    }

    return std::move(result);
}

manapi::future<std::vector<manapi::net::http::replace_founded_item>> manapi::net::http::found_replacers_in_file(const async::shared_cthread &ctx, const std::string &path, const ssize_t &start, const size_t &size, const std::map<std::string, std::string> &replacers) {
    // SPECIAL
    std::string special_key;
    bool opened = false;
    bool special = false;

    std::pair <ssize_t, ssize_t> pos;

    std::vector <replace_founded_item> founded;

    // find replacers
    filesystem::fstream f (path);
    auto res = co_await f.open(ev::FS_O_RDONLY);
    if (!res.ok())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, "Could not open the following file for finding replacers ({})", path);
    }

    f.seekg(start);

    ssize_t fsize = co_await f.size();

    std::string buffer;
    size_t buffer_size = BUFSIZ;
    buffer.reserve(buffer_size);

    while (fsize > 0) {
        auto rhs = co_await f.read (buffer.data(), static_cast<ssize_t>(buffer_size));
        if (rhs <= 0) {
            co_await f.close();
            THROW_MANAPIHTTP_EXCEPTION(ERR_FILESYSTEM_FAILED, "Failed to read the file: {}", path);
        }
        fsize -= rhs;
        for (size_t j = 0; j < rhs; j++) {
            const char &c = *(buffer.data() + j);

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
