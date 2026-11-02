#!/usr/bin/env python3
"""swd_hammer.py: keep the SWD pins busy, to test touch (I2C) sharing GPIO9 with SWCLK.

Needs no target: with nothing wired each DPIDR read gets "no ACK", but SWCLK still toggles and the
bus arbiter still switches. Tap the probe's screen while it runs; the probe must keep answering.

Usage: python tools/swd_hammer.py --ip 192.168.1.61 [--seconds 120]
"""
import argparse
import statistics
import sys
import time

from dap_client import DP_R_DPIDR, ID_CONNECT, ID_DISCONNECT, ID_TRANSFER, ID_SWJ_SEQUENCE, PORT_SWD, Probe

BATCH = 32  # DPIDR reads per packet


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", required=True)
    ap.add_argument("--port", type=int, default=4441)
    ap.add_argument("--seconds", type=float, default=120)
    args = ap.parse_args()

    p = Probe(args.ip, args.port)
    if p.cmd(bytes([ID_CONNECT, PORT_SWD]))[1] != PORT_SWD:
        print("DAP_Connect(SWD) refused")
        return 1
    p.cmd(bytes([ID_SWJ_SEQUENCE, 51]) + b"\xFF" * 7)  # line reset
    req = bytes([ID_TRANSFER, 0, BATCH]) + bytes([DP_R_DPIDR]) * BATCH
    samples, acks, bad = [], {}, 0
    t_end = time.perf_counter() + args.seconds
    while time.perf_counter() < t_end:
        t0 = time.perf_counter()
        r = p.cmd(req)
        samples.append((time.perf_counter() - t0) * 1000)
        acks[r[2]] = acks.get(r[2], 0) + 1
        # 0xFF in byte 1 = the probe answered "bus busy" (handle_frame's error response).
        if r[1] == 0xFF:
            bad += 1
    p.cmd(bytes([ID_DISCONNECT]))
    p.close()
    samples.sort()
    print(f"{len(samples)} packets in {args.seconds:.0f} s, ACK values {acks}, bus-busy errors {bad}")
    print(f"round trip: median {statistics.median(samples):.1f}  p95 {samples[int(len(samples) * 0.95) - 1]:.1f}"
          f"  max {samples[-1]:.1f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
