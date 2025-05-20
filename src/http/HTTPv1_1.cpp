#include <cstring>
#include <cctype>
#include <memory>

#include "http/HTTPv1_1.hpp"

#include "ManapiBase64.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiString.hpp"
#include "services/ManapiFetch.hpp"

enum http_v1_1_callbacks {
    HTTP_V1_1_CALLBACK_INIT = 0,
    HTTP_V1_1_CALLBACK_SKIP_WHITE_SPACE,
    HTTP_V1_1_CALLBACK_NEXT_LINER,
    HTTP_V1_1_CALLBACK_NEXT_LINEN,
    HTTP_V1_1_CALLBACK_PARSE_HEADER_KEY,
    HTTP_V1_1_CALLBACK_PARSE_HEADER_VALUE,
    HTTP_V1_1_CALLBACK_PARSE_HTTP_1_1,
    HTTP_V1_1_CALLBACK_PARSE_HTTP_2,
    HTTP_V1_1_CALLBACK_PARSE_METHOD,
    HTTP_V1_1_CALLBACK_PARSE_PATH,
    HTTP_V1_1_CALLBACK_THINK,
    HTTP_V1_1_CALLBACK_UPGRADE,
    HTTP_V1_1_CALLBACK_FINISH,
    HTTP_V1_1_CALLBACK_BUG
};

static constexpr char version_label_1_1[] = "HTTP/1.1";
static constexpr char version_label_2[] = "HTTP/2.0";

static const std::set<char> tcharlist = {'!', '#', '$', '%', '&', '\'', '*', '+', '-', '.', '^', '_', '`', '|', '~'};

bool istokenchar (const char &c) {
    return ::isalpha(c) || ::isdigit(c) || tcharlist.contains(c);
}

