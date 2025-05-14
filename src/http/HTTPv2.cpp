#include "http/HTTPv2.hpp"

#include "encoding/ManapiUnicode.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "encoding/ManapiURL.hpp"
#include "components/ManapiURLDecodeStream.hpp"

int manapi::net::http::http_v2_work(http_v2_t *ctx, http::config *config) {

    return EHTTP_V2_PROTOCOL_OK;
}
