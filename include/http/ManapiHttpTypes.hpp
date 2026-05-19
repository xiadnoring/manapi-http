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

    constexpr std::string_view S_EMPTY                               = "Empty";

    constexpr std::string_view S_CONTINUE_100                        = "Continue";
    constexpr std::string_view S_SWITCHING_PROTOCOLS_101             = "Switching Protocols";
    constexpr std::string_view S_PROCESSING_102                      = "Processing";
    constexpr std::string_view S_EARLY_HINTS_103                     = "Early Hints";

    constexpr std::string_view S_OK_200                              = "OK";
    constexpr std::string_view S_CREATED_201                         = "Created";
    constexpr std::string_view S_ACCEPTED_202                        = "Accepted";
    constexpr std::string_view S_NON_AUTHORITATIVE_INFORMATION_203   = "Non-Authoritative Information";
    constexpr std::string_view S_NO_CONTENT_204                      = "No Content";
    constexpr std::string_view S_RESET_CONTENT_205                   = "Reset Content";
    constexpr std::string_view S_PARTIAL_CONTENT_206                 = "Partial Content";
    constexpr std::string_view S_MULTI_STATUS_207                    = "Multi-Status";
    constexpr std::string_view S_ALREADY_REPORTED_208                = "Already Reported";
    constexpr std::string_view S_IM_USED_226                         = "IM Used";

    constexpr std::string_view S_MULTIPLE_CHOICES_300                = "Multiple Choices";
    constexpr std::string_view S_MOVED_PERMANENTLY_301               = "Moved Permanently";
    constexpr std::string_view S_FOUND_302                           = "Found";
    constexpr std::string_view S_SEE_OTHER_303                       = "See Other";
    constexpr std::string_view S_NOT_MODIFIED_304                    = "Not Modified";
    constexpr std::string_view S_USE_PROXY_305                       = "Use Proxy";
    constexpr std::string_view S_TEMPORARY_REDIRECT_307              = "Temporary Redirect";
    constexpr std::string_view S_PERMANENT_REDIRECT_308              = "Permanent Redirect";

    constexpr std::string_view S_BAD_REQUEST_400                     = "Bad Request";
    constexpr std::string_view S_UNAUTHORIZED_401                    = "Unauthorized";
    constexpr std::string_view S_PAYMENT_REQUIRED_402                = "Payment Required";
    constexpr std::string_view S_FORBIDDEN_403                       = "Forbidden";
    constexpr std::string_view S_NOT_FOUND_404                       = "Not Found";
    constexpr std::string_view S_METHOD_NOT_ALLOWED_405              = "Method Not Allowed";
    constexpr std::string_view S_NOT_ACCEPTABLE_406                  = "Not Acceptable";
    constexpr std::string_view S_PROXY_AUTHENTICATION_REQUIRED_407   = "Proxy Authentication Required";
    constexpr std::string_view S_REQUEST_TIMEOUT_408                 = "Request Timeout";
    constexpr std::string_view S_CONFLICT_409                        = "Conflict";
    constexpr std::string_view S_GONE_410                            = "Gone";
    constexpr std::string_view S_LENGTH_REQUIRED_411                 = "Length Required";
    constexpr std::string_view S_PRECONDITION_FAILED_412             = "Precondition Failed";
    constexpr std::string_view S_PAYLOAD_TOO_LARGE_413               = "Payload Too Large";
    constexpr std::string_view S_URI_TOO_LONG_414                    = "URI Too Long";
    constexpr std::string_view S_UNSUPPORTED_MEDIA_TYPE_415          = "Unsupported Media Type";
    constexpr std::string_view S_RANGE_NOT_SATISFIABLE_416           = "Range Not Satisfiable";
    constexpr std::string_view S_EXPECTATION_FAILED_417              = "Expectation Failed";
    constexpr std::string_view S_IM_A_TEAPOT_418                     = "I'm a teapot";
    constexpr std::string_view S_AUTHENTICATION_TIMEOUT_419          = "Authentication Timeout";
    constexpr std::string_view S_MISDIRECTED_REQUEST_421             = "Misdirected Request";
    constexpr std::string_view S_UNPROCESSABLE_ENTITY_422            = "Unprocessable Entity";
    constexpr std::string_view S_LOCKED_423                          = "Locked";
    constexpr std::string_view S_FAILED_DEPENDENCY_424               = "Failed Dependency";
    constexpr std::string_view S_TOO_EARLY_425                       = "Too Early";
    constexpr std::string_view S_UPGRADE_REQUIRED_426                = "Upgrade Required";
    constexpr std::string_view S_PRECONDITION_REQUIRED_428           = "Precondition Required";
    constexpr std::string_view S_TOO_MANY_REQUESTS_429               = "Too Many Requests";
    constexpr std::string_view S_REQUEST_HEADER_FIELDS_TOO_LARGE_431 = "Request Header Fields Too Large";
    constexpr std::string_view S_RETRY_WITH_449                      = "Retry With";
    constexpr std::string_view S_UNAVAILABLE_FOR_LEGAL_REASONS_451   = "Unavailable For Legal Reasons";
    constexpr std::string_view S_CLIENT_CLOSED_REQUEST_499           = "Client Closed Request";

    constexpr std::string_view S_INTERNAL_SERVER_ERROR_500           = "Internal Server Error";
    constexpr std::string_view S_NOT_IMPLEMENTED_501                 = "Not Implemented";
    constexpr std::string_view S_BAD_GATEWAY_502                     = "Bad Gateway";
    constexpr std::string_view S_SERVICE_UNAVAILABLE_503             = "Service Unavailable";
    constexpr std::string_view S_GATEWAY_TIMEOUT_504                 = "Gateway Timeout";
    constexpr std::string_view S_HTTP_VERSION_NOT_SUPPORTED_505      = "HTTP Version Not Supported";
    constexpr std::string_view S_VARIANT_ALSO_NEGOTIATES_506         = "Variant Also Negotiates";
    constexpr std::string_view S_INSUFFICIENT_STORAGE_507            = "Insufficient Storage";
    constexpr std::string_view S_LOOP_DETECTED_508                   = "Loop Detected";
    constexpr std::string_view S_BANDWIDTH_LIMIT_EXCEEDED_509        = "Bandwidth Limit Exceeded";
    constexpr std::string_view S_NOT_EXTENDED_510                    = "Not Extended";
    constexpr std::string_view S_NETWORK_AUTHENTICATION_REQUIRED_511 = "Network Authentication Required";
    constexpr std::string_view S_UNKNOWN_ERROR_520                   = "Unknown Error";
    constexpr std::string_view S_WEB_SERVER_IS_DOWN_521              = "Web Server Is Down";
    constexpr std::string_view S_CONNECTION_TIMED_OUT_522            = "Connection Timed Out";
    constexpr std::string_view S_ORIGIN_IS_UNREACHABLE_523           = "Origin Is Unreachable";
    constexpr std::string_view S_TIMEOUT_OCCURRED_524                = "Timeout Occurred";
    constexpr std::string_view S_SSL_HANDSHAKE_FAILED_525            = "SSL Handshake Failed";
    constexpr std::string_view S_INVALID_SSL_CERTIFICATE_526         = "Invalid SSL Certificate";

    /**
     * provides all existing http headers as a string
     */
    constexpr std::string_view H_CONTENT_RANGE       = "content-range";
    constexpr std::string_view H_CONTENT_LENGTH      = "content-length";
    constexpr std::string_view H_CONTENT_TYPE        = "content-type";
    constexpr std::string_view H_SET_COOKIE          = "set-cookie";
    constexpr std::string_view H_COOKIE              = "cookie";
    constexpr std::string_view H_ACCEPT              = "accept";
    constexpr std::string_view H_ACCEPT_LANGUAGE     = "accept-language";
    constexpr std::string_view H_ACCEPT_ENCODING     = "accept-encoding";
    constexpr std::string_view H_ACCEPT_RANGES       = "accept-ranges";
    constexpr std::string_view H_HOST                = "host";
    constexpr std::string_view H_USER_AGENT          = "user-agent";
    constexpr std::string_view H_CONNECTION          = "connection";
    constexpr std::string_view H_CACHE_CONTROL       = "cache-control";
    constexpr std::string_view H_EXPIRES             = "expires";
    constexpr std::string_view H_LAST_MODIFIED       = "last-modified";
    constexpr std::string_view H_ETAG                = "etag";
    constexpr std::string_view H_SERVER              = "server";
    constexpr std::string_view H_DATE                = "date";
    constexpr std::string_view H_LOCATION            = "location";
    constexpr std::string_view H_REFRESH             = "refresh";
    constexpr std::string_view H_PRAGMA              = "pragma";
    constexpr std::string_view H_CONTENT_DISPOSITION = "content-disposition";
    constexpr std::string_view H_CONTENT_ENCODING    = "content-encoding";
    constexpr std::string_view H_RANGE               = "range";
    constexpr std::string_view H_KEEP_ALIVE          = "keep-alive";
    constexpr std::string_view H_ALT_SVC             = "alt-svc";
    constexpr std::string_view H_AUTHORIZATION       = "authorization";
    constexpr std::string_view H_UPGRADE             = "upgrade";
    constexpr std::string_view H_EXPECT              = "expect";
    constexpr std::string_view H_TRANSFER_ENCODING   = "transfer-encoding";
    constexpr std::string_view H_PRIORITY            = "priority";
    constexpr std::string_view H_WARNING             = "warning";


    /**
     * Get the http status as a string
     *
     * @param status Http Status
     * @return if the http status exists it returns the http status as a string, but otherwise, it returns the NotFound status
     */
    manapi::status_or<std::string_view> status_to_string (uint16_t status);
}