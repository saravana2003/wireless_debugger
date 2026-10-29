#pragma once
// stats: lock-free counters shared between the network/DAP tasks (writers) and the UI (reader).

#include <atomic>
#include <stdint.h>

namespace stats {

struct Counters {
    std::atomic<uint32_t> rx_frames{0};
    std::atomic<uint32_t> tx_frames{0};
    std::atomic<uint32_t> rx_bytes{0};
    std::atomic<uint32_t> tx_bytes{0};
    std::atomic<uint32_t> errors{0};       // framing errors, socket errors, rejected clients
    std::atomic<uint32_t> client_ip{0};    // IPv4, network byte order; 0 = no client
    std::atomic<uint16_t> client_port{0};
};

inline Counters g;

inline void add(std::atomic<uint32_t> &c, uint32_t n = 1) { c.fetch_add(n, std::memory_order_relaxed); }

}  // namespace stats
