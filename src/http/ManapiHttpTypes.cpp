#include "http/ManapiHttpTypes.hpp"
#include "../include/ManapiUtils.hpp"

static const std::map<int, std::string_view> status_to_string_map = {
    {100, manapi::net::http::status::CONTINUE_100},
    {101, manapi::net::http::status::SWITCHING_PROTOCOLS_101},
    {102, manapi::net::http::status::PROCESSING_102},
    {103, manapi::net::http::status::EARLY_HINTS_103},

    {200, manapi::net::http::status::OK_200},
    {201, manapi::net::http::status::CREATED_201},
    {202, manapi::net::http::status::ACCEPTED_202},
    {203, manapi::net::http::status::NON_AUTHORITATIVE_INFORMATION_203},
    {204, manapi::net::http::status::NO_CONTENT_204},
    {205, manapi::net::http::status::RESET_CONTENT_205},
    {206, manapi::net::http::status::PARTIAL_CONTENT_206},
    {207, manapi::net::http::status::MULTI_STATUS_207},
    {208, manapi::net::http::status::ALREADY_REPORTED_208},
    {226, manapi::net::http::status::IM_USED_226},

    {300, manapi::net::http::status::MULTIPLE_CHOICES_300},
    {301, manapi::net::http::status::MOVED_PERMANENTLY_301},
    {302, manapi::net::http::status::FOUND_302},
    {303, manapi::net::http::status::SEE_OTHER_303},
    {304, manapi::net::http::status::NOT_MODIFIED_304},
    {305, manapi::net::http::status::USE_PROXY_305},
    {307, manapi::net::http::status::TEMPORARY_REDIRECT_307},
    {308, manapi::net::http::status::PERMANENT_REDIRECT_308},

    {400, manapi::net::http::status::BAD_REQUEST_400},
    {401, manapi::net::http::status::UNAUTHORIZED_401},
    {402, manapi::net::http::status::PAYMENT_REQUIRED_402},
    {403, manapi::net::http::status::FORBIDDEN_403},
    {404, manapi::net::http::status::NOT_FOUND_404},
    {405, manapi::net::http::status::METHOD_NOT_ALLOWED_405},
    {406, manapi::net::http::status::NOT_ACCEPTABLE_406},
    {407, manapi::net::http::status::PROXY_AUTHENTICATION_REQUIRED_407},
    {408, manapi::net::http::status::REQUEST_TIMEOUT_408},
    {409, manapi::net::http::status::CONFLICT_409},
    {410, manapi::net::http::status::GONE_410},
    {411, manapi::net::http::status::LENGTH_REQUIRED_411},
    {412, manapi::net::http::status::PRECONDITION_FAILED_412},
    {413, manapi::net::http::status::PAYLOAD_TOO_LARGE_413},
    {414, manapi::net::http::status::URI_TOO_LONG_414},
    {415, manapi::net::http::status::UNSUPPORTED_MEDIA_TYPE_415},
    {416, manapi::net::http::status::RANGE_NOT_SATISFIABLE_416},
    {417, manapi::net::http::status::EXPECTATION_FAILED_417},
    {418, manapi::net::http::status::IM_A_TEAPOT_418},
    {419, manapi::net::http::status::AUTHENTICATION_TIMEOUT_419},
    {421, manapi::net::http::status::MISDIRECTED_REQUEST_421},
    {422, manapi::net::http::status::UNPROCESSABLE_ENTITY_422},
    {423, manapi::net::http::status::LOCKED_423},
    {424, manapi::net::http::status::FAILED_DEPENDENCY_424},
    {425, manapi::net::http::status::TOO_EARLY_425},
    {426, manapi::net::http::status::UPGRADE_REQUIRED_426},
    {428, manapi::net::http::status::PRECONDITION_REQUIRED_428},
    {429, manapi::net::http::status::TOO_MANY_REQUESTS_429},
    {431, manapi::net::http::status::REQUEST_HEADER_FIELDS_TOO_LARGE_431},
    {449, manapi::net::http::status::RETRY_WITH_449},
    {451, manapi::net::http::status::UNAVAILABLE_FOR_LEGAL_REASONS_451},
    {499, manapi::net::http::status::CLIENT_CLOSED_REQUEST_499},

    {500, manapi::net::http::status::INTERNAL_SERVER_ERROR_500},
    {501, manapi::net::http::status::NOT_IMPLEMENTED_501},
    {502, manapi::net::http::status::BAD_GATEWAY_502},
    {503, manapi::net::http::status::SERVICE_UNAVAILABLE_503},
    {504, manapi::net::http::status::GATEWAY_TIMEOUT_504},
    {505, manapi::net::http::status::HTTP_VERSION_NOT_SUPPORTED_505},
    {506, manapi::net::http::status::VARIANT_ALSO_NEGOTIATES_506},
    {507, manapi::net::http::status::INSUFFICIENT_STORAGE_507},
    {508, manapi::net::http::status::LOOP_DETECTED_508},
    {509, manapi::net::http::status::BANDWIDTH_LIMIT_EXCEEDED_509},
    {510, manapi::net::http::status::NOT_EXTENDED_510},
    {511, manapi::net::http::status::NETWORK_AUTHENTICATION_REQUIRED_511},
    {520, manapi::net::http::status::UNKNOWN_ERROR_520},
    {521, manapi::net::http::status::WEB_SERVER_IS_DOWN_521},
    {522, manapi::net::http::status::CONNECTION_TIMED_OUT_522},
    {523, manapi::net::http::status::ORIGIN_IS_UNREACHABLE_523},
    {524, manapi::net::http::status::TIMEOUT_OCCURRED_524},
    {525, manapi::net::http::status::SSL_HANDSHAKE_FAILED_525},
    {526, manapi::net::http::status::INVALID_SSL_CERTIFICATE_526}
};

