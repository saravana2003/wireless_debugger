# 03 — Protocols

Read this before Phases 2–5. Verify every command code and bit layout against the official ARM documents
(CMSIS-DAP: https://arm-software.github.io/CMSIS-DAP/latest/ ; ARM ADIv5.2 / ADIv6 spec).
The tables here are a guide, not the authority.

## 1. SWD (Serial Wire Debug): the physical layer
- 2 wires: **SWCLK** (always driven by the probe) and **SWDIO** (bidirectional; ownership swaps with a
  "turnaround" cycle).
- One transaction = **8-bit request** (Start, APnDP, RnW, A[2:3], Parity, Stop, Park) → turnaround → **3-bit ACK**
  (OK / WAIT / FAULT) → turnaround if needed → **32-bit data + parity**.
- Line reset: ≥50 clocks with SWDIO high. The JTAG-to-SWD switch sequence and dormant-wakeup sequence are sent by the
  host via `DAP_SWJ_Sequence`. The probe just clocks out the bits it's given.
- The probe firmware must handle: WAIT retry (count configurable via `DAP_TransferConfigure`), FAULT reporting,
  parity check, idle cycles after a transfer, and data phase on WAIT/FAULT (configurable).
- Implementation: bit-bang with **direct GPIO register writes** (`GPIO.out_w1ts/out_w1tc`, `GPIO.in`) or the ESP32-S3
  **dedicated GPIO** feature for speed. No `digitalWrite()` in the hot loop. Start with ~1 MHz, measure, then optimize.

## 2. CMSIS-DAP: the probe command protocol
The host sends a **request packet** (byte 0 = command ID) and the probe answers a **response packet** (byte 0 echoes
the command ID). Minimum command set needed for pyOCD/OpenOCD over SWD:

| Command | Purpose |
|---|---|
| `DAP_Info` | Vendor/product/FW version, capabilities (SWD), **packet size**, **packet count** |
| `DAP_HostStatus` | LEDs (map to on-screen indicators) |
| `DAP_Connect` / `DAP_Disconnect` | Enter/leave SWD mode (configure pins via the arbiter) |
| `DAP_TransferConfigure` | Idle cycles, WAIT retry, match retry |
| `DAP_Transfer` | Many DP/AP register reads/writes in one packet (incl. value match, match mask) |
| `DAP_TransferBlock` | Many reads/writes to the same register (fast memory access) |
| `DAP_TransferAbort` | Abort ongoing transfer |
| `DAP_WriteABORT` | Write the DP ABORT register |
| `DAP_Delay` | Delay in µs |
| `DAP_ResetTarget` | Device-specific reset (can return "not implemented") |
| `DAP_SWJ_Pins` | Read/write pins (only SWCLK/SWDIO exist here, nRESET not wired) |
| `DAP_SWJ_Clock` | Set the SWD clock |
| `DAP_SWJ_Sequence` | Clock out arbitrary bit sequences (line reset, JTAG→SWD) |
| `DAP_SWD_Configure` | Turnaround period, data phase |
| `DAP_SWD_Sequence` | Arbitrary SWD sequences (needed for some ADIv6/multi-drop flows) |
| `DAP_ExecuteCommands` / `DAP_QueueCommands` | Command batching. Important for latency |

Recommended approach: port ARM's reference implementation (**`DAP.c` + `SW_DP.c`**, Apache-2.0, from the
ARM-software/CMSIS-DAP GitHub repo). Supply our own `DAP_config.h` that maps the pin macros (`PIN_SWCLK_SET()` etc.)
onto ESP32 register writes, **via the bus arbiter**. Also look at **windowsair/wireless-esp8266-dap**, which already
did this port for ESP chips with a WiFi transport (check its license and ESP32-S3 support first).

## 3. Our WiFi transport (ESP32 side)
- TCP server on a fixed port (make it configurable in `include/app_config.h`). One client at a time. New connections
  are rejected with a log message while one is active.
- `TCP_NODELAY` on. Large socket buffers.
- **Framing must match whatever the chosen host backend expects** (see §5). Keep framing in its own small layer
  (`net_server/framing_*.cpp`) so it can be swapped:
  - `framing_raw`: our own simple format for testing: `[u16 length LE][CMSIS-DAP packet]`.
  - `framing_<host>`: added in Phase 5 to match the chosen host tool exactly.
- Advertise via **mDNS** as `wdbg.local`.

## 4. Simulated target (`SIM`)
A software model behind the same `swd_phy` interface:
- A DP with DPIDR (a clearly fake value), CTRL/STAT with power-up ACK behaviour, SELECT, RDBUFF.
- One MEM-AP (AHB-AP-like) with CSW/TAR/DRW/IDR, auto-increment, and a RAM array (e.g. 64 KB at 0x20000000).
- Optional minimal SCS registers (DHCSR, DCRSR/DCRDR, DEMCR, CPUID) so a host tool can "halt" and read registers.
  Extend only as far as needed for host tools to connect.
- Enable it with the build flag `-DWDBG_TARGET_SIM=1` (the SIM env in platformio.ini) **and/or** a runtime switch on the
  screen. Keep it simple: build flag first.

## 5. Host side: how VS Code reaches the probe (DECISION NEEDED, do research in Phase 5)
Options, in rough order of preference. **Research the current state, then present a recommendation to Saro before
implementing.**

1. **OpenOCD with a CMSIS-DAP network backend.** Check whether the current OpenOCD release has a TCP backend for
   `cmsis-dap`. If yes, implement that framing on the ESP32. Cortex-Debug supports `servertype: openocd` natively.
   Also check whether OpenOCD has RA8 / Cortex-M85 (ADIv6) support.
2. **pyOCD with a custom probe plugin** (`tools/pyocd_wdbg/`): a small Python package that implements pyOCD's probe
   interface by sending CMSIS-DAP packets over TCP. Cortex-Debug supports `servertype: pyocd`. RA8D1 support via the
   CMSIS pack (check `pyocd pack find R7FA8D1`).
3. **elaphureLink-compatible framing**, then use the elaphureLink proxy on Windows (Keil/pyOCD compatible). Check its
   protocol and license.
4. **Virtual USB (USB/IP)**: the ESP32 pretends to be a USB CMSIS-DAP device over the network. Most compatible, but
   complex on Windows. Last resort.

Whatever is chosen: deliver a working `.vscode/launch.json` example (Cortex-Debug) and `docs/HOST_SETUP.md`
explaining the install steps on Windows.

## 6. Test tooling (host, Python)
`tools/dap_client.py`: a CLI that speaks our raw framing:
- `ping` / `info` (DAP_Info), `latency` (N round trips, min/avg/max), `throughput` (TransferBlock reads),
- `read32 <addr>` / `write32 <addr> <val>` / `dump <addr> <len>` (via DP/AP transfers; works against SIM).
