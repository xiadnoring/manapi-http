#include <cstring>

#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "http/ManapiHttpConfig.hpp"
#include "http/ManapiHttpTypes.hpp"
#include "../include/ManapiUtils.hpp"

static constexpr char header_delimiter[] = ": ";


manapi::status_or<std::pair<std::string_view, std::string_view>> manapi::net::http::parse_header(std::string_view header) {
    std::pair <std::string_view, std::string_view> parsed;
    auto const pos = header.find(':');

    if (std::string::npos == pos) {
        return manapi::status_invalid_argument("invalid header: semicolon is missing");
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
    res.resize(stringify_header_size(header));
    auto size = stringify_header(res.data(), res.size(), header);
    assert(size <= res.size());
    res.resize(size);
    return std::move(res);
}

std::size_t manapi::net::http::stringify_header(char *buff, std::size_t sz, const std::pair<std::string_view, std::string_view> &header) {
    std::size_t i = 0;
    assert(sz >= header.first.size());
    sz -= header.first.size();
    memcpy (buff + i, header.first.data(), header.first.size());
    i += header.first.size();
    assert(sz >= sizeof (header_delimiter) - 1);
    sz -= sizeof (header_delimiter) - 1;
    memcpy (buff + i, header_delimiter, sizeof (header_delimiter) - 1);
    i += sizeof (header_delimiter) - 1;
    assert(sz >= header.second.size());
    sz -= header.second.size();
    memcpy (buff + i, header.second.data(), header.second.size());
    i += header.second.size();
    return i;
}

std::size_t manapi::net::http::stringify_header_size(const std::pair<std::string_view, std::string_view> &header) {
    return header.first.size() + header.second.size() + (sizeof (header_delimiter) - 1);
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

manapi::status_or<std::vector <manapi::net::http::header_value_t>> manapi::net::http::parse_header_value (std::string_view header_value) MANAPIHTTP_NOEXCEPT {
    try {
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
                                    return status_invalid_argument("parse_header_value: unexpected symbol");
                                rhs++;
                                state = HTTP_HV_FIELD_START;
                                next = HTTP_HV_ERR;
                                goto finish;
                            }
                            case ';': {
                                if (next != HTTP_HV_VALUE_START
                                    && next != HTTP_HV_FIELD_START)
                                    return status_invalid_argument("parse_header_value: unexpected symbol");
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

                                return status_invalid_argument("parse_header_value: unexpected symbol");
                            }
                            default: {
                                return status_invalid_argument("unreachable");
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
                    return status_invalid_argument("unreachable");
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
                return status_invalid_argument("parse_header_value: unexpected end");
            }
        }

        return std::move(data);
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "parse_header_value", e.what());
        return status_internal("parse_header_value");
    }
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
    if (addr) {
        if (addr->sa_family == ev::IPv4)
            return ev::IPv4;
        if (addr->sa_family == ev::IPv6)
            return ev::IPv6;
    }

    return -1;
}

bool manapi::net::http::header_has_more_fields(std::string_view name) MANAPIHTTP_NOEXCEPT {
    if (name == manapi::net::http::H_CACHE_CONTROL
        || name == manapi::net::http::H_WARNING
        || name == manapi::net::http::H_SET_COOKIE) {
        return true;
    }

    return false;
}

manapi::status_or<std::pair<std::string, uint16_t>> manapi::net::http::strinfigy_ip(const sockaddr *addr) {
    if (!addr)
        return status_invalid_argument("ip: null addr");

    std::string buffer;
    uint32_t size;

    if (addr->sa_family == manapi::ev::IPv4) {
        size = sizeof ("xxx:xxx:xxx:xxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *> (addr)->sin_addr, buffer.data(), size))
            return status_invalid_argument("ip: inet_ntop() returned null");

        while (size > 0 && buffer[size - 1] == '\0') {
            /* skip null bytes */
            size--;
        }

        buffer.resize(size);

        uint16_t const port = (reinterpret_cast<const sockaddr_in *> (addr)->sin_port);
        return std::make_pair(std::move(buffer), port);
    }

    if (addr->sa_family == manapi::ev::IPv6) {
        size = sizeof ("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr, buffer.data(), size))
            return status_invalid_argument("ip: inet_ntop() returned null");

        while (size > 0 && buffer[size - 1] == '\0') {
            /* skip null bytes */
            size--;
        }

        buffer.resize(size);

        uint16_t const port = (reinterpret_cast<const sockaddr_in6 *> (addr)->sin6_port);
        return std::make_pair(std::move(buffer), port);
    }

    return status_invalid_argument("ip: invalid sin_family");
}

manapi::status_or<uint16_t> manapi::net::http::port_by_addr(const sockaddr *addr) {
    if (!addr)
        return status_invalid_argument("ip: null addr");

    if (addr->sa_family == manapi::ev::IPv4)
        return (reinterpret_cast<const sockaddr_in *> (addr)->sin_port);

    if (addr->sa_family == manapi::ev::IPv6)
        return (reinterpret_cast<const sockaddr_in6 *> (addr)->sin6_port);

    return status_invalid_argument("ip: invalid sin_family");
}

manapi::status manapi::net::http::ip_by_addr(const sockaddr *addr, char *arr) {
    if (!addr)
        return status_invalid_argument("ip: null addr");

    if (addr->sa_family == manapi::ev::IPv4)
        memcpy (arr, &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr, sizeof (reinterpret_cast<const sockaddr_in *>(addr)->sin_addr));
    else if (addr->sa_family == manapi::ev::IPv6)
        memcpy (arr, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr, 16);
    else
        return status_invalid_argument("ip: invalid sin_family");

    return status_ok();
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