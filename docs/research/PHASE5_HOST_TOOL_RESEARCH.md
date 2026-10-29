# Phase 5: Host tool research (2026-10-30)

Question (03_PROTOCOLS.md §5): how does VS Code / GDB reach our WiFi CMSIS-DAP probe, and which tool can
debug **and flash** the EK-RA8D1 (R7FA8D1BHECBD, Cortex-M85)?

**Status: research done, recommendation presented to Saro, DECISION PENDING.** Nothing implemented yet.

## 1. OpenOCD has a CMSIS-DAP-over-TCP backend (upstream)
- File: `src/jtag/drivers/cmsis_dap_tcp.c` in github.com/openocd-org/openocd (GPL-2.0, author Brian Kuschak).
  Added 2025-09-01 (commit `fcff4b7`, "jtag/drivers/cmsis_dap: add new backend cmsis_dap_tcp"). Later fixes:
  2026-06-21 (error reporting), 2026-08-15 (`e89e738`, more robust socket handling).
- **Not in the last numbered release** (v0.12.0, 2023-01). Needs a dev build:
  - xPack OpenOCD **v0.12.0-7** (2025-10-05), built from "latest upstream master" -> very probably includes it
    (built after 2025-09-01) but not the 2026 fixes. **Verify after install** (`cmsis-dap backend tcp` accepted).
  - openocd-org "latest" GitHub release (2026-10-29) only ships `openocd-<sha>-i686-w64-mingw32.tar.gz` (32-bit).
- Config (from the source + osrtos.com example):
  ```
  adapter driver cmsis-dap
  cmsis-dap backend tcp
  cmsis-dap tcp host <probe-ip>
  cmsis-dap tcp port 4441          # default
  cmsis-dap tcp min_timeout 150    # ms, default 150 (WiFi-friendly flush timeout)
  transport select swd
  ```
- **Wire format** (read from the source, we implement it ourselves, no GPL code copied):
  | Offset | Field | Value |
  |---|---|---|
  | 0 | u32 LE signature | `0x00504144` ("DAP\0") |
  | 4 | u16 LE length | payload length, header excluded |
  | 6 | u8 packet_type | 1 = request (host->probe), 2 = response (probe->host) |
  | 7 | u8 reserved | 0 |
  | 8 | payload | one CMSIS-DAP packet, max 1024 bytes (`CMSIS_DAP_PACKET_SIZE`) |
  OpenOCD rejects responses with a wrong signature / type / oversize length.
- Existing ESP32 firmware using it: github.com/bkuschak/cmsis_dap_tcp_esp32 (tested on ESP32-S3; SWO unsupported,
  ~6 MHz max clock). Fork nyamuchan/openocd-wireless-dap adds an SRAM read cache for high-RTT links.
- **Target side:** Cortex-M85 is known to OpenOCD (`cortex_m.h`: `CORTEX_M85_PARTNO` = Arm, 0xD23) -> generic
  `cortex_m` target can halt/step/breakpoint. **No Renesas RA target config** in `tcl/` and **no flash driver for
  RA internal flash** in `src/flash/nor` (only `renesas_rpchf.c` = R-Car HyperFlash). => OpenOCD can debug but
  **cannot program the RA8D1 flash**.

## 2. pyOCD (0.46.0 checked in a scratch venv)
- **RA8D1 is supported via Renesas' official CMSIS pack** `Renesas.RA_DFP` 6.5.1 (`pyocd pack find R7FA8D1`
  -> R7FA8D1AF/AH/BF/BH; our part = R7FA8D1BH). The pack is already in Saro's cache:
  `%LOCALAPPDATA%\cmsis-pack-manager\cmsis-pack-manager\Renesas.RA_DFP.6.5.1.pdsc`.
