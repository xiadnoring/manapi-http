#ifndef MANAPIHTTP_MANAPIHTTPTYPES_H
#define MANAPIHTTP_MANAPIHTTPTYPES_H

#include <string>

#include "ManapiThreadSafe.hpp"

namespace manapi::net {
    static const struct {
        std::string CONTINUE_100                        = "Continue";
        std::string SWITCHING_PROTOCOLS_101             = "Switching Protocols";
        std::string PROCESSING_102                      = "Processing";
        std::string EARLY_HINTS_103                     = "Early Hints";

        std::string OK_200                              = "OK";
        std::string CREATED_201                         = "Created";
        std::string ACCEPTED_202                        = "Accepted";
        std::string NON_AUTHORITATIVE_INFORMATION_203   = "Non-Authoritative Information";
        std::string NO_CONTENT_204                      = "No Content";
        std::string RESET_CONTENT_205                   = "Reset Content";
        std::string PARTIAL_CONTENT_206                 = "Partial Content";
        std::string MULTI_STATUS_207                    = "Multi-Status";
        std::string ALREADY_REPORTED_208                = "Already Reported";
        std::string IM_USED_226                         = "IM Used";

        std::string MULTIPLE_CHOICES_300                = "Multiple Choices";
        std::string MOVED_PERMANENTLY_301               = "Moved Permanently";
        std::string FOUND_302                           = "Found";
        std::string SEE_OTHER_303                       = "See Other";
        std::string NOT_MODIFIED_304                    = "Not Modified";
        std::string USE_PROXY_305                       = "Use Proxy";
        std::string TEMPORARY_REDIRECT_307              = "Temporary Redirect";
        std::string PERMANENT_REDIRECT_308              = "Permanent Redirect";

        std::string BAD_REQUEST_400                     = "Bad Request";
        std::string UNAUTHORIZED_401                    = "Unauthorized";
        std::string PAYMENT_REQUIRED_402                = "Payment Required";
        std::string FORBIDDEN_403                       = "Forbidden";
        std::string NOT_FOUND_404                       = "Not Found";
        std::string METHOD_NOT_ALLOWED_405              = "Method Not Allowed";
        std::string NOT_ACCEPTABLE_406                  = "Not Acceptable";
        std::string PROXY_AUTHENTICATION_REQUIRED_407   = "Proxy Authentication Required";
        std::string REQUEST_TIMEOUT_408                 = "Request Timeout";
        std::string CONFLICT_409                        = "Conflict";
        std::string GONE_410                            = "Gone";
        std::string LENGTH_REQUIRED_411                 = "Length Required";
        std::string PRECONDITION_FAILED_412             = "Precondition Failed";
        std::string PAYLOAD_TOO_LARGE_413               = "Payload Too Large";
        std::string URI_TOO_LONG_414                    = "URI Too Long";
        std::string UNSUPPORTED_MEDIA_TYPE_415          = "Unsupported Media Type";
        std::string RANGE_NOT_SATISFIABLE_416           = "Range Not Satisfiable";
        std::string EXPECTATION_FAILED_417              = "Expectation Failed";
        std::string IM_A_TEAPOT_418                     = "I'm a teapot";
        std::string AUTHENTICATION_TIMEOUT_419          = "Authentication Timeout";
        std::string MISDIRECTED_REQUEST_421             = "Misdirected Request";
        std::string UNPROCESSABLE_ENTITY_422            = "Unprocessable Entity";
        std::string LOCKED_423                          = "Locked";
        std::string FAILED_DEPENDENCY_424               = "Failed Dependency";
        std::string TOO_EARLY_425                       = "Too Early";
        std::string UPGRADE_REQUIRED_426                = "Upgrade Required";
        std::string PRECONDITION_REQUIRED_428           = "Precondition Required";
        std::string TOO_MANY_REQUESTS_429               = "Too Many Requests";
        std::string REQUEST_HEADER_FIELDS_TOO_LARGE_431 = "Request Header Fields Too Large";
        std::string RETRY_WITH_449                      = "Retry With";
        std::string UNAVAILABLE_FOR_LEGAL_REASONS_451   = "Unavailable For Legal Reasons";
        std::string CLIENT_CLOSED_REQUEST_499           = "Client Closed Request";

        std::string INTERNAL_SERVER_ERROR_500           = "Internal Server Error";
        std::string NOT_IMPLEMENTED_501                 = "Not Implemented";
        std::string BAD_GATEWAY_502                     = "Bad Gateway";
        std::string SERVICE_UNAVAILABLE_503             = "Service Unavailable";
        std::string GATEWAY_TIMEOUT_504                 = "Gateway Timeout";
        std::string HTTP_VERSION_NOT_SUPPORTED_505      = "HTTP Version Not Supported";
        std::string VARIANT_ALSO_NEGOTIATES_506         = "Variant Also Negotiates";
        std::string INSUFFICIENT_STORAGE_507            = "Insufficient Storage";
        std::string LOOP_DETECTED_508                   = "Loop Detected";
        std::string BANDWIDTH_LIMIT_EXCEEDED_509        = "Bandwidth Limit Exceeded";
        std::string NOT_EXTENDED_510                    = "Not Extended";
        std::string NETWORK_AUTHENTICATION_REQUIRED_511 = "Network Authentication Required";
        std::string UNKNOWN_ERROR_520                   = "Unknown Error";
        std::string WEB_SERVER_IS_DOWN_521              = "Web Server Is Down";
        std::string CONNECTION_TIMED_OUT_522            = "Connection Timed Out";
        std::string ORIGIN_IS_UNREACHABLE_523           = "Origin Is Unreachable";
        std::string TIMEOUT_OCCURRED_524                = "Timeout Occurred";
        std::string SSL_HANDSHAKE_FAILED_525            = "SSL Handshake Failed";
        std::string INVALID_SSL_CERTIFICATE_526         = "Invalid SSL Certificate";

    } http_status;

