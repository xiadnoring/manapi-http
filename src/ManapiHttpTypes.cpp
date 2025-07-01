#include "ManapiHttpTypes.hpp"

static const std::map<std::size_t, std::string_view> status_to_string_map = {
    {100, manapi::net::http::STATUS.CONTINUE_100},
    {101, manapi::net::http::STATUS.SWITCHING_PROTOCOLS_101},
    {102, manapi::net::http::STATUS.PROCESSING_102},
    {103, manapi::net::http::STATUS.EARLY_HINTS_103},

    {200, manapi::net::http::STATUS.OK_200},
    {201, manapi::net::http::STATUS.CREATED_201},
    {202, manapi::net::http::STATUS.ACCEPTED_202},
    {203, manapi::net::http::STATUS.NON_AUTHORITATIVE_INFORMATION_203},
    {204, manapi::net::http::STATUS.NO_CONTENT_204},
    {205, manapi::net::http::STATUS.RESET_CONTENT_205},
    {206, manapi::net::http::STATUS.PARTIAL_CONTENT_206},
    {207, manapi::net::http::STATUS.MULTI_STATUS_207},
    {208, manapi::net::http::STATUS.ALREADY_REPORTED_208},
    {226, manapi::net::http::STATUS.IM_USED_226},

    {300, manapi::net::http::STATUS.MULTIPLE_CHOICES_300},
    {301, manapi::net::http::STATUS.MOVED_PERMANENTLY_301},
    {302, manapi::net::http::STATUS.FOUND_302},
    {303, manapi::net::http::STATUS.SEE_OTHER_303},
    {304, manapi::net::http::STATUS.NOT_MODIFIED_304},
    {305, manapi::net::http::STATUS.USE_PROXY_305},
    {307, manapi::net::http::STATUS.TEMPORARY_REDIRECT_307},
    {308, manapi::net::http::STATUS.PERMANENT_REDIRECT_308},

    {400, manapi::net::http::STATUS.BAD_REQUEST_400},
    {401, manapi::net::http::STATUS.UNAUTHORIZED_401},
    {402, manapi::net::http::STATUS.PAYMENT_REQUIRED_402},
    {403, manapi::net::http::STATUS.FORBIDDEN_403},
    {404, manapi::net::http::STATUS.NOT_FOUND_404},
    {405, manapi::net::http::STATUS.METHOD_NOT_ALLOWED_405},
    {406, manapi::net::http::STATUS.NOT_ACCEPTABLE_406},
    {407, manapi::net::http::STATUS.PROXY_AUTHENTICATION_REQUIRED_407},
    {408, manapi::net::http::STATUS.REQUEST_TIMEOUT_408},
    {409, manapi::net::http::STATUS.CONFLICT_409},
    {410, manapi::net::http::STATUS.GONE_410},
    {411, manapi::net::http::STATUS.LENGTH_REQUIRED_411},
    {412, manapi::net::http::STATUS.PRECONDITION_FAILED_412},
    {413, manapi::net::http::STATUS.PAYLOAD_TOO_LARGE_413},
    {414, manapi::net::http::STATUS.URI_TOO_LONG_414},
    {415, manapi::net::http::STATUS.UNSUPPORTED_MEDIA_TYPE_415},
    {416, manapi::net::http::STATUS.RANGE_NOT_SATISFIABLE_416},
    {417, manapi::net::http::STATUS.EXPECTATION_FAILED_417},
    {418, manapi::net::http::STATUS.IM_A_TEAPOT_418},
    {419, manapi::net::http::STATUS.AUTHENTICATION_TIMEOUT_419},
    {421, manapi::net::http::STATUS.MISDIRECTED_REQUEST_421},
    {422, manapi::net::http::STATUS.UNPROCESSABLE_ENTITY_422},
    {423, manapi::net::http::STATUS.LOCKED_423},
    {424, manapi::net::http::STATUS.FAILED_DEPENDENCY_424},
    {425, manapi::net::http::STATUS.TOO_EARLY_425},
    {426, manapi::net::http::STATUS.UPGRADE_REQUIRED_426},
    {428, manapi::net::http::STATUS.PRECONDITION_REQUIRED_428},
    {429, manapi::net::http::STATUS.TOO_MANY_REQUESTS_429},
    {431, manapi::net::http::STATUS.REQUEST_HEADER_FIELDS_TOO_LARGE_431},
    {449, manapi::net::http::STATUS.RETRY_WITH_449},
    {451, manapi::net::http::STATUS.UNAVAILABLE_FOR_LEGAL_REASONS_451},
    {499, manapi::net::http::STATUS.CLIENT_CLOSED_REQUEST_499},

    {500, manapi::net::http::STATUS.INTERNAL_SERVER_ERROR_500},
    {501, manapi::net::http::STATUS.NOT_IMPLEMENTED_501},
    {502, manapi::net::http::STATUS.BAD_GATEWAY_502},
    {503, manapi::net::http::STATUS.SERVICE_UNAVAILABLE_503},
    {504, manapi::net::http::STATUS.GATEWAY_TIMEOUT_504},
    {505, manapi::net::http::STATUS.HTTP_VERSION_NOT_SUPPORTED_505},
    {506, manapi::net::http::STATUS.VARIANT_ALSO_NEGOTIATES_506},
    {507, manapi::net::http::STATUS.INSUFFICIENT_STORAGE_507},
    {508, manapi::net::http::STATUS.LOOP_DETECTED_508},
    {509, manapi::net::http::STATUS.BANDWIDTH_LIMIT_EXCEEDED_509},
    {510, manapi::net::http::STATUS.NOT_EXTENDED_510},
    {511, manapi::net::http::STATUS.NETWORK_AUTHENTICATION_REQUIRED_511},
    {520, manapi::net::http::STATUS.UNKNOWN_ERROR_520},
    {521, manapi::net::http::STATUS.WEB_SERVER_IS_DOWN_521},
    {522, manapi::net::http::STATUS.CONNECTION_TIMED_OUT_522},
    {523, manapi::net::http::STATUS.ORIGIN_IS_UNREACHABLE_523},
    {524, manapi::net::http::STATUS.TIMEOUT_OCCURRED_524},
    {525, manapi::net::http::STATUS.SSL_HANDSHAKE_FAILED_525},
    {526, manapi::net::http::STATUS.INVALID_SSL_CERTIFICATE_526}
};

std::string_view manapi::net::http::status_to_string(std::size_t status) {
    auto it = status_to_string_map.find(status);
    if (it != status_to_string_map.end()) {
        return it->second;
    }
    return STATUS.EMPTY;
}
