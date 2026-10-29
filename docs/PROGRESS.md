# PROGRESS

> Update this file at the end of every phase (and whenever a decision is made). Newest entries at the top of each section.

## Current status
- **Phase:** 3 **done** (real-wire validation PENDING-HW). Next: Phase 4 (CMSIS-DAP engine + simulated target).
- **Hardware in hand:** Waveshare ESP32-S3-LCD-4.3 only. EK-RA8D1 is **not available** (all PENDING-HW items blocked).

## Phase log
| Date | Phase | Result | Notes |
|---|---|---|---|
| 2026-10-29 | 3 | **Done** | swd_engine (template, 12 native tests incl. a mutation check), ESP32 pin layer, bus_arbiter, boot self-test shown on screen. GPIO6 checked in schematic. Fixed: INFO/WARN logs were compiled out in modules that included esp_log.h directly (new `wdbg_log.h`) |
| 2026-10-29 | 2 | **Done** | `bb_invalidate_cache` on: avg latency 77 -> 14 ms, p95 166 -> 37 ms, max 4359 -> 57 ms; 66 / 212 frames/s (window 1 / 4). Boot log clean. Screen confirmed fine by Saro |
| 2026-10-29 | 2 | Display driver swap | LovyanGFX replaced by ESP-IDF esp_lcd RGB driver with bounce buffers (Saro's call: he saw the same issue before, lowering pclk didn't help). ~18x throughput vs LovyanGFX, still ~2x below display-off |
| 2026-10-29 | 2 | Server OK, perf blocker | Echo over TCP works, reconnects fine. A/B test proves the RGB LCD causes the bad WiFi (table in Measurements). Also fixed a boot loop: socket() before lwIP init asserted in sys_mutex_lock |
| 2026-10-29 | 1 | Partial | STA joins home WiFi (192.168.1.61), screen shows mode/SSID/RSSI/IP, board reports mDNS started. **`wdbg.local` does not resolve on the laptop** (see Open questions). Ping by IP OK, but slow: min 47 / avg 136 / max 502 ms (router itself: 2 ms) |
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
| 2026-10-29 | Display: LovyanGFX -> ESP-IDF `esp_lcd` RGB driver, bounce buffers, LVGL draw buffers in internal RAM, LV_COLOR_16_SWAP=0 | LovyanGFX+RGB made WiFi 25-40x slower; esp_lcd bounce buffers keep LCD DMA off the PSRAM bus |
| 2026-10-29 | GPIO6 OK for SWDIO: direct trace to J6 pin 3, no R/C (schematic checked), internal pull-up enabled | 02_HARDWARE §3 TODO closed |
| (pending) | Host tool choice (OpenOCD TCP / pyOCD plugin / elaphureLink / USB-IP) | Decide in Phase 5 |

## Measurements
| Date | What | Value |
|---|---|---|
| 2026-10-29 | TCP echo latency, 64 B x 200, **display ON** | min 8.7 / median 81 / avg 187 / p95 498 / max 1363 ms |
| 2026-10-29 | TCP echo latency, 64 B x 200, **display OFF** (`WDBG_DISPLAY=0`) | min 2.9 / median 5.7 / avg 7.5 / p95 16.9 / max 118 ms |
| 2026-10-29 | TCP echo latency, 64 B x 200, **esp_lcd + bounce + bb_invalidate_cache** (current) | min 3.7 / median 9.7 / avg 14.0 / p95 36.7 / max 57 ms |
| 2026-10-29 | Echo throughput 1 KB, window 1 / 4, **esp_lcd + bounce + bb_invalidate_cache** (current) | 66 / 212 frames/s (66 / 212 KiB/s each way) |
| 2026-10-29 | TCP echo latency, 64 B x 200, **esp_lcd + 10-line bounce buffers** | min 3.6 / median 14.4 / avg 76.7 / p95 166 / max 4359 ms |
| 2026-10-29 | Echo throughput 1 KB, window 1 / 4, **esp_lcd + bounce** | 54 / 154 frames/s |
| 2026-10-29 | Echo throughput 1 KB frames, window 1 / 4, **display ON** | 3 / 8 frames/s (3.4 / 7.8 KiB/s each way) |
| 2026-10-29 | Echo throughput 1 KB frames, window 1 / 4, **display OFF** | 107 / 307 frames/s (107 / 307 KiB/s each way) |
| 2026-10-29 | ICMP ping laptop -> ESP32 (STA, power-save off), 20 pings | min 47 / avg 136 / max 502 ms (router: 2 ms) |
| 2026-10-29 | Max SWCLK, direct GPIO registers, no delay | **1283 kHz** (187 CPU cycles/bit), identical with IRQs masked -> GPIO register access is the limit, not the LCD ISR |
| 2026-10-29 | SWCLK at the 1 MHz default | 983 kHz measured |
| 2026-10-29 | DPIDR read, nothing connected | ACK 0x7 (pull-up), as expected |

