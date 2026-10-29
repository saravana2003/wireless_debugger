# 01 — Goal and Architecture

## 1. The goal, in one sentence
Debug the EK-RA8D1 from VS Code **exactly like with a J-Link**: breakpoints, step over/into, run/halt, registers,
memory view, live variable watch and (later) plotting. The difference is that there is **no USB probe cable to the
target**: the ESP32-S3 sits next to the RA8 board and talks to the laptop over **WiFi**.

## 2. How a normal wired debugger works (the mental model)
```
VS Code (Cortex-Debug extension)
   │  Debug Adapter Protocol
GDB (arm-none-eabi-gdb)
   │  GDB Remote Serial Protocol over TCP (localhost)
GDB server (OpenOCD / pyOCD / JLinkGDBServer)    ← knows Cortex-M, flash algorithms, breakpoints
   │  USB (CMSIS-DAP commands, or J-Link's closed protocol)
Debug probe (J-Link / ST-Link / CMSIS-DAP)        ← dumb-ish: turns commands into wire wiggles
   │  SWD: 2 wires (SWDIO, SWCLK) + GND
Target MCU's CoreSight Debug Access Port (DAP)    ← built into the RA8; can halt CPU, read/write any address
```
**Key insight:** the probe is only a translator. All the "smart" debugging logic lives in the GDB server on the
laptop. So we only need to build the **probe** part, and make it talk over WiFi instead of USB.

## 3. Our architecture
```
┌──────────────────────── Laptop (Windows) ────────────────────────┐
│ VS Code + Cortex-Debug                                           │
│   → arm-none-eabi-gdb                                            │
│     → GDB server (pyOCD or OpenOCD, see 03_PROTOCOLS.md §5)       │
│        → network backend / small host bridge (tools/)            │
└───────────────────────────────┬──────────────────────────────────┘
                                │ WiFi, TCP  (CMSIS-DAP packets, framed)
┌───────────────────────────────▼──────────────────────────────────┐
│ ESP32-S3 (Waveshare 4.3")                                        │
│  net_server   : TCP server, framing, one client at a time        │
│  dap_engine   : CMSIS-DAP command processor (DAP_Info, Transfer…)│
│  swd_phy      : bit-banged SWD on 2 GPIOs (or SIM target)        │
│  bus_arbiter  : shares GPIO9 between SWCLK and I2C (touch)       │
│  ui           : LVGL status dashboard on the 800×480 screen      │
└───────────────────────────────┬──────────────────────────────────┘
                                │ SWDIO (GPIO6), SWCLK (GPIO9), GND
┌───────────────────────────────▼──────────────────────────────────┐
│ EK-RA8D1, connector J13 (10-pin Cortex debug), "Debug In" mode   │
└──────────────────────────────────────────────────────────────────┘
```

## 4. Design principles
1. **Standard protocol on the wire (CMSIS-DAP).** Then existing GDB servers (pyOCD/OpenOCD) do the heavy lifting:
   Cortex-M85 support, flash programming, breakpoints. We don't write a GDB server.
2. **Batching beats latency.** WiFi round trips cost ~2–10 ms. CMSIS-DAP already batches many register transfers per
   packet (`DAP_Transfer`, `DAP_TransferBlock`). Support large packet sizes (e.g. 1–4 KB) and multiple packets in
   flight (`DAP_Info` packet count) so the host can pipeline.
3. **Testable without the target.** A **simulated target** (`SIM` build option) replaces the real SWD wires with a
   software model of a DP + MEM-AP + RAM, so the whole chain (host tool → WiFi → ESP32 → "target") can be tested
   with no RA8 board.
4. **One owner per resource.** The SWD engine runs in its own high-priority FreeRTOS task. The UI never blocks it.
   GPIO9 is owned by the bus arbiter.
5. **The screen is a dashboard, not a gimmick.** It shows WiFi/IP, client connected, target status (DPIDR, halted or
   running), packets per second and errors. Later it can show live variables.

## 5. What success looks like (final, with hardware)
- [ ] VS Code `launch.json` (Cortex-Debug) connects to the RA8 through the ESP32 over WiFi.
- [ ] Flash a program, hit a breakpoint in `main()`, step over/into, read locals and registers.
- [ ] Live Watch shows a changing global variable while the CPU runs.
- [ ] (Stretch) RTT terminal over the same link; a simple live plot on the ESP32 screen.

## 6. Non-goals (for now)
- SWO / ETM trace (no spare pins on this board).
- JTAG (SWD only).
- Multiple simultaneous host clients.
- Security beyond a simple on-LAN setup. We will at least avoid an open AP with no password.

## 7. Realistic expectations
- Stepping and breakpoints: should feel normal.
- Live watch: a few to tens of updates/sec per variable is realistic over WiFi.
- Flashing: slower than USB J-Link. Fine for development.
- SWD clock: start at ~1 MHz and optimize later.
