# EK-RA8D1 bring-up checklist (Phase 8)

The board already runs the right firmware: **`esp32-s3-waveshare-43` env, FW 0.7.0, green `REAL TARGET` badge**.
No reflash is needed before the first test. Everything here is **untested on real hardware**: this is the first time
real SWD signals run. If a step fails, stop and record what you saw, rather than moving on.

## 1. Set up the RA8 board (UM §5.2.2 "Debug In", see 02_HARDWARE.md §6)
| Jumper | Setting | Why |
|---|---|---|
| **J9** | **Closed (change this!)** | Holds the on-board J-Link in reset, so it lets go of the SWD lines |
| J6 | Open (default) | |
| J8 | 1-2 (default) | |
| J29 | 1-2, 3-4, 5-6, 7-8 (default) | |

Power the RA8 from USB on J10 or J11.

## 2. Wire it: power both boards OFF first
| Signal | ESP32 board | EK-RA8D1 J13 |
|---|---|---|
| SWDIO | J6 "Sensor AD" **pin 3** (AD, GPIO6) | **pin 2** |
| SWCLK | I2C header **SCL** (GPIO9) | **pin 4** |
| GND | J6 "Sensor AD" **pin 2** | **pin 3** |

- **Do NOT connect any 3V3.** J6 pin 1 is 3V3: leave it empty. Same for J13 pin 1.
- Keep the wires short and twisted with GND if you can. SWCLK shares a pin with the I2C bus (4.7 kOhm pull-ups, plus
  the touch chip and CH422G load), so its edges are the first thing to suspect at higher clocks.
- Power up. The screen must look as it did today. **Touch off** (don't tap) for the first tests.

## 3. First contact, smallest step first
```sh
python tools/dap_client.py --ip 192.168.1.61 connect
```
| You see | Meaning | Next |
|---|---|---|
| A DPIDR value, `debug power-up: OK` | **SWD works.** Record DPIDR in PROGRESS.md; its version field answers the ADIv5/ADIv6 question | Go to step 4 |
| `ACK 0x7` | Nothing answers | Check J9, the wiring, RA8 power, GND. Try `--clock 100000` |
| `ACK 0x2` / `0x4` | Target answers but says WAIT / FAULT | SWD is alive. Note it, then try `pyocd` (step 4), which does a fuller connect |
| Value changes between runs, parity errors | Signal quality | `--clock 100000`, shorter wires, check GND |

`dap_client.py` assumes an ADIv5 DP. If the DPIDR version field says DPv3 (ADIv6), its AP steps may fail even though SWD
is fine: go on to pyOCD, which handles both.

## 4. pyOCD: halt, read registers
```sh
.host/venv/Scripts/pyocd.exe commander --probe wdbg:192.168.1.61 --target r7fa8d1bh -f 1000000 \
    -c status -c halt -c "reg pc" -c step -c "reg pc" -c go
```
The `load-svd` traceback is harmless (HOST_SETUP.md §2). The first run shows whether the RA8D1 needs anything special at
connect (TrustZone, DLM state, ID code): an open question nobody has been able to test yet.

## 5. Flash and debug
```sh
.host/venv/Scripts/pyocd.exe load --probe wdbg:192.168.1.61 --target r7fa8d1bh -f 1000000 your_project.elf
```
Then use the Cortex-Debug `launch.json` in HOST_SETUP.md §3. **Neither has ever been run.** Build a blinky first, so a
failure can't be blamed on the application. Goals, from the roadmap: breakpoint in `main()`, step, watch variables,
Live Watch on a global counter.

## 6. Then test what the simulator couldn't
1. **Touch while debugging.** Tap buttons during a live session, on a running target, and watch `SWD errors`. This is the
   first real test of holding SWDIO low during I2C (02_HARDWARE.md §4). If the session dies on a tap, that design is wrong.
2. **Faster clocks.** Try `-f 2000000`, then `4000000`. The screen's `SWCLK limit` button caps whatever the host asks for,
   which helps rule the clock in or out. Update `WDBG_SWD_DEFAULT_HZ` only after a clock has proved reliable.
3. Bring the logic analyzer if you can: real SWCLK has never been measured, only derived from CPU cycles.

## Practical notes
- **Flash the ESP32:** only if the firmware changes. Hold BOOT, tap RST, release BOOT, then `pio run -t upload --upload-port COM16`, then press RST.
- **One client at a time.** `connection closed by peer` = an old pyOCD or OpenOCD is still running: close it.
- **Closing `tools/serial_log.py` can drop the board into download mode** (it looks dead: no ping, blank log). Press RST.
- **The serial log misses roughly the first 3 s of boot**, so boot-time numbers are on the screen instead.
- If `pio upload` fails with `Write timeout`: unplug the USB-C, wait 3 s, replug, redo BOOT+RST.
- The boot self-test clocks SWCLK at full speed (10 MHz) for a moment with no target selected. It should be harmless,
  because a host always starts with a line reset. Worth remembering if something odd happens only right after an ESP32 reset.
