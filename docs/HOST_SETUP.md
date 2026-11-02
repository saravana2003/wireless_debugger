# Host setup (Windows)

The laptop runs a normal GDB server. It reaches the probe over WiFi instead of USB:

```
VS Code (Cortex-Debug) -> arm-none-eabi-gdb -> pyOCD + our plugin -> WiFi, TCP 4441 -> ESP32 -> SWD -> RA8D1
```

Use **pyOCD** for daily work, because it is the only one of the two that can flash the RA8D1. **OpenOCD** can debug
but not flash, and is useful as an independent check of the probe.

Everything below assumes the repo root as the current folder, in Git Bash. Replace `192.168.1.61` with the IP on the
probe's screen (`wdbg.local` does not resolve on Saro's LAN).

## 1. Install pyOCD and the plugin
Python 3.11 is what this was tested with (PlatformIO's own Python works). `.host/` is git-ignored.
```sh
~/.platformio/penv/Scripts/python.exe -m venv .host/venv
.host/venv/Scripts/python.exe -m pip install -e tools/pyocd_wdbg   # pulls in pyocd==0.46.0
.host/venv/Scripts/pyocd.exe pack install R7FA8D1BH                # Renesas.RA_DFP: flash algorithms + SVD
```
The plugin uses pyOCD internals, so keep the pinned version unless you re-test.

## 2. Check the link
```sh
python tools/dap_client.py --ip 192.168.1.61 info
.host/venv/Scripts/pyocd.exe commander --probe wdbg:192.168.1.61 --target r7fa8d1bh -c status -c halt -c "reg pc"
```
Network probes can't be enumerated, so `pyocd list --probes` won't show it. Always pass `--probe wdbg:<ip>[:<port>]`.

`Exception in thread load-svd ... AttributeError` from pyOCD with the `r7fa8d1bh` target is harmless: pyOCD 0.46.0's
SVD parser can't read the Renesas SVD. The session continues; only peripheral names in `commander` are missing.
Cortex-Debug parses the SVD itself.

## 3. VS Code (Cortex-Debug)
Put this in the **RA8 project's** `.vscode/launch.json`, and fix the `executable` path:
```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "RA8D1 over WiFi (pyOCD)",
      "type": "cortex-debug",
      "request": "launch",            // flashes, then stops at main; use "attach" to leave the target as it is
      "servertype": "pyocd",
      "serverpath": "D:/WIRELESS_DEBUGGER/Wireless_debugger/.host/venv/Scripts/pyocd.exe",
      "serverArgs": ["--probe", "wdbg:192.168.1.61"],
      "targetId": "r7fa8d1bh",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/Debug/your_project.elf",
      "runToEntryPoint": "main",
      "svdFile": "D:/WIRELESS_DEBUGGER/Wireless_debugger/.host/svd/R7FA8D1BH.svd"
    }
  ]
}
```
The SVD (peripheral register view) lives inside the pack archive, so extract it once:
```sh
mkdir -p .host/svd
unzip -o -j "$LOCALAPPDATA"/cmsis-pack-manager/cmsis-pack-manager/Renesas/RA_DFP/6.5.1.pack SVD/R7FA8D1BH.svd -d .host/svd
```

## 4. OpenOCD (optional, debug only)
Use a build with the TCP backend: the last numbered release (0.12.0) doesn't have it. Two builds were tested:
- **[openocd-org "latest"](https://github.com/openocd-org/openocd/releases/tag/latest)** (recommended, 32-bit): connects
  in under a second, with no socket errors.
- **xPack v0.12.0-7**: works, but on Windows spends ~10 s at connect printing `peek_socket ... 10035` and
  `error reading header` (harmless: 64 startup flush reads, each timing out). Upstream fixed this later.

Unzip into `.host/openocd-latest/`, then:
```sh
.host/openocd-latest/bin/openocd.exe -c "set WDBG_IP 192.168.1.61" -f tools/openocd/wdbg.cfg
arm-none-eabi-gdb your_project.elf -ex "target extended-remote :3333"
```
The probe takes **one client at a time**. If a new session gets `connection closed by peer`, an old pyOCD or OpenOCD is
still running: close it.

## 5. Test without the board
`tools/sim_server` runs the probe's CMSIS-DAP engine and the simulated Cortex-M85 on the laptop (needs MinGW `g++`):
```sh
sh tools/sim_server/build.sh && .pio/sim_server.exe
.host/venv/Scripts/pyocd.exe commander --probe wdbg:127.0.0.1 --target cortex_m -c halt -c step -c "reg pc"
```
The `esp32_sim` firmware env gives you the same target on the real board, over WiFi.

The simulated target has no real execution and no flash, so it can't test flashing, and GDB shows
`Invalid state, unable to determine sp alias` warnings against it.

After `monitor reset halt`, GDB may show the old PC: that's GDB's register cache. Run `maintenance flush register-cache`.

## Status of this guide
| Step | State |
|---|---|
| pyOCD, plugin, OpenOCD, GDB against the simulated target | Tested |
| Cortex-Debug `launch.json` above | **Not run yet**: needs a real ELF and target |
| Flashing the RA8D1, real SWD signals, connecting to real silicon | **PENDING-HW** (Phase 8) |
