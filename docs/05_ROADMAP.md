# 05 — Roadmap

Each phase ends with: build passes → tested → `docs/PROGRESS.md` updated → Saro told what he can see or test.
**PENDING-HW** = needs the EK-RA8D1. Skip until Saro says the board is back.

---

## Phase 0: Project skeleton & display bring-up
**Goal:** a clean project that boots and shows a screen.
- [ ] Replace `platformio.ini` with Saro's config (`04_PROJECT_STRUCTURE.md` §2). Confirm first.
- [ ] Create the folder layout, `include/board_pins.h`, `include/app_config.h`, `secrets.example.h`, `.gitignore`.
- [ ] Bring in `LGFX_Config.h`, `lv_conf.h` (+ `lvgl/lvgl.h` shim only if needed) from `docs/reference/`.
      **Disable touch for now** (don't let LGFX own the I2C bus on GPIO8/9). Move the pin numbers to `board_pins.h`.
- [ ] Check whether the CH422G must be initialised for backlight/LCD reset (wiki/schematic). Do it once at boot if needed.
- [ ] LVGL boot screen: "Wireless Debugger", FW version, "WiFi: —", "Client: —", "Target: —".
**Done when:** `pio run -t upload` works, the screen shows the boot UI, and the serial log is clean.

## Phase 1: WiFi & status
- [ ] STA mode with creds from `secrets.h`. If it fails within N s → fall back to **AP mode** (SSID `WDBG-xxxx`,
      WPA2 password from secrets).
- [ ] mDNS `wdbg.local`.
- [ ] Screen shows mode, SSID, IP, RSSI. Reconnects automatically.
**Done when:** Saro can `ping wdbg.local` from the laptop and the IP is on screen.

## Phase 2: TCP server + raw framing + host test client
- [ ] `net_server`: single-client TCP server, `TCP_NODELAY`, `framing_raw` (`[u16 len][payload]`).
- [ ] For now, reply to an echo/ping command only.
- [ ] `tools/dap_client.py`: `ping`, `latency` (1000 round trips → min/avg/max/p95), `throughput`.
- [ ] Screen: client IP, packets/s, bytes/s, last latency.
**Done when:** latency and throughput numbers are recorded in PROGRESS.md (this tells us how much batching we need).

## Phase 3: SWD PHY (real pins) + bus arbiter
- [ ] `bus_arbiter`: acquire/release SWD vs I2C on GPIO9 (detach/attach via the GPIO matrix), plus a mutex.
- [ ] `swd_phy` interface: `line_reset`, `sequence(bits)`, `transfer(req, *data) → ack`, `set_clock(hz)`,
      turnaround/idle config.
- [ ] Real implementation with direct register access on GPIO6 (SWDIO) / GPIO9 (SWCLK), IRAM.
- [ ] Verify the GPIO6 analog-front-end question (02_HARDWARE §3 TODO).
- [ ] Without a target: verify **timing** by toggling and measuring achievable SWCLK frequency (log cycles via
      `esp_cpu_get_cycle_count`). If Saro has a logic analyzer, capture a waveform. Ask him.
**Done when:** the PHY compiles and is unit-tested against the SIM model; measured max SWCLK is recorded. **Real-wire
validation is PENDING-HW.**

## Phase 4: CMSIS-DAP engine + simulated target
- [ ] Port `DAP.c` / `SW_DP.c` (check the license) with our `DAP_config.h` → `swd_phy`.
- [ ] Implement `swd_phy_sim` (DP + MEM-AP + RAM + minimal SCS: see 03_PROTOCOLS §4).
- [ ] `esp32_sim` env (`-DWDBG_TARGET_SIM=1`) and `native` env unit tests (`test/test_dap_engine`).
- [ ] Wire it up: TCP → dap_engine → swd_phy (real or sim).
- [ ] Extend `dap_client.py`: `info`, `connect`, `read32`, `write32`, `dump`.
**Done when:** over WiFi, `dap_client.py` reads DPIDR, writes then reads back RAM in the SIM target, and the unit
tests pass.

## Phase 5: Host tool integration (decision first!)
- [ ] Research the options in 03_PROTOCOLS §5 (current OpenOCD/pyOCD capabilities, RA8D1/Cortex-M85 support,
      Windows friendliness). **Present a recommendation to Saro and wait for approval.**
- [ ] Implement the matching framing on the ESP32 (`framing_<host>`) and/or the host plugin/bridge in `tools/`.
- [ ] Against SIM: the host tool connects, reads memory, and (ideally) halts the "core" in the sim model.
- [ ] Write `docs/HOST_SETUP.md` (Windows: install the tool, config files, Cortex-Debug `launch.json` example).
**Done when:** the chosen standard tool talks to the ESP32 over WiFi and reads SIM memory.

## Phase 6: Performance pass
- [ ] Support `DAP_Info` packet count > 1 (pipelining), tune packet size, socket buffers, task priorities.
- [ ] Optimize the SWD bit loop (dedicated GPIO / tighter loop). Target ≥ 2–4 MHz if feasible. (Phase 3 baseline:
      1.28 MHz max with GPIO registers; delay model inaccurate at small delays, see PROGRESS open questions.)
- [ ] Record before/after numbers in PROGRESS.md.

## Phase 7: UI dashboard + touch via the arbiter
- [ ] Dashboard: WiFi/IP, client, target state (DPIDR, halted/running), packets/s, errors, SWD clock.
- [ ] Re-enable GT911 touch, read through the arbiter (no LGFX auto-ownership of GPIO9).
- [ ] Touch controls: SIM/real indicator, reset stats, SWD clock select, show IP as a big label.
**Done when:** touch works while a host client hammers the probe, with no SWD errors or I2C glitches.

---

## Phase 8: Real target bring-up (PENDING-HW)
- [ ] EK-RA8D1: **close J9**, keep J6/J8/J29 at defaults (02_HARDWARE §6). Power via J10 or J11. Wire GND, SWDIO,
      SWCLK to J13.
- [ ] Read DPIDR, power up the debug domain, read the ROM table / CPUID. Note whether it's ADIv6.
- [ ] Halt / resume / single-step via the host tool.
- [ ] Flash a blinky built with Renesas FSP (e2 studio or VS Code + FSP), set a breakpoint, step, watch variables.
- [ ] Cortex-Debug Live Watch on a global counter.

## Phase 9: Stretch (later)
- [ ] RTT over the link (the host tool polls the RTT control block in RAM).
- [ ] Live variable plot on the ESP32 screen (read an address periodically, between host packets).
- [ ] Web page on the ESP32 for config (WiFi creds, port) instead of reflashing.
