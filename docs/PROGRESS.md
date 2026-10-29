# PROGRESS

> Update this file at the end of every phase (and whenever a decision is made). Newest entries at the top of each section.

## Current status
- **Phase:** 0 (not started)
- **Hardware in hand:** Waveshare ESP32-S3-LCD-4.3 only. EK-RA8D1 is **not available** (all PENDING-HW items blocked).

## Phase log
| Date | Phase | Result | Notes |
|---|---|---|---|
| 2026-10-29 | Planning | Docs created | Goal, hardware, protocols, structure, roadmap written |

## Decisions
| Date | Decision | Why |
|---|---|---|
| 2026-10-29 | SWDIO = GPIO6 (Sensor AD header), SWCLK = GPIO9 (I2C SCL, shared via arbiter) | Board has almost no free GPIOs; I2C ignores SCL toggling without a START condition |
| 2026-10-29 | Probe speaks standard CMSIS-DAP; host GDB server does the Cortex-M logic | Don't reinvent a GDB server; reuse pyOCD/OpenOCD |
| 2026-10-29 | Touch disabled until Phase 7 | Avoid GPIO9 conflicts during bring-up |
| (pending) | Host tool choice (OpenOCD TCP / pyOCD plugin / elaphureLink / USB-IP) | Decide in Phase 5 |

## Measurements
| Date | What | Value |
|---|---|---|
| | WiFi round-trip latency (min/avg/p95) | |
| | TCP throughput | |
| | Max SWCLK achieved | |

## Open questions
- Does the Sensor AD input (GPIO6) have an RC filter/divider on the Waveshare schematic?
- Does the CH422G need init for backlight/LCD reset, or are the defaults ok?
- Is the RA8D1 debug port ADIv6 (DPv3)? Which host tools support RA8D1 / Cortex-M85 today?
- Does Saro have a logic analyzer for SWD waveform checks?

## Third-party code used
| Source | License | Files | Notes |
|---|---|---|---|
| | | | |