namespace manapi::net::http::status {
    std::string_view EMPTY                               = "Empty";

    std::string_view CONTINUE_100                        = "Continue";
    std::string_view SWITCHING_PROTOCOLS_101             = "Switching Protocols";
    std::string_view PROCESSING_102                      = "Processing";
    std::string_view EARLY_HINTS_103                     = "Early Hints";

    std::string_view OK_200                              = "OK";
    std::string_view CREATED_201                         = "Created";
    std::string_view ACCEPTED_202                        = "Accepted";
    std::string_view NON_AUTHORITATIVE_INFORMATION_203   = "Non-Authoritative Information";
    std::string_view NO_CONTENT_204                      = "No Content";
    std::string_view RESET_CONTENT_205                   = "Reset Content";
    std::string_view PARTIAL_CONTENT_206                 = "Partial Content";
    std::string_view MULTI_STATUS_207                    = "Multi-Status";
    std::string_view ALREADY_REPORTED_208                = "Already Reported";
    std::string_view IM_USED_226                         = "IM Used";

    std::string_view MULTIPLE_CHOICES_300                = "Multiple Choices";
    std::string_view MOVED_PERMANENTLY_301               = "Moved Permanently";
    std::string_view FOUND_302                           = "Found";
    std::string_view SEE_OTHER_303                       = "See Other";
    std::string_view NOT_MODIFIED_304                    = "Not Modified";
    std::string_view USE_PROXY_305                       = "Use Proxy";
    std::string_view TEMPORARY_REDIRECT_307              = "Temporary Redirect";
    std::string_view PERMANENT_REDIRECT_308              = "Permanent Redirect";

    std::string_view BAD_REQUEST_400                     = "Bad Request";
    std::string_view UNAUTHORIZED_401                    = "Unauthorized";
    std::string_view PAYMENT_REQUIRED_402                = "Payment Required";
    std::string_view FORBIDDEN_403                       = "Forbidden";
    std::string_view NOT_FOUND_404                       = "Not Found";
    std::string_view METHOD_NOT_ALLOWED_405              = "Method Not Allowed";
    std::string_view NOT_ACCEPTABLE_406                  = "Not Acceptable";
    std::string_view PROXY_AUTHENTICATION_REQUIRED_407   = "Proxy Authentication Required";
    std::string_view REQUEST_TIMEOUT_408                 = "Request Timeout";
    std::string_view CONFLICT_409                        = "Conflict";
    std::string_view GONE_410                            = "Gone";
    std::string_view LENGTH_REQUIRED_411                 = "Length Required";
    std::string_view PRECONDITION_FAILED_412             = "Precondition Failed";
    std::string_view PAYLOAD_TOO_LARGE_413               = "Payload Too Large";
    std::string_view URI_TOO_LONG_414                    = "URI Too Long";
    std::string_view UNSUPPORTED_MEDIA_TYPE_415          = "Unsupported Media Type";
    std::string_view RANGE_NOT_SATISFIABLE_416           = "Range Not Satisfiable";
    std::string_view EXPECTATION_FAILED_417              = "Expectation Failed";
    std::string_view IM_A_TEAPOT_418                     = "I'm a teapot";
    std::string_view AUTHENTICATION_TIMEOUT_419          = "Authentication Timeout";
    std::string_view MISDIRECTED_REQUEST_421             = "Misdirected Request";
    std::string_view UNPROCESSABLE_ENTITY_422            = "Unprocessable Entity";
    std::string_view LOCKED_423                          = "Locked";
    std::string_view FAILED_DEPENDENCY_424               = "Failed Dependency";
    std::string_view TOO_EARLY_425                       = "Too Early";
    std::string_view UPGRADE_REQUIRED_426                = "Upgrade Required";
    std::string_view PRECONDITION_REQUIRED_428           = "Precondition Required";
    std::string_view TOO_MANY_REQUESTS_429               = "Too Many Requests";
    std::string_view REQUEST_HEADER_FIELDS_TOO_LARGE_431 = "Request Header Fields Too Large";
    std::string_view RETRY_WITH_449                      = "Retry With";
    std::string_view UNAVAILABLE_FOR_LEGAL_REASONS_451   = "Unavailable For Legal Reasons";
    std::string_view CLIENT_CLOSED_REQUEST_499           = "Client Closed Request";

