#pragma once
// framing_dap_tcp: the wire format of OpenOCD's "cmsis-dap backend tcp" (src/jtag/drivers/cmsis_dap_tcp.c).
// Our pyOCD plugin and tools/dap_client.py speak it too. All fields are little-endian:
//
//   offset 0  u32 signature    0x00504144 ("DAP\0")
//   offset 4  u16 length       payload bytes, header excluded
//   offset 6  u8  packet type  1 = request (host -> probe), 2 = response (probe -> host)
//   offset 7  u8  reserved     0
//   offset 8  one CMSIS-DAP packet
//
// Pure C++ (no Arduino/lwIP) so it can be unit-tested in the native env.

#include <stddef.h>
#include <stdint.h>

namespace net::framing_dap_tcp {

constexpr size_t HEADER_SIZE = 8;
constexpr uint32_t SIGNATURE = 0x00504144u;
constexpr uint8_t TYPE_REQUEST = 1;
constexpr uint8_t TYPE_RESPONSE = 2;

// Decode a request header. Returns false on a wrong signature or packet type, or a length that is
// 0 or larger than max_payload (the stream can't be resynchronised, so the caller drops the client).
bool decode_request(const uint8_t hdr[HEADER_SIZE], size_t max_payload, uint16_t *len);

// Encode a response header for a payload of len bytes.
void encode_response(uint16_t len, uint8_t hdr[HEADER_SIZE]);

}  // namespace net::framing_dap_tcp
