#pragma once
// framing_raw: our test framing, [u16 length, little-endian][payload].
// Pure C++ (no Arduino/lwIP) so it can be unit-tested in the native env.

#include <stddef.h>
#include <stdint.h>

namespace net::framing_raw {

constexpr size_t HEADER_SIZE = 2;

// Decode a header. Returns false if the length is 0 or larger than max_payload
// (the stream can't be resynchronised after that, so the caller drops the client).
bool decode_header(const uint8_t hdr[HEADER_SIZE], size_t max_payload, uint16_t *len);

// Encode a header for a payload of len bytes.
void encode_header(uint16_t len, uint8_t hdr[HEADER_SIZE]);

}  // namespace net::framing_raw
