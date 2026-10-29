#!/usr/bin/env python3
"""dap_client.py: test client for the Wireless Debugger's raw TCP framing.

Framing: [u16 length, little-endian][payload]. In Phase 2 the probe echoes every frame.

Examples:
  python tools/dap_client.py --ip 192.168.1.61 ping
  python tools/dap_client.py --ip 192.168.1.61 latency -n 1000
  python tools/dap_client.py --ip 192.168.1.61 throughput --size 1024 --seconds 5 --window 4
"""
import argparse
import os
import socket
import statistics
import struct
import sys
import time

DEFAULT_PORT = 4242   # WDBG_TCP_PORT in include/app_config.h
FRAME_MAX = 4096      # WDBG_FRAME_MAX in include/app_config.h


class Probe:
    def __init__(self, ip, port, timeout=10.0):
        self.sock = socket.create_connection((ip, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self):
        self.sock.close()

    def send_frame(self, payload):
        if not 0 < len(payload) <= FRAME_MAX:
            raise ValueError(f"payload must be 1..{FRAME_MAX} bytes")
        self.sock.sendall(struct.pack("<H", len(payload)) + payload)

    def _recv_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("probe closed the connection")
            buf += chunk
        return bytes(buf)

    def recv_frame(self):
        (n,) = struct.unpack("<H", self._recv_exact(2))
        return self._recv_exact(n)

    def transact(self, payload):
        self.send_frame(payload)
        return self.recv_frame()


def cmd_ping(p, args):
    payload = b"\x80ping"
    t0 = time.perf_counter()
    resp = p.transact(payload)
    dt = (time.perf_counter() - t0) * 1000
    ok = resp == payload
    print(f"ping: {'OK' if ok else 'MISMATCH'}  {dt:.2f} ms")
    return 0 if ok else 1


def cmd_latency(p, args):
    payload = os.urandom(args.size)
    samples = []
    for _ in range(args.n):
        t0 = time.perf_counter()
        resp = p.transact(payload)
        samples.append((time.perf_counter() - t0) * 1000)
        if resp != payload:
            print("echo mismatch!")
            return 1
    samples.sort()
    p95 = samples[int(len(samples) * 0.95) - 1]
    print(f"latency, {args.n} round trips of {args.size} B:")
    print(f"  min {samples[0]:.2f}  avg {statistics.mean(samples):.2f}  "
          f"median {statistics.median(samples):.2f}  p95 {p95:.2f}  max {samples[-1]:.2f}  (ms)")
    return 0


def cmd_throughput(p, args):
    payload = os.urandom(args.size)
    in_flight = 0
    done = 0
    t_end = time.perf_counter() + args.seconds
    t0 = time.perf_counter()
    while True:
        now = time.perf_counter()
        while in_flight < args.window and now < t_end:
            p.send_frame(payload)
            in_flight += 1
        if in_flight == 0:
            break
        if p.recv_frame() != payload:
            print("echo mismatch!")
            return 1
        in_flight -= 1
        done += 1
    dt = time.perf_counter() - t0
    one_way = done * (args.size + 2)
    print(f"throughput, {args.size} B frames, window {args.window}, {dt:.1f} s:")
    print(f"  {done / dt:.0f} frames/s   {one_way / dt / 1024:.1f} KiB/s each way "
          f"({2 * one_way / dt / 1024:.1f} KiB/s total)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", required=True, help="probe IP (shown on the probe's screen)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping", help="one echo round trip")

    lat = sub.add_parser("latency", help="N sequential round trips -> min/avg/p95/max")
    lat.add_argument("-n", type=int, default=1000)
    lat.add_argument("--size", type=int, default=64, help="payload bytes (default 64, a typical DAP packet)")

    thr = sub.add_parser("throughput", help="echo bandwidth with N frames in flight")
    thr.add_argument("--size", type=int, default=1024)
    thr.add_argument("--seconds", type=float, default=5.0)
    thr.add_argument("--window", type=int, default=1, help="frames in flight (pipelining)")

    args = ap.parse_args()
    try:
        p = Probe(args.ip, args.port)
    except OSError as e:
        print(f"cannot connect to {args.ip}:{args.port}: {e}")
        return 2
    try:
        return {"ping": cmd_ping, "latency": cmd_latency, "throughput": cmd_throughput}[args.cmd](p, args)
    finally:
        p.close()


if __name__ == "__main__":
    sys.exit(main())
