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
        extern std::string_view EMPTY;

        extern std::string_view CONTINUE_100;
        extern std::string_view SWITCHING_PROTOCOLS_101;
        extern std::string_view PROCESSING_102;
        extern std::string_view EARLY_HINTS_103;

        extern std::string_view OK_200;
        extern std::string_view CREATED_201;
        extern std::string_view ACCEPTED_202;
        extern std::string_view NON_AUTHORITATIVE_INFORMATION_203;
        extern std::string_view NO_CONTENT_204;
        extern std::string_view RESET_CONTENT_205;
        extern std::string_view PARTIAL_CONTENT_206;
        extern std::string_view MULTI_STATUS_207;
        extern std::string_view ALREADY_REPORTED_208;
        extern std::string_view IM_USED_226;

        extern std::string_view MULTIPLE_CHOICES_300;
        extern std::string_view MOVED_PERMANENTLY_301;
        extern std::string_view FOUND_302;
        extern std::string_view SEE_OTHER_303;
        extern std::string_view NOT_MODIFIED_304;
        extern std::string_view USE_PROXY_305;
        extern std::string_view TEMPORARY_REDIRECT_307;
        extern std::string_view PERMANENT_REDIRECT_308;

        extern std::string_view BAD_REQUEST_400;
        extern std::string_view UNAUTHORIZED_401;
        extern std::string_view PAYMENT_REQUIRED_402;
        extern std::string_view FORBIDDEN_403;
        extern std::string_view NOT_FOUND_404;
        extern std::string_view METHOD_NOT_ALLOWED_405;
        extern std::string_view NOT_ACCEPTABLE_406;
        extern std::string_view PROXY_AUTHENTICATION_REQUIRED_407;
        extern std::string_view REQUEST_TIMEOUT_408;
        extern std::string_view CONFLICT_409;
        extern std::string_view GONE_410;
        extern std::string_view LENGTH_REQUIRED_411;
        extern std::string_view PRECONDITION_FAILED_412;
        extern std::string_view PAYLOAD_TOO_LARGE_413;
        extern std::string_view URI_TOO_LONG_414;
        extern std::string_view UNSUPPORTED_MEDIA_TYPE_415;
        extern std::string_view RANGE_NOT_SATISFIABLE_416;
        extern std::string_view EXPECTATION_FAILED_417;
        extern std::string_view IM_A_TEAPOT_418;
        extern std::string_view AUTHENTICATION_TIMEOUT_419;
        extern std::string_view MISDIRECTED_REQUEST_421;
        extern std::string_view UNPROCESSABLE_ENTITY_422;
        extern std::string_view LOCKED_423;
        extern std::string_view FAILED_DEPENDENCY_424;
        extern std::string_view TOO_EARLY_425;
        extern std::string_view UPGRADE_REQUIRED_426;
        extern std::string_view PRECONDITION_REQUIRED_428;
        extern std::string_view TOO_MANY_REQUESTS_429;
        extern std::string_view REQUEST_HEADER_FIELDS_TOO_LARGE_431;
        extern std::string_view RETRY_WITH_449;
        extern std::string_view UNAVAILABLE_FOR_LEGAL_REASONS_451;
        extern std::string_view CLIENT_CLOSED_REQUEST_499;

        extern std::string_view INTERNAL_SERVER_ERROR_500;
        extern std::string_view NOT_IMPLEMENTED_501;
        extern std::string_view BAD_GATEWAY_502;
        extern std::string_view SERVICE_UNAVAILABLE_503;
        extern std::string_view GATEWAY_TIMEOUT_504;
        extern std::string_view HTTP_VERSION_NOT_SUPPORTED_505;
        extern std::string_view VARIANT_ALSO_NEGOTIATES_506;
        extern std::string_view INSUFFICIENT_STORAGE_507;
        extern std::string_view LOOP_DETECTED_508;
        extern std::string_view BANDWIDTH_LIMIT_EXCEEDED_509;
        extern std::string_view NOT_EXTENDED_510;
        extern std::string_view NETWORK_AUTHENTICATION_REQUIRED_511;
        extern std::string_view UNKNOWN_ERROR_520;
        extern std::string_view WEB_SERVER_IS_DOWN_521;
        extern std::string_view CONNECTION_TIMED_OUT_522;
        extern std::string_view ORIGIN_IS_UNREACHABLE_523;
        extern std::string_view TIMEOUT_OCCURRED_524;
        extern std::string_view SSL_HANDSHAKE_FAILED_525;
        extern std::string_view INVALID_SSL_CERTIFICATE_526;
    }

    /**
     * provides all existing http headers as a string
     */
    namespace header {
        extern std::string_view CONTENT_RANGE;
        extern std::string_view CONTENT_LENGTH;
        extern std::string_view CONTENT_TYPE;
        extern std::string_view SET_COOKIE;
        extern std::string_view COOKIE;
        extern std::string_view ACCEPT;
        extern std::string_view ACCEPT_LANGUAGE;
        extern std::string_view ACCEPT_ENCODING;
        extern std::string_view ACCEPT_RANGES;
        extern std::string_view HOST;
        extern std::string_view USER_AGENT;
        extern std::string_view CONNECTION;
        extern std::string_view CACHE_CONTROL;
        extern std::string_view EXPIRES;
        extern std::string_view LAST_MODIFIED;
        extern std::string_view ETAG;
        extern std::string_view SERVER;
        extern std::string_view DATE;
        extern std::string_view LOCATION;
        extern std::string_view REFRESH;
        extern std::string_view PRAGMA;
        extern std::string_view CONTENT_DISPOSITION;
        extern std::string_view CONTENT_ENCODING;
        extern std::string_view RANGE;
        extern std::string_view KEEP_ALIVE;
        extern std::string_view ALT_SVC;
        extern std::string_view AUTHORIZATION;
        extern std::string_view UPGRADE;
        extern std::string_view EXPECT;
        extern std::string_view TRANSFER_ENCODING;
        extern std::string_view PRIORITY;
        extern std::string_view WARNING;
    }

    /**
     * Get the http status as a string
     *
     * @param status Http Status
     * @return if the http status exists it returns the http status as a string, but otherwise, it returns the NotFound status
     */
    manapi::error::status_or<std::string_view> status_to_string (int status);
}