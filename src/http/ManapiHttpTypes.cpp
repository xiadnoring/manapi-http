#include "http/ManapiHttpTypes.hpp"
#include "../include/ManapiUtils.hpp"

static const std::map<uint16_t, std::string_view> status_to_string_map = {
    {100, manapi::net::http::S_CONTINUE_100},
    {101, manapi::net::http::S_SWITCHING_PROTOCOLS_101},
    {102, manapi::net::http::S_PROCESSING_102},
    {103, manapi::net::http::S_EARLY_HINTS_103},

    {200, manapi::net::http::S_OK_200},
    {201, manapi::net::http::S_CREATED_201},
    {202, manapi::net::http::S_ACCEPTED_202},
    {203, manapi::net::http::S_NON_AUTHORITATIVE_INFORMATION_203},
    {204, manapi::net::http::S_NO_CONTENT_204},
    {205, manapi::net::http::S_RESET_CONTENT_205},
    {206, manapi::net::http::S_PARTIAL_CONTENT_206},
    {207, manapi::net::http::S_MULTI_STATUS_207},
    {208, manapi::net::http::S_ALREADY_REPORTED_208},
    {226, manapi::net::http::S_IM_USED_226},

    {300, manapi::net::http::S_MULTIPLE_CHOICES_300},
    {301, manapi::net::http::S_MOVED_PERMANENTLY_301},
    {302, manapi::net::http::S_FOUND_302},
    {303, manapi::net::http::S_SEE_OTHER_303},
    {304, manapi::net::http::S_NOT_MODIFIED_304},
    {305, manapi::net::http::S_USE_PROXY_305},
    {307, manapi::net::http::S_TEMPORARY_REDIRECT_307},
    {308, manapi::net::http::S_PERMANENT_REDIRECT_308},

    {400, manapi::net::http::S_BAD_REQUEST_400},
    {401, manapi::net::http::S_UNAUTHORIZED_401},
    {402, manapi::net::http::S_PAYMENT_REQUIRED_402},
    {403, manapi::net::http::S_FORBIDDEN_403},
    {404, manapi::net::http::S_NOT_FOUND_404},
    {405, manapi::net::http::S_METHOD_NOT_ALLOWED_405},
    {406, manapi::net::http::S_NOT_ACCEPTABLE_406},
    {407, manapi::net::http::S_PROXY_AUTHENTICATION_REQUIRED_407},
    {408, manapi::net::http::S_REQUEST_TIMEOUT_408},
    {409, manapi::net::http::S_CONFLICT_409},
    {410, manapi::net::http::S_GONE_410},
    {411, manapi::net::http::S_LENGTH_REQUIRED_411},
    {412, manapi::net::http::S_PRECONDITION_FAILED_412},
    {413, manapi::net::http::S_PAYLOAD_TOO_LARGE_413},
    {414, manapi::net::http::S_URI_TOO_LONG_414},
    {415, manapi::net::http::S_UNSUPPORTED_MEDIA_TYPE_415},
    {416, manapi::net::http::S_RANGE_NOT_SATISFIABLE_416},
    {417, manapi::net::http::S_EXPECTATION_FAILED_417},
    {418, manapi::net::http::S_IM_A_TEAPOT_418},
    {419, manapi::net::http::S_AUTHENTICATION_TIMEOUT_419},
    {421, manapi::net::http::S_MISDIRECTED_REQUEST_421},
    {422, manapi::net::http::S_UNPROCESSABLE_ENTITY_422},
    {423, manapi::net::http::S_LOCKED_423},
    {424, manapi::net::http::S_FAILED_DEPENDENCY_424},
    {425, manapi::net::http::S_TOO_EARLY_425},
    {426, manapi::net::http::S_UPGRADE_REQUIRED_426},
    {428, manapi::net::http::S_PRECONDITION_REQUIRED_428},
    {429, manapi::net::http::S_TOO_MANY_REQUESTS_429},
    {431, manapi::net::http::S_REQUEST_HEADER_FIELDS_TOO_LARGE_431},
    {449, manapi::net::http::S_RETRY_WITH_449},
    {451, manapi::net::http::S_UNAVAILABLE_FOR_LEGAL_REASONS_451},
    {499, manapi::net::http::S_CLIENT_CLOSED_REQUEST_499},

    {500, manapi::net::http::S_INTERNAL_SERVER_ERROR_500},
    {501, manapi::net::http::S_NOT_IMPLEMENTED_501},
    {502, manapi::net::http::S_BAD_GATEWAY_502},
    {503, manapi::net::http::S_SERVICE_UNAVAILABLE_503},
    {504, manapi::net::http::S_GATEWAY_TIMEOUT_504},
    {505, manapi::net::http::S_HTTP_VERSION_NOT_SUPPORTED_505},
    {506, manapi::net::http::S_VARIANT_ALSO_NEGOTIATES_506},
    {507, manapi::net::http::S_INSUFFICIENT_STORAGE_507},
    {508, manapi::net::http::S_LOOP_DETECTED_508},
    {509, manapi::net::http::S_BANDWIDTH_LIMIT_EXCEEDED_509},
    {510, manapi::net::http::S_NOT_EXTENDED_510},
    {511, manapi::net::http::S_NETWORK_AUTHENTICATION_REQUIRED_511},
    {520, manapi::net::http::S_UNKNOWN_ERROR_520},
    {521, manapi::net::http::S_WEB_SERVER_IS_DOWN_521},
    {522, manapi::net::http::S_CONNECTION_TIMED_OUT_522},
    {523, manapi::net::http::S_ORIGIN_IS_UNREACHABLE_523},
    {524, manapi::net::http::S_TIMEOUT_OCCURRED_524},
    {525, manapi::net::http::S_SSL_HANDSHAKE_FAILED_525},
    {526, manapi::net::http::S_INVALID_SSL_CERTIFICATE_526}
};

manapi::status_or<std::string_view> manapi::net::http::status_to_string(uint16_t status) {
    auto const it = status_to_string_map.find(status);
    if (it != status_to_string_map.end())
        return it->second;

    return status_not_found("http:Status not found");
}
