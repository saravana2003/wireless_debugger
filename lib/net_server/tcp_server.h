#pragma once
// tcp_server: single-client TCP server with raw framing. Each received frame is passed to a
// handler, and the handler's response is sent back as one frame.
//
// Phase 2: the handler echoes. Phase 4: the handler is the CMSIS-DAP engine.

#include <stddef.h>
#include <stdint.h>

namespace net {

// Process one request. Write the response into resp (capacity resp_cap) and return its length.
// Return 0 to send no response.
using FrameHandler = size_t (*)(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap);

// Start the server task (core 0). Non-blocking.
void tcp_start(FrameHandler handler);

}  // namespace net
