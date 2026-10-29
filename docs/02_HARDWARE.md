# 02 — Hardware

## 1. Boards
| Board | Role | Notes |
|---|---|---|
| Waveshare **ESP32-S3-LCD-4.3** (800×480 RGB, GT911 touch, N8R8 = 8 MB flash, 8 MB octal PSRAM) | The probe | In hand now |
| Renesas **EK-RA8D1** (R7FA8D1BHECBD, Cortex-M85) | The target | **Not available now: PENDING-HW** |

Sources: Waveshare wiki https://waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3 , schematic
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4.3-Sch.pdf , EK-RA8D1 UM (local PDF).

## 2. ESP32 board: what's already used (from the wiki and `docs/reference/LGFX_Config.h`)
- **RGB LCD data/control:** GPIO 0,1,2,3,5,7,10,14,17,18,21,38,39,40,41,42,45,46,47,48. Hands off.
- **I2C bus (port 1):** SDA = GPIO8, SCL = GPIO9. Shared by the GT911 touch, the **CH422G IO expander**, and the external
  I2C header. Touch INT = GPIO4.
  - `LGFX_Config.h` uses GT911 address **0x14**. The wiki mentions 0x5D. The GT911 address depends on the INT level at
    reset. Keep 0x14 (it worked before) and fall back to 0x5D if it doesn't respond.
  - The CH422G drives EXIO1 = touch reset, EXIO2 = backlight (DISP), EXIO3 = LCD reset, EXIO4 = SD CS, EXIO5 = USB_SEL
    (wiki for 1/2/4/5; EXIO3 from community drivers, confirm in schematic). It has no register pointer: each command is its
    own 7-bit I2C address with one data byte: **0x24** WR_SET (bit0 IO_OE = outputs), **0x38** WR_IO (IO0-7 levels).
    Source: Espressif `esp_io_expander_ch422g.c` (Apache-2.0). Implemented in `lib/ch422g`, used once at boot.
- **UART0:** TX = GPIO43, RX = GPIO44, through a CH343P → the USB-C port labelled **UART**. Use this port for
  **flashing and the serial monitor**.
- **USB (native):** GPIO19/20, switched with **CAN** (FSUSB42) via USB_SEL. Not used by us.
- **RS-485:** GPIO15/16 through a transceiver. Not usable as raw GPIO.
- **Sensor AD header:** **GPIO6**: free.

## 3. Pin plan for SWD
| Signal | ESP32 pin | Header on ESP32 board | EK-RA8D1 J13 pin |
|---|---|---|---|
| SWDIO | **GPIO6** | Sensor AD → "AD" pin | **2** (P210/SWDIO) |
| SWCLK | **GPIO9** | I2C → "SCL" pin | **4** (P211/SWCLK) |
| GND | GND | any GND pin | **3** (also 5) |
| nRESET | not wired | n/a | 10, optional. Use software reset (AIRCR.SYSRESETREQ) instead |
| VTref | not wired | n/a | 1. Both boards are 3.3 V logic |

**TODO (verify in schematic before Phase 3 is closed):** check whether the Sensor AD input (GPIO6) has a series
resistor, divider or RC filter that would slow down a bidirectional signal. Record the finding in PROGRESS.md.

## 4. The GPIO9 sharing problem and the solution
GPIO9 is both I2C SCL (touch/CH422G) and SWCLK.

- **Why it's safe electrically:** an I2C transaction only begins with a START (SDA falls while SCL is high). If SDA
  (GPIO8) is left idle-high while SWD toggles GPIO9, I2C devices see clock noise with no START and ignore it. The I2C
  pull-up on GPIO9 is fine for a push-pull SWCLK.
- **Why it's still a software problem:** the I2C peripheral and the SWD code cannot both own GPIO9 at the same moment.
- **Solution: `bus_arbiter` module (mutex + pin-mux switch).**
  - `arbiter_acquire(SWD)` → wait for any I2C transaction to finish, detach GPIO9 from the I2C peripheral
    (GPIO matrix), configure it as GPIO output for SWCLK.
  - `arbiter_acquire(I2C)` → park SWCLK low/idle, re-attach GPIO9 to the I2C peripheral.
  - SWD bursts are short (one CMSIS-DAP packet). The touch read (~20 Hz) slots in between.
  - Touch must be read through our own path (not an LGFX auto-poll that bypasses the arbiter). Phase 1 runs
    **display-only, touch disabled**. Touch comes back in Phase 7 via the arbiter.
  - Also take into account that the CH422G is on the same bus (backlight etc.): init it once at boot before SWD starts.

## 5. Power and ground
- ESP32 board: USB-C (**UART** port) → laptop. Powers the board, and is used for flashing and logs.
- EK-RA8D1: USB to **J10 (DEBUG)** or **J11 (USB FULL SPEED)**. Either powers the board (UM §5.1.1).
- **GND must be wired between the boards.** The 3V3 rails must **not** be connected.

## 6. EK-RA8D1 setup for external debug (PENDING-HW, from UM §5.2.2, Table 9 "Debug In")
| Jumper | Setting | Meaning |
|---|---|---|
| J6 | Open (default) | Target MD connected to debug |
| J8 | 1-2 (default) | Target RESET# connected to debug RESET# |
| **J9** | **Closed (change this!)** | Holds the on-board RA4M2 J-Link OB in reset, so it releases the SWD lines |
| J29 | 1-2, 3-4, 5-6, 7-8 (default) | Target debug signals connected to the debug interface |

J13 (10-pin Cortex debug) pinout (same as J20 pins 1–10): 1 VTref(+3V3), 2 SWDIO, 3 GND, 4 SWCLK, 5 GND, 6 SWO,
7 key, 8 TDI, 9 GNDDetect, 10 nSRST(RESET#).

## 7. Target facts to verify when the board arrives (don't assume them)
- Cortex-M85 debug may use **ADIv6 / DPv3** (DPIDR version field). The probe firmware (CMSIS-DAP) is mostly
  agnostic, because the host handles DP/AP semantics. But the chosen host tool **must support the RA8D1 / Cortex-M85**
  (check the pyOCD target/pack support and the OpenOCD config availability). Record the result in PROGRESS.md.
- The expected DPIDR value is read live. Don't hard-code it.