## Open questions
- **SWCLK delay model is approximate at small delays:** set_clock() assumes a linear delay-loop cost (measured 50 cycles at H=200), but at H=1 the real cost is lower (1 MHz request -> 983 kHz instead of the modelled ~830 kHz). Requests between ~830 and ~980 kHz may therefore run slightly faster than asked. Harmless for the RA8; fix in Phase 6 together with dedicated GPIO + a deterministic delay.
- **Max SWCLK 1.28 MHz** is limited by GPIO register writes (~60 cycles each). Phase 6: ESP32-S3 dedicated GPIO (CPU-instruction pin access).
- **mDNS:** board starts mDNS (screen shows `wdbg.local`), but Windows `Resolve-DnsName wdbg.local` fails. Windows mDNS is enabled and the firewall allows mDNS-In on Public. pktmon capture at the NIC (before the firewall): laptop sent 40 mDNS queries for wdbg.local in 5 s and received **0 mDNS packets from any device**. So the reply never reaches the laptop: the router is not delivering multicast to WiFi clients, or the board never sees the query. Windows is not the cause. Workaround: use the IP shown on screen.
- **RGB LCD vs WiFi (mostly solved):** LovyanGFX RGB made WiFi 25-40x worse. esp_lcd + bounce buffers + `bb_invalidate_cache` brought it to ~2x of display-off. That the cache flag helped so much supports the theory: frame-buffer reads evicted the cache that WiFi/lwIP buffers in PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1`) rely on. Phase 6 ideas: bigger bounce buffers, keeping WiFi buffers in internal RAM (custom sdkconfig).
- **Serial/flash over UART (CH343, COM16):** auto-reset into the bootloader does not work on this port from this laptop (esptool: "No serial data received"). Flashing needs manual BOOT+RST; after flashing press RST. Listening to the log works if nothing toggles DTR/RTS. Flashing over the native USB port does auto-reset fine.
- CH422G: init is now done at boot (see Decisions). EXIO3 = LCD_RST comes from community drivers for this board family (wiki gives EXIO1/2/4/5 only). Confirm in the schematic.
- Is the RA8D1 debug port ADIv6 (DPv3)? Which host tools support RA8D1 / Cortex-M85 today?
- Logic analyzer: Saro has one but not at hand. Not required before RA8 bring-up (Phase 3 timing is measured with the CPU cycle counter).

## Third-party code used
| Source | License | Files | Notes |
|---|---|---|---|
| ARM CMSIS-DAP `SW_DP.c`, `DAP.h` (github.com/ARM-software/CMSIS-DAP) | Apache-2.0 | none copied | Reference only: swd_engine.h behaviour and DAP_TRANSFER_* values checked against it |
| Espressif `esp_io_expander_ch422g.c` (ESP32_IO_Expander) | Apache-2.0 | none copied | Used only as the reference for CH422G addresses 0x24 / 0x38 and the IO_OE bit |