- Parsed PDSC for R7FA8D1BH (family RA8D1 Series / subFamily RA8D1_2M):
  - processor: Cortex-M85 r0p0, TZ, DP FPU, MPU, DSP, MVE (FP), PACBTI, 480 MHz; SVD `SVD/R7FA8D1BH.svd`
  - memory: Flash 0x02000000 size 0x1F8000 | DataFlash 0x27000000 0x3000 | DataFlash_Config 0x27030080 0x2E0 |
    ConfigROM 0x0300A100 0x100 | ConfigROM_S 0x0300A200 0x100 | SRAM 0x22000000 0xE0000 | ITCM 0x0 0x10000 |
    DTCM 0x20000000 0x10000
  - **flash algorithms**: `Flash/RA8D1_2M.FLM` (code flash), `RA8D1_DATA_C2M.FLM` (data flash),
    `RA8D1_CCONF.FLM`, `RA8D1_DCONF.FLM`; all RAMstart 0x22000000, RAMsize 0x7800
  => **pyOCD can flash the RA8D1.** Zephyr's EK-RA8D1 board docs also list pyOCD as a supported runner.
- Cortex-M85 debug components known (`coresight/component_ids.py`: 0xD23 DWT/BPU/ITM/PMU/EWIC); ADIv6 code present.
- **No CMSIS-DAP-over-TCP transport in pyOCD.** Its `remote` probe (`tcp_client_probe.py`) speaks pyOCD's own
  high-level probe-server protocol, not CMSIS-DAP.
- **Plugin route is feasible:** entry point group `[pyocd.probe]` (built-ins: cmsisdap, jlink, picoprobe, remote,
  stlink). `pyocd/probe/pydapaccess/interface/interface.py` `Interface` has: open, close, write, read, get_info,
  get/set_packet_count, get/set_packet_size, get_serial_number, has_swo_ep, is_bulk.
  `DAPAccessCMSISDAP(unique_id, interface=None)` accepts an interface object directly.
  => Plugin = TCP `Interface` (OpenOCD framing above) + ProbeFactoryPlugin wrapping CMSISDAPProbe.
  Risk: relies on pyOCD **internal** API -> pin the pyOCD version.

## 3. Others (only skimmed)
- elaphureLink: Windows proxy aimed at Keil MDK; adds a third-party Windows app; dapjs fork is MIT. Not pursued.
- USB/IP: most compatible in theory but needs usbip-win drivers on Windows. Last resort.

## 4. Recommendation given to Saro (awaiting his answer)
1. Firmware: implement **OpenOCD's cmsis-dap-tcp framing on port 4441** (replace our raw `[u16 len]` framing;
   update `tools/dap_client.py` to match).
2. **Primary host tool: pyOCD + our plugin `tools/pyocd_wdbg/`** (only option that flashes the RA8D1).
   Cortex-Debug `servertype: pyocd`.
3. **Secondary: OpenOCD (xPack)** with zero host code: debug-only sessions + an independent check that our
   firmware speaks the protocol correctly.
4. Extend `swd_sim` only as far as the tools need: minimal ROM table + SCS (CPUID, DHCSR halt/run, DCRSR/DCRDR).
   Take every register/ID value from the ARMv8-M ARM / Cortex-M85 TRM, not from memory.

## Sources
- openocd-org/openocd: `src/jtag/drivers/cmsis_dap_tcp.c`, `src/target/cortex_m.c/.h`, `tcl/`, `src/flash/nor`
- https://openocd.org/doc/doxygen/html/cmsis__dap_8c_source.html
- https://github.com/bkuschak/cmsis_dap_tcp_esp32 ; https://osrtos.com/projects/cmsis-dap-over-tcp-for-esp32/
- https://github.com/nyamuchan/openocd-wireless-dap
- https://github.com/xpack-dev-tools/openocd-xpack/releases (v0.12.0-7)
- https://docs.zephyrproject.org/latest/boards/renesas/ek_ra8d1/doc/index.html
- https://kb.segger.com/Renesas_RA8 (dual-core RA8x2 primary-CPU bit note; not our part, but RA8 debug caveat)
- pyOCD 0.46.0 package source (installed locally), Renesas.RA_DFP 6.5.1 PDSC
