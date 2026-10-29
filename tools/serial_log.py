#!/usr/bin/env python3
"""serial_log.py: capture the probe's boot log from the UART port without resetting it.

Usage: python tools/serial_log.py COM16 40     (then press RST on the board within 40 s)

Opens the CH343 port with DTR/RTS released (so the board is not forced into download mode) and
survives the port vanishing for a moment when RST is pressed. Prints everything at the end.
"""
import sys
import time

import serial  # pyserial (bundled with PlatformIO's penv python)

end = time.time() + float(sys.argv[2] if len(sys.argv) > 2 else 40)
buf = b""
p = None
while time.time() < end:
    try:
        if p is None:
            p = serial.Serial()
            p.port = sys.argv[1]
            p.baudrate = 115200
            p.timeout = 0.2
            p.dtr = False
            p.rts = False
            p.open()
        buf += p.read(4096)
    except (serial.SerialException, OSError):
        try:
            if p:
                p.close()
        except Exception:
            pass
        p = None
        time.sleep(0.2)
sys.stdout.write(buf.decode("utf-8", "replace"))
