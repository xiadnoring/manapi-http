/**
 * @file http/ManapiHttpTypes.hpp
 * @brief Provides Http Types
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string_view>

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"

namespace manapi::net::http {
    /**
     * provides all existing http statuses
     */
    enum response_status {
        CONTINUE_100                        = 100,
        SWITCHING_PROTOCOLS_101             = 101,
        PROCESSING_102                      = 102,
        EARLY_HINTS_103                     = 103,
        OK_200                              = 200,
        CREATED_201                         = 201,
        ACCEPTED_202                        = 202,
        NON_AUTHORITATIVE_INFORMATION_203   = 203,
        NO_CONTENT_204                      = 204,
        RESET_CONTENT_205                   = 205,
        PARTIAL_CONTENT_206                 = 206,
        MULTI_STATUS_207                    = 207,
        ALREADY_REPORTED_208                = 208,
        IM_USED_226                         = 226,

        MULTIPLE_CHOICES_300                = 300,
        MOVED_PERMANENTLY_301               = 301,
        FOUND_302                           = 302,
        SEE_OTHER_303                       = 303,
        NOT_MODIFIED_304                    = 304,
        USE_PROXY_305                       = 305,
        TEMPORARY_REDIRECT_307              = 307,
        PERMANENT_REDIRECT_308              = 308,

        BAD_REQUEST_400                     = 400,
        UNAUTHORIZED_401                    = 401,
        PAYMENT_REQUIRED_402                = 402,
        FORBIDDEN_403                       = 403,
        NOT_FOUND_404                       = 404,
        METHOD_NOT_ALLOWED_405              = 405,
        NOT_ACCEPTABLE_406                  = 406,
        PROXY_AUTHENTICATION_REQUIRED_407   = 407,
        REQUEST_TIMEOUT_408                 = 408,
        CONFLICT_409                        = 409,
        GONE_410                            = 410,
        LENGTH_REQUIRED_411                 = 411,
        PRECONDITION_FAILED_412             = 412,
        PAYLOAD_TOO_LARGE_413               = 413,
        URI_TOO_LONG_414                    = 414,
        UNSUPPORTED_MEDIA_TYPE_415          = 415,
        RANGE_NOT_SATISFIABLE_416           = 416,
        EXPECTATION_FAILED_417              = 417,
        IM_A_TEAPOT_418                     = 418,
        /* ok */
        AUTHENTICATION_TIMEOUT_419          = 419,
        MISDIRECTED_REQUEST_421             = 421,
        UNPROCESSABLE_ENTITY_422            = 422,
        LOCKED_423                          = 423,
        FAILED_DEPENDENCY_424               = 424,
        TOO_EARLY_425                       = 425,
        UPGRADE_REQUIRED_426                = 426,
        PRECONDITION_REQUIRED_428           = 428,
        TOO_MANY_REQUESTS_429               = 429,
        REQUEST_HEADER_FIELDS_TOO_LARGE_431 = 431,
        RETRY_WITH_449                      = 449,
        UNAVAILABLE_FOR_LEGAL_REASONS_451   = 451,
        CLIENT_CLOSED_REQUEST_499           = 499,

        INTERNAL_SERVER_ERROR_500           = 500,
        NOT_IMPLEMENTED_501                 = 501,
        BAD_GATEWAY_502                     = 502,
        SERVICE_UNAVAILABLE_503             = 503,
        GATEWAY_TIMEOUT_504                 = 504,
        HTTP_VERSION_NOT_SUPPORTED_505      = 505,
        VARIANT_ALSO_NEGOTIATES_506         = 506,
        INSUFFICIENT_STORAGE_507            = 507,
        LOOP_DETECTED_508                   = 508,
        BANDWIDTH_LIMIT_EXCEEDED_509        = 509,
        NOT_EXTENDED_510                    = 510,
        NETWORK_AUTHENTICATION_REQUIRED_511 = 511,
        UNKNOWN_ERROR_520                   = 520,
        WEB_SERVER_IS_DOWN_521              = 521,
        CONNECTION_TIMED_OUT_522            = 522,
        ORIGIN_IS_UNREACHABLE_523           = 523,
        TIMEOUT_OCCURRED_524                = 524,
        SSL_HANDSHAKE_FAILED_525            = 525,
        INVALID_SSL_CERTIFICATE_526         = 526,
    };

    /**
     * provides all existing http status as a string
     */
    namespace status {
        inline std::string_view EMPTY                               = "Empty";

        inline std::string_view CONTINUE_100                        = "Continue";
        inline std::string_view SWITCHING_PROTOCOLS_101             = "Switching Protocols";
        inline std::string_view PROCESSING_102                      = "Processing";
        inline std::string_view EARLY_HINTS_103                     = "Early Hints";

        inline std::string_view OK_200                              = "OK";
        inline std::string_view CREATED_201                         = "Created";
        inline std::string_view ACCEPTED_202                        = "Accepted";
        inline std::string_view NON_AUTHORITATIVE_INFORMATION_203   = "Non-Authoritative Information";
        inline std::string_view NO_CONTENT_204                      = "No Content";
        inline std::string_view RESET_CONTENT_205                   = "Reset Content";
        inline std::string_view PARTIAL_CONTENT_206                 = "Partial Content";
        inline std::string_view MULTI_STATUS_207                    = "Multi-Status";
        inline std::string_view ALREADY_REPORTED_208                = "Already Reported";
        inline std::string_view IM_USED_226                         = "IM Used";

        inline std::string_view MULTIPLE_CHOICES_300                = "Multiple Choices";
        inline std::string_view MOVED_PERMANENTLY_301               = "Moved Permanently";
        inline std::string_view FOUND_302                           = "Found";
        inline std::string_view SEE_OTHER_303                       = "See Other";
        inline std::string_view NOT_MODIFIED_304                    = "Not Modified";
        inline std::string_view USE_PROXY_305                       = "Use Proxy";
        inline std::string_view TEMPORARY_REDIRECT_307              = "Temporary Redirect";
        inline std::string_view PERMANENT_REDIRECT_308              = "Permanent Redirect";

        inline std::string_view BAD_REQUEST_400                     = "Bad Request";
        inline std::string_view UNAUTHORIZED_401                    = "Unauthorized";
        inline std::string_view PAYMENT_REQUIRED_402                = "Payment Required";
        inline std::string_view FORBIDDEN_403                       = "Forbidden";
        inline std::string_view NOT_FOUND_404                       = "Not Found";
        inline std::string_view METHOD_NOT_ALLOWED_405              = "Method Not Allowed";
        inline std::string_view NOT_ACCEPTABLE_406                  = "Not Acceptable";
        inline std::string_view PROXY_AUTHENTICATION_REQUIRED_407   = "Proxy Authentication Required";
        inline std::string_view REQUEST_TIMEOUT_408                 = "Request Timeout";
        inline std::string_view CONFLICT_409                        = "Conflict";
        inline std::string_view GONE_410                            = "Gone";
        inline std::string_view LENGTH_REQUIRED_411                 = "Length Required";
        inline std::string_view PRECONDITION_FAILED_412             = "Precondition Failed";
        inline std::string_view PAYLOAD_TOO_LARGE_413               = "Payload Too Large";
        inline std::string_view URI_TOO_LONG_414                    = "URI Too Long";
        inline std::string_view UNSUPPORTED_MEDIA_TYPE_415          = "Unsupported Media Type";
        inline std::string_view RANGE_NOT_SATISFIABLE_416           = "Range Not Satisfiable";
        inline std::string_view EXPECTATION_FAILED_417              = "Expectation Failed";
        inline std::string_view IM_A_TEAPOT_418                     = "I'm a teapot";
        inline std::string_view AUTHENTICATION_TIMEOUT_419          = "Authentication Timeout";
        inline std::string_view MISDIRECTED_REQUEST_421             = "Misdirected Request";
        inline std::string_view UNPROCESSABLE_ENTITY_422            = "Unprocessable Entity";
        inline std::string_view LOCKED_423                          = "Locked";
        inline std::string_view FAILED_DEPENDENCY_424               = "Failed Dependency";
        inline std::string_view TOO_EARLY_425                       = "Too Early";
        inline std::string_view UPGRADE_REQUIRED_426                = "Upgrade Required";
        inline std::string_view PRECONDITION_REQUIRED_428           = "Precondition Required";
        inline std::string_view TOO_MANY_REQUESTS_429               = "Too Many Requests";
        inline std::string_view REQUEST_HEADER_FIELDS_TOO_LARGE_431 = "Request Header Fields Too Large";
        inline std::string_view RETRY_WITH_449                      = "Retry With";
        inline std::string_view UNAVAILABLE_FOR_LEGAL_REASONS_451   = "Unavailable For Legal Reasons";
        inline std::string_view CLIENT_CLOSED_REQUEST_499           = "Client Closed Request";

        inline std::string_view INTERNAL_SERVER_ERROR_500           = "Internal Server Error";
        inline std::string_view NOT_IMPLEMENTED_501                 = "Not Implemented";
        inline std::string_view BAD_GATEWAY_502                     = "Bad Gateway";
        inline std::string_view SERVICE_UNAVAILABLE_503             = "Service Unavailable";
        inline std::string_view GATEWAY_TIMEOUT_504                 = "Gateway Timeout";
        inline std::string_view HTTP_VERSION_NOT_SUPPORTED_505      = "HTTP Version Not Supported";
        inline std::string_view VARIANT_ALSO_NEGOTIATES_506         = "Variant Also Negotiates";
        inline std::string_view INSUFFICIENT_STORAGE_507            = "Insufficient Storage";
        inline std::string_view LOOP_DETECTED_508                   = "Loop Detected";
        inline std::string_view BANDWIDTH_LIMIT_EXCEEDED_509        = "Bandwidth Limit Exceeded";
        inline std::string_view NOT_EXTENDED_510                    = "Not Extended";
        inline std::string_view NETWORK_AUTHENTICATION_REQUIRED_511 = "Network Authentication Required";
        inline std::string_view UNKNOWN_ERROR_520                   = "Unknown Error";
        inline std::string_view WEB_SERVER_IS_DOWN_521              = "Web Server Is Down";
        inline std::string_view CONNECTION_TIMED_OUT_522            = "Connection Timed Out";
        inline std::string_view ORIGIN_IS_UNREACHABLE_523           = "Origin Is Unreachable";
        inline std::string_view TIMEOUT_OCCURRED_524                = "Timeout Occurred";
        inline std::string_view SSL_HANDSHAKE_FAILED_525            = "SSL Handshake Failed";
        inline std::string_view INVALID_SSL_CERTIFICATE_526         = "Invalid SSL Certificate";
    }

    /**
     * provides all existing http headers as a string
     */
    namespace header {
        inline std::string_view CONTENT_RANGE       = "content-range";
        inline std::string_view CONTENT_LENGTH      = "content-length";
        inline std::string_view CONTENT_TYPE        = "content-type";
        inline std::string_view SET_COOKIE          = "set-cookie";
        inline std::string_view COOKIE              = "cookie";
        inline std::string_view ACCEPT              = "accept";
        inline std::string_view ACCEPT_LANGUAGE     = "accept-language";
        inline std::string_view ACCEPT_ENCODING     = "accept-encoding";
        inline std::string_view ACCEPT_RANGES       = "accept-ranges";
        inline std::string_view HOST                = "host";
        inline std::string_view USER_AGENT          = "user-agent";
        inline std::string_view CONNECTION          = "connection";
        inline std::string_view CACHE_CONTROL       = "cache-control";
        inline std::string_view EXPIRES             = "expires";
        inline std::string_view LAST_MODIFIED       = "last-modified";
        inline std::string_view ETAG                = "etag";
        inline std::string_view SERVER              = "server";
        inline std::string_view DATE                = "date";
        inline std::string_view LOCATION            = "location";
        inline std::string_view REFRESH             = "refresh";
        inline std::string_view PRAGMA              = "pragma";
        inline std::string_view CONTENT_DISPOSITION = "content-disposition";
        inline std::string_view CONTENT_ENCODING    = "content-encoding";
        inline std::string_view RANGE               = "range";
        inline std::string_view KEEP_ALIVE          = "keep-alive";
        inline std::string_view ALT_SVC             = "alt-svc";
        inline std::string_view AUTHORIZATION       = "authorization";
        inline std::string_view UPGRADE             = "upgrade";
        inline std::string_view EXPECT              = "expect";
        inline std::string_view TRANSFER_ENCODING   = "transfer-encoding";
        inline std::string_view PRIORITY            = "priority";
        inline std::string_view WARNING             = "warning";
    }

    /**
     * Get the http status as a string
     *
     * @param status Http Status
     * @return if the http status exists it returns the http status as a string, but otherwise, it returns the NotFound status
     */
    manapi::error::status_or<std::string_view> status_to_string (uint16_t status);
}