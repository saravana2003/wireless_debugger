# CLAUDE.md — Wireless Debugger (ESP32-S3 → EK-RA8D1)

You are working on **Wireless_debugger**, a PlatformIO project. It turns a **Waveshare ESP32-S3-LCD-4.3** board into a
**wireless SWD debug probe** (like a J-Link / ST-Link, but over WiFi) for a **Renesas EK-RA8D1** board
(R7FA8D1BHECBD, Arm Cortex-M85).

The owner is **Saro**, an embedded engineer (automotive) who is also learning. Explain decisions in plain language
first, then the technical detail. Do one small, well-defined step at a time.

---

## 0. START HERE (do this before writing any code)

1. Read every file in `docs/`, in this order:
   1. `docs/01_GOAL_AND_ARCHITECTURE.md`: what we are building and why
   2. `docs/02_HARDWARE.md`: boards, pins, wiring, power, RA8 jumpers
   3. `docs/03_PROTOCOLS.md`: SWD, CMSIS-DAP, the WiFi transport, and the host-side tool options
   4. `docs/04_PROJECT_STRUCTURE.md`: folder layout, modules, coding rules
   5. `docs/05_ROADMAP.md`: phases, tasks, and "definition of done" for each
   6. `docs/PROGRESS.md`: what has already been done (the source of truth for "where are we")
2. Look at `docs/reference/`. These are **known-good files from Saro's previous project** on this same board
   (LovyanGFX display/touch config, LVGL config, a SquareLine include shim). Use them as the starting point for
   the display. Don't rewrite them from scratch.
3. Reference PDFs are one folder up, in `D:\WIRELESS_DEBUGGER\` (i.e. `../`):
   - `r20ut5205eg0102-ek-ra8d1-v1-um.pdf`: EK-RA8D1 user's manual (debug modes, J13/J20 pinout, jumpers)
   - `R7FA8D1BHECBD_DATASHEET.pdf`: RA8D1 MCU datasheet
4. **Reply to Saro with a short summary** of your understanding: the goal, the current phase, and the next 3 concrete
   steps. **Wait for his OK** before starting the work.

---

## 1. Ground rules (always follow these)

### Scope and honesty
- **The RA8 board is NOT available right now.** All work happens on the ESP32 board alone. Anything that needs the
  real target is marked **PENDING-HW** in the roadmap. Don't fake "it works on the target". Use the **simulated
  target** (see roadmap Phase 4) for testing instead.
- **Never invent register addresses, IDs, protocol command codes, or pin numbers.** Take them from the ARM specs
  (ADIv5/ADIv6, CMSIS-DAP docs), the ESP-IDF/Arduino docs, the Waveshare wiki and schematic, or the RA8 PDFs.
  If you can't verify something, say so and put it in `PROGRESS.md` under *Open questions*.
- If something in these docs turns out to be wrong (e.g. a pin is not actually free), **stop, tell Saro, and update
  the doc**. Don't silently work around it.

### Workflow
- Work **phase by phase** (`docs/05_ROADMAP.md`). Don't jump ahead.
- Before each phase: state the plan in 3–6 bullets. After each phase: build, test, then update `docs/PROGRESS.md`.
- Keep changes small and buildable. `pio run` must succeed after every step.
- Ask before: adding a new library, changing `platformio.ini` beyond the planned edits, changing the pin plan,
  or choosing between the host-tool options in `03_PROTOCOLS.md`.
- Copying code from open-source projects (ARM CMSIS-DAP `DAP.c`/`SW_DP.c`, windowsair `wireless-esp8266-dap`,
  etc.) is allowed **only after checking that the license is compatible** (Apache-2.0 / MIT / BSD are fine). Keep the
  license header and record the source in `docs/PROGRESS.md` → *Third-party code*.

### Hardware safety
- **All pin numbers live in ONE file: `include/board_pins.h`.** No GPIO numbers anywhere else.
- The ESP32 strapping pins (GPIO0, 3, 45, 46) are already used by the display. Never reconfigure them.
- **GPIO9 is shared between I2C SCL (touch + CH422G IO-expander) and SWCLK.** Every use of GPIO9 must go through the
  **bus arbiter** (`lib/bus_arbiter`). Never drive GPIO9 directly from SWD or I2C code. See `02_HARDWARE.md` §4.
- Never connect or drive the 3V3 rails between the boards. Only GND, SWDIO, and SWCLK are wired.

### Code quality
- C++17, Arduino framework on ESP-IDF underneath. FreeRTOS tasks where it helps. **No blocking calls in the WiFi or
  LVGL paths.**
- Each module is a library under `lib/` with its own header, so it can be unit-tested in the `native` env.
- Log with `ESP_LOGx` and a per-module TAG. No stray `Serial.print` spam in hot paths (SWD loop).
- WiFi credentials go in `include/secrets.h`, which is git-ignored. Commit `include/secrets.example.h` instead.

### Communication style with Saro
- Plain-language intuition first, then the technical detail. One concept at a time; no giant dumps.
- When a phase is done, tell him **what he can physically see or test** (e.g. "the screen shows the IP; run
  `python tools/dap_client.py --ip ... ping`").
