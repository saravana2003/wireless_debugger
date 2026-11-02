#include "framing_dap_tcp.h"

namespace net::framing_dap_tcp {

bool decode_request(const uint8_t hdr[HEADER_SIZE], size_t max_payload, uint16_t *len) {
    const uint32_t sig = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (static_cast<uint32_t>(hdr[3]) << 24);
    if (sig != SIGNATURE || hdr[6] != TYPE_REQUEST) return false;
    const uint16_t n = static_cast<uint16_t>(hdr[4] | (hdr[5] << 8));
    if (n == 0 || n > max_payload) return false;
    *len = n;
    return true;
}

void encode_response(uint16_t len, uint8_t hdr[HEADER_SIZE]) {
    hdr[0] = static_cast<uint8_t>(SIGNATURE & 0xFF);
    hdr[1] = static_cast<uint8_t>((SIGNATURE >> 8) & 0xFF);
    hdr[2] = static_cast<uint8_t>((SIGNATURE >> 16) & 0xFF);
    hdr[3] = static_cast<uint8_t>(SIGNATURE >> 24);
    hdr[4] = static_cast<uint8_t>(len & 0xFF);
    hdr[5] = static_cast<uint8_t>(len >> 8);
    hdr[6] = TYPE_RESPONSE;
    hdr[7] = 0;
}

}  // namespace net::framing_dap_tcp
