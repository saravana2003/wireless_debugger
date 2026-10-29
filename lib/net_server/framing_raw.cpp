#include "framing_raw.h"

namespace net::framing_raw {

bool decode_header(const uint8_t hdr[HEADER_SIZE], size_t max_payload, uint16_t *len) {
    const uint16_t n = static_cast<uint16_t>(hdr[0] | (hdr[1] << 8));
    if (n == 0 || n > max_payload) return false;
    *len = n;
    return true;
}

void encode_header(uint16_t len, uint8_t hdr[HEADER_SIZE]) {
    hdr[0] = static_cast<uint8_t>(len & 0xFF);
    hdr[1] = static_cast<uint8_t>(len >> 8);
}

}  // namespace net::framing_raw