    static const struct {
        std::string CONTENT_RANGE       = "content-range";
        std::string CONTENT_LENGTH      = "content-length";
        std::string CONTENT_TYPE        = "content-type";
        std::string SET_COOKIE          = "set-cookie";
        std::string COOKIE              = "cookie";
        std::string ACCEPT              = "accept";
        std::string ACCEPT_LANGUAGE     = "accept-language";
        std::string ACCEPT_ENCODING     = "accept-encoding";
        std::string ACCEPT_RANGES       = "accept-ranges";
        std::string HOST                = "host";
        std::string USER_AGENT          = "user-agent";
        std::string CONNECTION          = "connection";
        std::string CACHE_CONTROL       = "cache-control";
        std::string EXPIRES             = "expires";
        std::string LAST_MODIFIED       = "last-modified";
        std::string ETAG                = "etag";
        std::string SERVER              = "server";
        std::string DATE                = "date";
        std::string LOCATION            = "location";
        std::string REFRESH             = "refresh";
        std::string PRAGMA              = "pragma";
        std::string CONTENT_DISPOSITION = "content-disposition";
        std::string CONTENT_ENCODING    = "content-encoding";
        std::string RANGE               = "range";
        std::string KEEP_ALIVE          = "keep-alive";
        std::string ALT_SVC             = "alt-svc";
        std::string AUTHORIZATION       = "authorization";
    } http_header;

    static const struct {
        std::string TEXT_PLAIN                          = "text/plain";
        std::string TEXT_HTML                           = "text/html";
        std::string TEXT_CSS                            = "text/css";
        std::string TEXT_JS                             = "text/javascript";
        std::string TEXT_CSV                            = "text/csv";

        std::string APPLICATION_JS                      = "application/javascript";
        std::string APPLICATION_JSON                    = "application/json";
        std::string APPLICATION_JSON_LD                 = "application/ld+json";
        std::string APPLICATION_OCTET_STREAM            = "application/octet-stream";
        std::string APPLICATION_GZIP                    = "application/gzip";
        std::string APPLICATION_PDF                     = "application/pdf";
        std::string APPLICATION_RAR                     = "application/vnd.rar";
        std::string APPLICATION_SHELL                   = "application/x-sh";
        std::string APPLICATION_ZIP                     = "application/zip";
        std::string APPLICATION_TAR                     = "application/x-tar";

        std::string MULTIPART_FORM_DATA                 = "multipart/form-data";

        std::string APPLICATION_X_WWW_FORM_URLENCODED   = "application/x-www-form-urlencoded";

        std::string VIDEO_MP4                           = "video/mp4";
        std::string VIDEO_MPEG                          = "video/mpeg";
        std::string VIDEO_WEBM                          = "audio/webm";

        std::string AUDIO_MP3                           = "audio/mp3";
        std::string AUDIO_AAC                           = "audio/aac";
        std::string AUDIO_WAV                           = "audio/wav";
        std::string AUDIO_WEBA                          = "audio/webm";

        std::string IMAGE_GIF                           = "image/gif";
        std::string IMAGE_JPEG                          = "image/jpeg";
        std::string IMAGE_PNG                           = "image/png";
        std::string IMAGE_SVG                           = "image/svg+xml";
        std::string IMAGE_WEBP                          = "image/webp";
        std::string IMAGE_BMP                           = "image/bmp";

        std::string FONT_TTF                            = "font/ttf";
        std::string FONT_WOFF                           = "font/woff";
        std::string FONT_WOFF2                          = "font/woff2";
        std::string FONT_OTF                            = "font/otf";

    } http_mime;

    static manapi::utils::safe_unordered_map <std::string, std::string> mime_by_extension = {
        {"txt", http_mime.TEXT_PLAIN},
        {"mp4", http_mime.VIDEO_MP4},
        {"js",  http_mime.TEXT_JS},
        {"mjs", http_mime.TEXT_JS},
        {"css", http_mime.TEXT_CSS},
        {"mp4", http_mime.VIDEO_MP4},
        {"mp3", http_mime.AUDIO_MP3},
        {"html", http_mime.TEXT_HTML},
        {"zip", http_mime.APPLICATION_ZIP},
        {"gzip", http_mime.APPLICATION_GZIP},
        {"tar", http_mime.APPLICATION_TAR},
        {"png", http_mime.IMAGE_PNG},
        {"jpeg", http_mime.IMAGE_JPEG},
        {"jpg", http_mime.IMAGE_JPEG},
        {"svg", http_mime.IMAGE_SVG},
        {"ttf", http_mime.FONT_TTF},
        {"woff", http_mime.FONT_WOFF},
        {"woff2", http_mime.FONT_WOFF2},
        {"otf", http_mime.FONT_OTF},
        {"webp", http_mime.IMAGE_WEBP},
        {"webm", http_mime.VIDEO_WEBM},
        {"weba", http_mime.AUDIO_WEBA},
        {"pdf", http_mime.APPLICATION_PDF},
        {"json", http_mime.APPLICATION_JSON},
        {"htm", http_mime.TEXT_HTML},
        {"gif", http_mime.IMAGE_GIF},
        {"bmp", http_mime.IMAGE_BMP},
        {"bin", http_mime.APPLICATION_OCTET_STREAM},
        {"",    http_mime.APPLICATION_OCTET_STREAM}
    };
}

#endif //MANAPIHTTP_MANAPIHTTPTYPES_H