    std::string_view INTERNAL_SERVER_ERROR_500           = "Internal Server Error";
    std::string_view NOT_IMPLEMENTED_501                 = "Not Implemented";
    std::string_view BAD_GATEWAY_502                     = "Bad Gateway";
    std::string_view SERVICE_UNAVAILABLE_503             = "Service Unavailable";
    std::string_view GATEWAY_TIMEOUT_504                 = "Gateway Timeout";
    std::string_view HTTP_VERSION_NOT_SUPPORTED_505      = "HTTP Version Not Supported";
    std::string_view VARIANT_ALSO_NEGOTIATES_506         = "Variant Also Negotiates";
    std::string_view INSUFFICIENT_STORAGE_507            = "Insufficient Storage";
    std::string_view LOOP_DETECTED_508                   = "Loop Detected";
    std::string_view BANDWIDTH_LIMIT_EXCEEDED_509        = "Bandwidth Limit Exceeded";
    std::string_view NOT_EXTENDED_510                    = "Not Extended";
    std::string_view NETWORK_AUTHENTICATION_REQUIRED_511 = "Network Authentication Required";
    std::string_view UNKNOWN_ERROR_520                   = "Unknown Error";
    std::string_view WEB_SERVER_IS_DOWN_521              = "Web Server Is Down";
    std::string_view CONNECTION_TIMED_OUT_522            = "Connection Timed Out";
    std::string_view ORIGIN_IS_UNREACHABLE_523           = "Origin Is Unreachable";
    std::string_view TIMEOUT_OCCURRED_524                = "Timeout Occurred";
    std::string_view SSL_HANDSHAKE_FAILED_525            = "SSL Handshake Failed";
    std::string_view INVALID_SSL_CERTIFICATE_526         = "Invalid SSL Certificate";
}

namespace manapi::net::http::header {
    std::string_view CONTENT_RANGE       = "content-range";
    std::string_view CONTENT_LENGTH      = "content-length";
    std::string_view CONTENT_TYPE        = "content-type";
    std::string_view SET_COOKIE          = "set-cookie";
    std::string_view COOKIE              = "cookie";
    std::string_view ACCEPT              = "accept";
    std::string_view ACCEPT_LANGUAGE     = "accept-language";
    std::string_view ACCEPT_ENCODING     = "accept-encoding";
    std::string_view ACCEPT_RANGES       = "accept-ranges";
    std::string_view HOST                = "host";
    std::string_view USER_AGENT          = "user-agent";
    std::string_view CONNECTION          = "connection";
    std::string_view CACHE_CONTROL       = "cache-control";
    std::string_view EXPIRES             = "expires";
    std::string_view LAST_MODIFIED       = "last-modified";
    std::string_view ETAG                = "etag";
    std::string_view SERVER              = "server";
    std::string_view DATE                = "date";
    std::string_view LOCATION            = "location";
    std::string_view REFRESH             = "refresh";
    std::string_view PRAGMA              = "pragma";
    std::string_view CONTENT_DISPOSITION = "content-disposition";
    std::string_view CONTENT_ENCODING    = "content-encoding";
    std::string_view RANGE               = "range";
    std::string_view KEEP_ALIVE          = "keep-alive";
    std::string_view ALT_SVC             = "alt-svc";
    std::string_view AUTHORIZATION       = "authorization";
    std::string_view UPGRADE             = "upgrade";
    std::string_view EXPECT              = "expect";
    std::string_view TRANSFER_ENCODING   = "transfer-encoding";
    std::string_view PRIORITY            = "priority";
    std::string_view WARNING             = "warning";
}

manapi::error::status_or<std::string_view> manapi::net::http::status_to_string(int status) {
    auto const it = status_to_string_map.find(status);
    if (it != status_to_string_map.end())
        return it->second;

    return error::status_not_found("http:Status not found");
}
