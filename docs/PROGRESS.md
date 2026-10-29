# PROGRESS

> Update this file at the end of every phase (and whenever a decision is made). Newest entries at the top of each section.

## Current status
- **Phase:** 1 (WiFi & status) in progress. Phase 0 done.
- **Hardware in hand:** Waveshare ESP32-S3-LCD-4.3 only. EK-RA8D1 is **not available** (all PENDING-HW items blocked).

## Phase log
| Date | Phase | Result | Notes |
|---|---|---|---|
| 2026-10-29 | 0 | **Done** | Flashed via native USB (COM15). Saro confirmed: backlight on, boot screen readable, colours correct (LV_COLOR_16_SWAP=1 + swap565_t is right) |
| 2026-10-29 | 0 | Build OK | Skeleton, board_pins/app_config/secrets template, LGFX (touch removed), LVGL boot screen, CH422G boot init. Flash 27.6 %, RAM 8.1 %. Not yet flashed (board not connected) |
| 2026-10-29 | Planning | Docs created | Goal, hardware, protocols, structure, roadmap written |

## Decisions
| Date | Decision | Why |
|---|---|---|
| 2026-10-29 | SWDIO = GPIO6 (Sensor AD header), SWCLK = GPIO9 (I2C SCL, shared via arbiter) | Board has almost no free GPIOs; I2C ignores SCL toggling without a START condition |
| 2026-10-29 | Probe speaks standard CMSIS-DAP; host GDB server does the Cortex-M logic | Don't reinvent a GDB server; reuse pyOCD/OpenOCD |
| 2026-10-29 | Touch disabled until Phase 7 | Avoid GPIO9 conflicts during bring-up |
| 2026-10-29 | `build_unflags = -DCORE_DEBUG_LEVEL=0` + `-Iinclude` in platformio.ini | Arduino builder (`platformio-build.py:371`) hard-codes CORE_DEBUG_LEVEL=0 after our flags, which silently disabled all logs; libs under lib/ can't see include/ otherwise |
| 2026-10-29 | CH422G driven once at boot (LCD_RST released, touch held in reset, backlight on after first frame), then `Wire.end()` | Known state for the panel; GPIO9 free until the bus arbiter exists |
| 2026-10-29 | Git: `main` = baseline, work on `dev` (github.com/saravana2003/wireless_debugger) | Saro's request |
| (pending) | Host tool choice (OpenOCD TCP / pyOCD plugin / elaphureLink / USB-IP) | Decide in Phase 5 |

## Measurements
| Date | What | Value |
|---|---|---|
| | WiFi round-trip latency (min/avg/p95) | |
| | TCP throughput | |
| | Max SWCLK achieved | |

## Open questions
- Does the Sensor AD input (GPIO6) have an RC filter/divider on the Waveshare schematic?
- CH422G: init is now done at boot (see Decisions). EXIO3 = LCD_RST comes from community drivers for this board family (wiki gives EXIO1/2/4/5 only). Confirm in the schematic.
- Is the RA8D1 debug port ADIv6 (DPv3)? Which host tools support RA8D1 / Cortex-M85 today?
- Logic analyzer: Saro has one but not at hand. Not required before RA8 bring-up (Phase 3 timing is measured with the CPU cycle counter).

## Third-party code used
| Source | License | Files | Notes |
|---|---|---|---|
| Espressif `esp_io_expander_ch422g.c` (ESP32_IO_Expander) | Apache-2.0 | none copied | Used only as the reference for CH422G addresses 0x24 / 0x38 and the IO_OE bit |
