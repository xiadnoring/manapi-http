#include "http/ManapiHttpTypes.hpp"
#include "../include/ManapiUtils.hpp"

static const std::map<uint16_t, std::string_view> status_to_string_map = {
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

DLLExportImport manapi::error::status_or<std::string_view> manapi::net::http::status_to_string(uint16_t status) {
    auto const it = status_to_string_map.find(status);
    if (it != status_to_string_map.end())
        return it->second;

    return error::status_not_found("http:Status not found");
}
