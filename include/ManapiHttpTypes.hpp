#pragma once

#include <string>
#include <map>
#include <optional>

#include "ManapiUtils.hpp"
#include "ManapiErrors.hpp"

namespace manapi::net::http {
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
    static const struct {
        std::string EMPTY                               = "Empty";

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

    } STATUS;

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
        std::string UPGRADE             = "upgrade";
        std::string EXPECT              = "expect";
        std::string TRANSFER_ENCODING   = "transfer-encoding";
    } HEADER;

    std::string_view status_to_string (const std::size_t &status);
}