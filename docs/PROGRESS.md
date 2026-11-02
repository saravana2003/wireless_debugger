# PROGRESS

> Update this file at the end of every phase (and whenever a decision is made). Newest entries at the top of each section.

## >>> RESUME HERE (2026-11-03) <<<
**Where we are:** Phases 0-7 are done: the ESP32 side is complete, as far as it can be tested without a target. **Next:
Phase 8 with the real EK-RA8D1. Start at `docs/RA8_BRINGUP.md`** (jumpers, wiring, first commands, what each result means).

**Board state right now:** running the **real-SWD build** (`esp32-s3-waveshare-43` env, FW 0.7.0, green `REAL TARGET`
badge), IP **192.168.1.61**, TCP port **4441**. No reflash needed before wiring it up.

**Never run on real hardware yet:** real SWD signals at any speed, connecting to the RA8D1, flashing, Cortex-Debug
`launch.json`, holding SWDIO low during I2C. All tests so far used the simulated target, or the real pins with nothing wired.

**Host tools (git-ignored, in `.host/`):** `.host/venv` = pyOCD 0.46.0 + our plugin, `.host/openocd-latest` and
`.host/openocd` = OpenOCD builds, `.host/svd` = RA8D1 SVD. `tools/sim_server` = probe + SIM on the laptop, for testing
host tools without flashing. `tools/swd_hammer.py` = keeps the SWD pins busy, for touch tests.

**How to work with this board (learned the hard way):**
- PlatformIO CLI: `~/.platformio/penv/Scripts/pio.exe` (not on PATH). Builds: `pio run` (real, default env),
  `pio run -e esp32_sim`, tests: `pio test -e native` (24 tests). First build of an env ~2-4 min.
- Only the **UART** USB-C cable is connected (CH343, **COM16**); Saro has no second cable. Auto-reset into the
  bootloader does NOT work on this port. **Flashing: ask Saro to hold BOOT, tap RST, release BOOT**, then
  `pio run [-e esp32_sim] -t upload --upload-port COM16`, then ask him to **press RST** (it stays in download mode).
- Boot log: start `python tools/serial_log.py COM16 40` (PlatformIO's penv python has pyserial) in the background,
  then ask Saro to press RST. Don't toggle DTR/RTS (holding them gave `boot:0x0 DOWNLOAD`). It prints only when its window
  ends, misses roughly the first 3 s of boot, and **closing it can reset the board into download mode**. So put boot-time
  numbers on the screen, and if the board looks dead after a capture, press RST. (Upside: in that state you can flash
  without BOOT+RST.)
- `Write timeout` from esptool (seen once): unplug the USB-C, replug, redo BOOT+RST.
- Never run two `pio` builds at once in this folder (they corrupted each other once).
- Logs: modules must include `wdbg_log.h`, never `<esp_log.h>` (INFO/WARN get compiled out otherwise).
- Saro's WiFi creds are in git-ignored `include/secrets.h` (he once typed them into secrets.example.h by mistake;
  check that file before every commit).
- Git: repo github.com/saravana2003/wireless_debugger, work on branch **`dev`** (tracks origin/dev), `main` = baseline.
  Local identity: saravana2003 / sumithranand.1927@gmail.com. Commit trailer: `Co-Authored-By: Claude Opus 5.5`.

## Current status
- **Phase:** 0-7 done. Next: 8 (real target), see `docs/RA8_BRINGUP.md`.
- **Hardware in hand:** Waveshare ESP32-S3-LCD-4.3. EK-RA8D1 expected on 2026-11-03 (Saro), which unblocks the PENDING-HW items.

