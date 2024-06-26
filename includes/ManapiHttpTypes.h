#ifndef MANAPIHTTP_MANAPIHTTPTYPES_H
#define MANAPIHTTP_MANAPIHTTPTYPES_H

#include <string>

static struct {
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

static struct {
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
} http_header;

static struct {
    std::string TEXT_PLAIN          = "text/plain";
    std::string TEXT_HTML           = "text/html";
    std::string TEXT_CSS            = "text/css";
    std::string TEXT_JS             = "text/javascript";

    std::string APPLICATION_JS      = "application/javascript";
    std::string APPLICATION_JSON    = "application/json";

    std::string MULTIPART_FORM_DATA = "multipart/form-data";

    std::string APPLICATION_X_WWW_FORM_URLENCODED = "application/x-www-form-urlencoded";
} http_mime;

#endif //MANAPIHTTP_MANAPIHTTPTYPES_H
