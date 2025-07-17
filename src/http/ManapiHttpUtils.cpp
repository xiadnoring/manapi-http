#include "http/ManapiHttpUtils.hpp"

#include <cstring>

#include "ManapiFilesystem.hpp"
#include "ManapiHttpConfig.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "async/ManapiAsyncFileStream.hpp"
#include "../include/ManapiUtils.hpp"

manapi::error::status_or<std::pair<std::string_view, std::string_view>> manapi::net::http::parse_header(std::string_view header) {
    std::pair <std::string_view, std::string_view> parsed;
    auto const pos = header.find(':');

    if (std::string::npos == pos) {
        return manapi::error::status_invalid_argument("invalid header: semicolon is missing");
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

std::string manapi::net::http::stringify_header (const std::pair<std::string_view, std::string_view> &header) {
    std::string res;
    res.resize(header.first.size() + header.second.size() + (sizeof (": ") - 1));
    res += header.first;
    res += ": ";
    res += header.second;
    return std::move(res);
}

void manapi::net::http::request_data_clear(request_data_t &data) {
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

std::string manapi::net::http::stringify_header_value (const std::vector <header_value_view_t> &header_value) {
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

                result += param->first;
                result += '=';
                result += param->second;
            }
        }
    }

    return std::move(result);
}

int manapi::net::http::version_ip_by_addr(const sockaddr *addr) {
    auto const sn = reinterpret_cast<const sockaddr_in *> (addr);
    if (sn) {
        if (sn->sin_family == ev::IPv4)
            return ev::IPv4;
        if (sn->sin_family == ev::IPv6)
            return ev::IPv6;
    }

    return -1;
}

manapi::error::status_or<std::pair<std::string, uint16_t>> manapi::net::http::strinfigy_ip(const sockaddr *addr) {
    if (!addr)
        return error::status_invalid_argument("ip: null addr");

    auto const sn = reinterpret_cast<const sockaddr_in *> (addr);

    std::string buffer;
    int size;

    if (sn->sin_family == manapi::ev::IPv4) {
        size = sizeof ("xxx:xxx:xxx:xxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET, &sn->sin_addr, buffer.data(), size))
            return error::status_invalid_argument("ip: inet_ntop() returned null");

        while (--size >= 0 && buffer[size] == '\0') {
            /* skip null bytes */
        }

        buffer.resize(size + 1);

        uint16_t const port = (reinterpret_cast<const sockaddr_in *> (&addr)->sin_port);
        return std::make_pair(std::move(buffer), port);
    }

    if (sn->sin_family == manapi::ev::IPv6) {
        size = sizeof ("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr, buffer.data(), size))
            return error::status_invalid_argument("ip: inet_ntop() returned null");

        while (--size >= 0 && buffer[size] == '\0') {
            /* skip null bytes */
        }

        buffer.resize(size + 1);

        uint16_t const port = (reinterpret_cast<const sockaddr_in6 *> (&addr)->sin6_port);
        return std::make_pair(std::move(buffer), port);
    }

    return error::status_invalid_argument("ip: invalid sin_family");
}

manapi::error::status_or<uint16_t> manapi::net::http::port_by_addr(const sockaddr *addr) {
    if (!addr)
        return error::status_invalid_argument("ip: null addr");

    auto const sn = reinterpret_cast<const sockaddr_in *> (addr);

    if (sn->sin_family == manapi::ev::IPv4)
        return (reinterpret_cast<const sockaddr_in *> (&addr)->sin_port);

    if (sn->sin_family == manapi::ev::IPv6)
        return (reinterpret_cast<const sockaddr_in6 *> (&addr)->sin6_port);

    return error::status_invalid_argument("ip: invalid sin_family");
}

manapi::error::status manapi::net::http::ip_by_addr(const sockaddr *addr, char *arr) {
    if (!addr)
        return error::status_invalid_argument("ip: null addr");

    auto const sn = reinterpret_cast<const sockaddr_in *> (addr);

    if (sn->sin_family == manapi::ev::IPv4)
        memcpy (arr, &sn->sin_addr, sizeof (sn->sin_addr));
    else if (sn->sin_family == manapi::ev::IPv6)
        memcpy (arr, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr, 16);
    else
        return error::status_invalid_argument("ip: invalid sin_family");

    return error::status_ok();
}

bool manapi::net::http::split_http_port(std::string_view name, std::string_view &host, std::string_view &port, bool& has_port) {
    has_port = false;
    if (!name.empty() && name[0] == '[') {
        // Parse a bracketed host, typically an IPv6 literal.
        const size_t rbracket = name.find(']', 1);
        if (rbracket == std::string_view::npos) {
            // Unmatched [
            return false;
        }
        if (rbracket == name.size() - 1) {
            // ]<end>
            port = std::string_view();
        } else if (name[rbracket + 1] == ':') {
            // ]:<port?>
            port = name.substr(rbracket + 2, name.size() - rbracket - 2);
            has_port = true;
        } else {
            // ]<invalid>
            return false;
        }
        host = name.substr(1, rbracket - 1);
        if (host.find(':') == std::string_view::npos) {
            // Require all bracketed hosts to contain a colon, because a hostname or
            // IPv4 address should never use brackets.
            host = std::string_view();
            return false;
        }
    } else {
        size_t colon = name.find(':');
        if (colon != std::string_view::npos &&
            name.find(':', colon + 1) == std::string_view::npos) {
            // Exactly 1 colon.  Split into host:port.
            host = name.substr(0, colon);
            port = name.substr(colon + 1, name.size() - colon - 1);
            has_port = true;
            } else {
                // 0 or 2+ colons.  Bare hostname or IPv6 litearal.
                host = name;
                port = std::string_view();
            }
    }
    return true;
}