## Phase log
| Date | Phase | Result | Notes |
|---|---|---|---|
| 2026-11-03 | 7 | **Done** (ESP side) | Dashboard (DPIDR, SWD errors, clock in use, boot-test clocks, SIM/REAL badge, touch counters) and GT911 touch through the arbiter; controls: reset errors, SWCLK limit 1/2/4 MHz, full-screen IP. Coordinates checked by Saro (6,7 and 796,476 at the corners). Real-SWD build, nothing wired, `swd_hammer.py` 150 s: 15631 packets, 0 bus-busy errors, buttons worked normally throughout (Saro). 21 I2C read failures during that run, see Open questions. Bug found: "stuck finger" check used a `now` older than the last report, the unsigned subtraction wrapped, and every press was cancelled. NOT tested: touch during a session with a real target |
| 2026-11-02 | 6 | **Done** | The real limit was **no compiler optimization at all**: this framework's flags set no -O level, so everything we compile ran at -O0. `-O2` for the whole project + dedicated GPIO + hot paths forced inline in IRAM + DAP task on core 1 + delay calibration checked at boot. SWCLK max 1.28 -> 10.4 MHz (derived from CPU cycles, never measured on a wire). Default stays 1 MHz until proven on the target. Regression after -O2 over WiFi: GDB via pyOCD and OpenOCD pass, 15/15 reconnects |
| 2026-11-02 | 5 | **Done** | OpenOCD framing on port 4441, pyOCD plugin `tools/pyocd_wdbg`, SIM extended with ROM table + debuggable Cortex-M85 (`sim_core`), `tools/sim_server`, `docs/HOST_SETUP.md`, 37 native tests. Over real WiFi against the SIM build: GDB via pyOCD (target `r7fa8d1bh`) and via OpenOCD (latest + xPack): halt, registers, memory read/write, stepi, hardware breakpoint. Two SIM bugs found by the tools, fixed + tests added: C_HALT not set on debug entry (pyOCD step hung); FPB checked after advancing the PC (OpenOCD step timed out over WiFi, passed on localhost only by luck). NOT tested: Cortex-Debug `launch.json`, flashing (SIM has no flash), real SWD |
| 2026-10-30 | 5 | Research done | OpenOCD has an upstream cmsis-dap TCP backend (no RA flash driver); pyOCD supports RA8D1 incl. flash via Renesas pack but needs our TCP plugin. Recommendation presented, decision pending. Details: docs/research/PHASE5_HOST_TOOL_RESEARCH.md |
| 2026-10-29 | 4 | **Done** | ARM DAP.c V2.1.2 ported (2 marked changes) + our DAP_config.h/glue on top of swd_engine; swd_sim (DP + MEM-AP + 64 KB RAM); esp32_sim env; 24 native tests. Over WiFi against SIM: info, connect (DPIDR 0x0FA01001), write32/read32/dump round-trip, unmapped read -> FAULT. Bug found on HW: sim returned 0xFC for AP IDR (constant name shadowing), fixed + test added; re-verified on HW 2026-10-30: IDR 0x24770011 (MEM-AP), last RAM word 0x2000FFFC round-trips |
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
| 2026-10-29 | Probe only drives SWCLK/SWDIO between DAP_Connect and DAP_Disconnect; GPIO9 arbiter taken once per DAP packet | Don't disturb a target the host isn't using |
| 2026-11-02 | Host tool (Saro's choice): pyOCD + own plugin (primary), OpenOCD cmsis-dap tcp (secondary); firmware uses OpenOCD's TCP framing on port 4441 (raw framing removed) | Only pyOCD can flash RA8D1 (Renesas pack FLM); OpenOCD needs no host code and cross-checks the protocol |
| 2026-11-03 | With a debugger connected, SWDIO is driven **low** during I2C | I2C edges on SCL are SWCLK edges for the target. SWDIO high = read as a start bit + request -> parity error -> target stops answering. SWDIO low = idle cycles. Untested on a real target |
| 2026-11-03 | Touch: with a client connected, I2C only on GT911 INT (both edges) or while pressed; never within 200 ms of a host SWJ sequence; slow polling only with no client. Never waits for the bus | No I2C edges on the target's clock unless someone touches the screen; a connect sequence can't be corrupted |
| 2026-11-03 | Halted/running is not on the dashboard | Only the host knows it (it reads DHCSR through us like any memory), and pyOCD/OpenOCD don't report it via DAP_HostStatus. Roadmap item dropped unless someone wants to snoop DHCSR |
| 2026-11-02 | `-O2` for the whole project in platformio.ini (Saro's call) | Framework flags set no -O level. Flash 45.8% -> 36.3% at the same features. All regression tests pass |
| 2026-11-02 | DAP/TCP task on core 1, WiFi/lwIP stay on core 0 | Dedicated GPIO channels are per-core and created on core 1 by the boot self-test; WiFi can't stretch SWCLK and bit-banging can't starve WiFi. Median latency not worse once -O2 was on (see Measurements) |
| 2026-11-02 | SWCLK capped at 4 MHz, default stays 1 MHz | Raw 10.4 MHz exists but GPIO matrix input delay limits read setup; nothing proven on a wire yet |
| 2026-11-02 | Recommend openocd-org "latest" over xPack 0.12.0-7 | Both work; xPack spends ~10 s on Windows socket errors at connect (64 startup flush reads), latest has none |

## Measurements
| Date | What | Value |
|---|---|---|
| 2026-11-03 | Max SWCLK, no delay, -O2 + dedicated GPIO (boot self-test, screen) | **10.4 MHz** (before: 1.28 MHz; dedicated GPIO alone at -O0: 1.44 MHz) |
| 2026-11-03 | SWCLK at each setting (boot self-test) | 1 MHz -> 979 kHz, 2 MHz -> 1935 kHz, 4 MHz -> 3809 kHz. Never above the request. All from CPU cycle counts, not a logic analyzer |
| 2026-11-03 | CMSIS-DAP round trip x300, SIM, FW 0.7.0, 3 runs | median 7.7 / 5.7 / 6.5 ms, p95 77 / 16 / 20, max 609 / 203 / 271 ms |
| 2026-11-03 | Same at -O0 on core 1, real-SWD build, 4 runs | median 10.5-11.9 ms, avg 13-16 ms (why -O2 mattered for latency too) |
| 2026-11-03 | Memory reads (255 words), SIM, window 4, FW 0.7.0 | 141 KiB/s over 120 s (was ~95). No SWD wire time included: a real target will be limited by SWCLK |
| 2026-11-03 | `swd_hammer.py` 150 s, real-SWD build, nothing wired, touch in use | 15631 packets, 0 bus-busy errors; round trip median 6.6 / p95 17.5 / max 1392 ms |
| 2026-11-02 | CMSIS-DAP round trip (DAP_Info) x300, SIM build, FW 0.5.0 | min 3.8 / median 6.8 / avg 8.2 / p95 15.9 / max 51 ms |
| 2026-11-02 | Memory reads via DAP_TransferBlock (255 words), SIM, window 1 / 4, FW 0.5.0 | 50.7 / 94.9 KiB/s (one earlier run: 52.5 / 60.3; WiFi varies between runs) |
| 2026-10-29 | TCP echo latency, 64 B x 200, **display ON** | min 8.7 / median 81 / avg 187 / p95 498 / max 1363 ms |
| 2026-10-29 | TCP echo latency, 64 B x 200, **display OFF** (`WDBG_DISPLAY=0`) | min 2.9 / median 5.7 / avg 7.5 / p95 16.9 / max 118 ms |
| 2026-10-29 | TCP echo latency, 64 B x 200, **esp_lcd + bounce + bb_invalidate_cache** (current) | min 3.7 / median 9.7 / avg 14.0 / p95 36.7 / max 57 ms |
| 2026-10-29 | Echo throughput 1 KB, window 1 / 4, **esp_lcd + bounce + bb_invalidate_cache** (current) | 66 / 212 frames/s (66 / 212 KiB/s each way) |
| 2026-10-29 | TCP echo latency, 64 B x 200, **esp_lcd + 10-line bounce buffers** | min 3.6 / median 14.4 / avg 76.7 / p95 166 / max 4359 ms |
| 2026-10-29 | Echo throughput 1 KB, window 1 / 4, **esp_lcd + bounce** | 54 / 154 frames/s |
| 2026-10-29 | Echo throughput 1 KB frames, window 1 / 4, **display ON** | 3 / 8 frames/s (3.4 / 7.8 KiB/s each way) |
| 2026-10-29 | Echo throughput 1 KB frames, window 1 / 4, **display OFF** | 107 / 307 frames/s (107 / 307 KiB/s each way) |
| 2026-10-29 | ICMP ping laptop -> ESP32 (STA, power-save off), 20 pings | min 47 / avg 136 / max 502 ms (router: 2 ms) |
| 2026-10-29 | CMSIS-DAP round trip (DAP_Info) x300, SIM build | min 3.2 / median 5.9 / avg 6.6 / p95 11.2 / max 49 ms |
| 2026-10-29 | Memory reads via DAP_TransferBlock (255 words), SIM, window 1 / 4 | 82.5 / 101.7 KiB/s (no SWD wire time; real target @1 MHz will be ~20 KB/s) |
| 2026-10-29 | Max SWCLK, direct GPIO registers, no delay | **1283 kHz** (187 CPU cycles/bit), identical with IRQs masked. Conclusion at the time ("GPIO register access is the limit") was wrong: the build was -O0, see 2026-11-02 |
| 2026-10-29 | SWCLK at the 1 MHz default | 983 kHz measured |
| 2026-10-29 | DPIDR read, nothing connected | ACK 0x7 (pull-up), as expected |

## Open questions
- **21 I2C read failures during the touch + hammer test** (`fail 21` on the `Touch:` row). Buttons still worked. Not investigated. Could be GT911 timing, or I2C restarting right after SWCLK activity with a 20 ms timeout. Watch this number with a real target; it should not grow with touch idle.
- **Occasional long round trips over WiFi:** 1392 ms once in 150 s of hammering, 609 ms in one latency run. Not investigated. Probably WiFi; note if a debug session stutters.
- **Ping OK but TCP 4441 timing out, for several minutes after a flash on 2026-11-02, never explained.** It cleared after an RST and did not return in 25 + 15 reconnect tests. Saro's RST presses and my serial captures overlapped, so the board state at the time is uncertain. If it comes back: note the screen, press RST.
- **Holding SWDIO low during I2C is untested on a real target** (RA8_BRINGUP.md §6).
- **SWCLK has never been measured on a wire:** all numbers come from CPU cycle counts. Bring the logic analyzer to Phase 8 if possible. High clocks may also suffer from the I2C pull-ups and bus load on GPIO9.
- **Cortex-Debug `launch.json` in HOST_SETUP.md has not been run:** needs a real ELF + target. Phase 8.
- pyOCD 0.46.0's SVD parser throws in its `load-svd` thread on the Renesas SVD (`dim` format). Harmless to debugging; only commander's peripheral names are lost.
- Does the RA8D1 need anything special at connect (TrustZone/DLM state, ID code)? PENDING-HW.
- **mDNS:** board starts mDNS (screen shows `wdbg.local`), but Windows `Resolve-DnsName wdbg.local` fails. Windows mDNS is enabled and the firewall allows mDNS-In on Public. pktmon capture at the NIC (before the firewall): laptop sent 40 mDNS queries for wdbg.local in 5 s and received **0 mDNS packets from any device**. So the reply never reaches the laptop: the router is not delivering multicast to WiFi clients, or the board never sees the query. Windows is not the cause. Workaround: use the IP shown on screen.
- **RGB LCD vs WiFi (mostly solved):** LovyanGFX RGB made WiFi 25-40x worse. esp_lcd + bounce buffers + `bb_invalidate_cache` brought it to ~2x of display-off. That the cache flag helped so much supports the theory: frame-buffer reads evicted the cache that WiFi/lwIP buffers in PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1`) rely on. Phase 6 ideas: bigger bounce buffers, keeping WiFi buffers in internal RAM (custom sdkconfig).
- **Serial/flash over UART (CH343, COM16):** auto-reset into the bootloader does not work on this port from this laptop (esptool: "No serial data received"). Flashing needs manual BOOT+RST; after flashing press RST. Listening to the log works if nothing toggles DTR/RTS. Flashing over the native USB port does auto-reset fine.
- CH422G: init is now done at boot (see Decisions). EXIO3 = LCD_RST comes from community drivers for this board family (wiki gives EXIO1/2/4/5 only). Confirm in the schematic.
- Is the RA8D1 debug port ADIv6 (DPv3)? Host tools: both know Cortex-M85, pyOCD also has the RA8D1 pack. PENDING-HW.
- Logic analyzer: Saro has one but not at hand. Not required before RA8 bring-up (Phase 3 timing is measured with the CPU cycle counter).

## Third-party code used
| Source | License | Files | Notes |
|---|---|---|---|
| ARM CMSIS-DAP `DAP.c`, `DAP.h` V2.1.2 (github.com/ARM-software/CMSIS-DAP @ 1263659) | Apache-2.0 | `lib/dap_engine/DAP.c`, `DAP.h`, `LICENSE` | Copied with license headers. 2 changes, marked `WDBG:` + notice in the header: DAP_SetClockHook() call in Set_Clock_Delay(); PIN_DELAY_SLOW overridable (default is Thumb asm). `DAP_config.h`, `cmsis_compiler.h`, `dap_glue.cpp` are our own |
| Espressif `esp_lcd_touch_gt911.c` (esp-bsp) | Apache-2.0 | none copied | Reference for the GT911 reset/address sequence and report layout |
| OpenOCD `cmsis_dap_tcp.c`, `cortex_m.c` | GPL-2.0 | none copied | Read only, as the reference for the wire format and for host behaviour; our framing is our own code. OpenOCD itself is an unmodified external tool |
| pyOCD 0.46.0 | Apache-2.0 | none copied | Our plugin subclasses its classes (runtime dependency, not vendored) |
| ARM CMSIS-DAP `SW_DP.c` | Apache-2.0 | none copied | Reference only: swd_engine.h behaviour checked against it |
| Espressif `esp_io_expander_ch422g.c` (ESP32_IO_Expander) | Apache-2.0 | none copied | Used only as the reference for CH422G addresses 0x24 / 0x38 and the IO_OE bit |