int manapi::net::http::http_v1_1_work(http_v1_1_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize) {
            // ctx->request_data->buffer = site->bufferpool()->get();
    ssize_t pos = 0;

    auto &buffer = *nbuffer;
    auto &size = *nsize;

    while (pos != size) {
        repeat: switch (ctx->current) {
            case HTTP_V1_1_CALLBACK_INIT: {

                ctx->current = HTTP_V1_1_CALLBACK_PARSE_METHOD;
                ctx->next = 0;
                ctx->http = 0;

                break;
            }
            case HTTP_V1_1_CALLBACK_NEXT_LINER: {
                if (buffer[pos] != '\r') {
                    return EHTTP_V1_1_PROTOCOL_ERROR;
                }
                pos++;
                ctx->current = HTTP_V1_1_CALLBACK_NEXT_LINEN;
                break;
            }
            case HTTP_V1_1_CALLBACK_NEXT_LINEN: {
                if (buffer[pos] != '\n') {
                    return EHTTP_V1_1_PROTOCOL_ERROR;
                }
                pos++;
                ctx->current = ctx->next;
                ctx->next = HTTP_V1_1_CALLBACK_BUG;
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_HEADER_KEY: {
                while (pos < size) {
                    if (!ctx->s1.empty() && ctx->s1.back() == '\r') {
                        /**
                         * RFC7230 (3.2.4) Field Parsing
                         * Historically, HTTP header field values could be extended over multiple
                         * lines by preceding each extra line with at least one space or horizontal tab (obs-fold).
                         */
                        if (buffer[pos] == '\n') {
                            if (ctx->s1.size() != 1) {
                                return EHTTP_V1_1_PROTOCOL_ERROR;
                            }

                            ctx->s1.pop_back();

                            pos ++;

                            ctx->current = HTTP_V1_1_CALLBACK_FINISH;
                            ctx->next = HTTP_V1_1_CALLBACK_BUG;

                            goto repeat;
                        }

                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }

                    if (buffer[pos] == '\r') {
                        ctx->s1.push_back(buffer[pos]);
                        pos++;
                        continue;
                    }

                    if (buffer[pos] == ':') {
                        /**
                         * RFC7230 (3.2) Header Fields
                         * Each header field consists of a case-insensitive field name followed by a colon (":"),
                         * optional leading whitespace, the field value, and optional trailing whitespace.
                         */

                        pos++;
                        ctx->current = HTTP_V1_1_CALLBACK_PARSE_HEADER_VALUE;

                        break;
                    }

                    if (!istokenchar(buffer[pos])) {
                        /**
                         * RFC7230 (3.2.4) Field Parsing
                         * No whitespace is allowed between the header field-name and colon.
                         *
                         * RFC7230 (3.2.6) Field Value Components
                         * Most HTTP header field values are defined using
                         * common syntax components (token, quoted-string, and comment)
                         * separated by whitespace or specific delimiting characters.
                         * Delimiters are chosen from the set of US-ASCII visual characters
                         * not allowed in a token (DQUOTE and "(),/:;<=>?@[\]{}").
                         */
                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }



                    ctx->s1.push_back(static_cast<char>(std::tolower(buffer[pos])));
                    pos++;
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_HEADER_VALUE: {
                if (buffer[pos] == ' ' && ctx->s2.empty()) {
                    /**
                     * RFC7230 (3.2) Header Fields
                     * Optitional leading whitespace
                     */
                    pos++;
                }

                while (pos < size) {
                    if (!ctx->s2.empty() && ctx->s2.back() == '\r') {
                        if (buffer[pos] == '\n') {
                            ctx->s2.pop_back();
                            pos++;

                            if (!ctx->s2.empty() && ctx->s2.back() == ' ') {
                                /**
                                 * RFC7230 (3.2) Header Fields
                                 * Optitional trailing whitespace
                                 */
                                ctx->s2.pop_back();
                            }

                            /* insert */
                            auto it = ctx->req->headers.find(ctx->s1);
                            if (it == ctx->req->headers.end()) {
                                ctx->req->headers.insert({ctx->s1, ctx->s2});
                            }
                            else {
                                /**
                                 * RFC7230 (3.2.2) Field Order
                                 * A recipient MAY combine multiple header fields with
                                 * the same field name into one "field-name: field-value" pair,
                                 * without changing the semantics of the message,
                                 * by appending each subsequent field value to the combined field value in order,
                                 * separated by a comma. The order in which header fields with the same
                                 * field name are received is therefore significant to
                                 * the interpretation of the combined field value;
                                 * a proxy MUST NOT change the order of these field values when forwarding a message.
                                 */

                                it->second.push_back(',');
                                it->second.append(ctx->s2);
                            }

                            ctx->s1.resize(0);
                            ctx->s2.resize(0);

                            ctx->current = HTTP_V1_1_CALLBACK_PARSE_HEADER_KEY;

                            break;
                        }

                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }

                    if (buffer[pos] == '\r') {
                        ctx->s2.push_back(buffer[pos]);
                        pos++;
                        continue;
                    }

                    if (!isprint(buffer[pos])) {
                        /**
                         * RFC7230 (3.2) Header Fields
                         *
                         * header-field   = field-name ":" OWS field-value OWS
                         *
                         * field-name     = token
                         * field-value    = *( field-content / obs-fold )
                         * field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
                         * field-vchar    = VCHAR / obs-text
                         *
                         * obs-fold       = CRLF 1*( SP / HTAB )
                         *                ; obsolete line folding
                         *                ; see Section 3.2.4
                         *
                         * V[isible]CHAR
                         */
                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }

                    ctx->s2.push_back(buffer[pos]);
                    pos++;
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_HTTP_1_1: {
                while (pos != size) {
                    if (buffer[pos] != version_label_1_1[ctx->next]) {
                        if (ctx->next == 5) {
                            /* another http version */
                            ctx->current = HTTP_V1_1_CALLBACK_PARSE_HTTP_2;
                            break;
                        }
                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }

                    pos++;
                    ctx->next++;

                    if (ctx->next == sizeof (version_label_1_1) - 1) {
                        /* eos */
                        ctx->current = HTTP_V1_1_CALLBACK_THINK;
                        ctx->next = HTTP_V1_1_CALLBACK_BUG;
                        ctx->http = versions::HTTP_v1_1;
                        break;
                    }
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_HTTP_2: {
                while (pos != size) {
                    if (buffer[pos] != version_label_2[ctx->next]) {
                        if (!(pos == 6 && buffer[pos] == '\r')) {
                            return EHTTP_V1_1_PROTOCOL_ERROR;
                        }
                    }

                    pos++;
                    ctx->next++;

                    if (ctx->next == sizeof (version_label_2) - 1) {
                        /* eos */
                        ctx->current = HTTP_V1_1_CALLBACK_THINK;
                        ctx->next = HTTP_V1_1_CALLBACK_BUG;
                        ctx->http = versions::HTTP_v2;
                        break;
                    }
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_METHOD: {
                while (pos != size) {
                    if (istokenchar(buffer[pos])) {
                        /**
                         * RFC (3.1.1) Request Line
                         * The method token indicates the request method to be performed on the target resource.
                         * The request method is case-sensitive.
                         *
                         * T[oken]CHAR
                         */
                        ctx->s1.push_back(buffer[pos]);
                        pos++;
                        continue;
                    }

                    if (buffer[pos] == ' ') {
                        pos++;
                        /* end of method */
                        ctx->current = HTTP_V1_1_CALLBACK_PARSE_PATH;
                    }
                    else {
                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }

                    break;
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_PARSE_PATH: {
                while (pos != size) {
                    if (buffer[pos] == ' ') {
                        /* end of path */
                        ctx->current = HTTP_V1_1_CALLBACK_PARSE_HTTP_1_1;
                        pos++;
                        break;
                    }

                    ctx->s2.push_back(buffer[pos]);

                    pos++;
                }
                break;
            }
            case HTTP_V1_1_CALLBACK_THINK: {
                buffer += pos;
                size -= pos;
                pos = 0;

                url_decode_stream url_decoder;

                switch (ctx->http) {
                    case versions::HTTP_v0_9:
                    case versions::HTTP_v1_0:
                    case versions::HTTP_v1_1: {
                        break;
                    }
                    case versions::HTTP_v2: {
                        if (ctx->s2.size() /* '*' path */ != 1 || ctx->s1.size() /* 'PRI' method*/ != 3) {
                            return EHTTP_V1_1_PROTOCOL_ERROR;
                        }
                        ctx->current = HTTP_V1_1_CALLBACK_UPGRADE;

                        goto finish;
                    }
                    default: {
                        return EHTTP_V1_1_PROTOCOL_ERROR;
                    }
                }

                ctx->req = std::make_unique<request_data_t>();
                ctx->req->method = ctx->s1;
                /* s1 is buffer */
                ctx->s1.resize(0);

                ctx->req->uri = ctx->s2;
                ctx->s2.resize(0);

                if (auto rhs = url_decoder << ctx->req->uri) {
                    return EHTTP_V1_1_PROTOCOL_ERROR;
                }

                ctx->req->http = ctx->http;
                ctx->req->path = url_decoder.result();
                ctx->req->divided = url_decoder.divided();

                ctx->current = HTTP_V1_1_CALLBACK_NEXT_LINER;
                ctx->next = HTTP_V1_1_CALLBACK_PARSE_HEADER_KEY;

                finish: break;
            }
            case HTTP_V1_1_CALLBACK_UPGRADE:
                buffer += pos;
                size -= pos;
                pos = 0;
                return EHTTP_V1_1_PROTOCOL_UPGRADE;
            case HTTP_V1_1_CALLBACK_FINISH: {
                buffer += pos;
                size -= pos;
                pos = 0;

                auto const hcontentlength = ctx->req->headers.find(HEADER.CONTENT_LENGTH);
                if (hcontentlength == ctx->req->headers.end()) {
                    ctx->req->body_size = -1;
                }
                else {
                    ctx->req->body_size = std::stoll(hcontentlength->second);
                }

                auto const hconnection = ctx->req->headers.find(HEADER.CONNECTION);
                if (hconnection != ctx->req->headers.end()) {
                    /**
                     * RFC7540 (3.2) Starting HTTP/2 for "http" URIs
                     *
                     * A client that makes a request for an "http"
                     * URI without prior knowledge about support for
                     * HTTP/2 on the next hop uses the HTTP Upgrade mechanism
                     * (Section 6.7 of [RFC7230]). The client does so by
                     * making an HTTP/1.1 request that includes an
                     * Upgrade header field with the "h2c" token.
                     */
                    const auto val = http::parse_header_value(hconnection->second);

                    for (const auto &param: val) {
                        if (manapi::string::equals(param.value, "upgrade", 0b10)) {
                            auto const hupgrade = ctx->req->headers.find(HEADER.UPGRADE);

                            if (hupgrade->second == "h2c") {
                                ctx->http = versions::HTTP_v2;
                            }

                            continue;
                        }
                        if (manapi::string::equals(param.value, "http2-settings", 0b10)) {
                            try {
                                ctx->s1 = encrypt::base64::from_base64(param.value);
                            }
                            catch (...) {
                                return EHTTP_V1_1_PROTOCOL_ERROR;
                            }

                            continue;
                        }
                    }

                    if (ctx->http == versions::HTTP_v2) {
                        if (ctx->s1.empty()) {
                            /**
                             * RFC7540 (3.2) Starting HTTP/2 for "http" URIs
                             *
                             * Such an HTTP/1.1 request MUST include exactly
                             * one HTTP2-Settings (Section 3.2.1) header field.
                             */

                            return EHTTP_V1_1_PROTOCOL_ERROR;
                        }

                        return EHTTP_V1_1_PROTOCOL_UPGRADE;
                    }

                    if (ctx->http != versions::HTTP_v1_1) {
                        return EHTTP_V1_1_PROTOCOL_UPGRADE;
                    }
                }

                return EHTTP_V1_1_PROTOCOL_OK;
            }
            case HTTP_V1_1_CALLBACK_BUG: {
                /* bug */
                return EHTTP_V1_1_PROTOCOL_ERROR;
            }
            default:
                return EHTTP_V1_1_PROTOCOL_ERROR;
        }
    }

    buffer += pos;
    size -= pos;
    pos = 0;

    return EHTTP_V1_1_PROTOCOL_WANT_READ;
}

manapi::future<ssize_t> manapi::net::http::http_v1_1_chunked_read(http_v1_1_chunked_t *ctx, worker::base *worker, worker::connection *conn, http::config *config, char *buffer, ssize_t size) {

}
