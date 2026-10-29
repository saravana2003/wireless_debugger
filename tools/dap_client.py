#!/usr/bin/env python3
"""dap_client.py: CMSIS-DAP test client for the Wireless Debugger (raw TCP framing).

Framing: [u16 length, little-endian][CMSIS-DAP packet]. Command IDs and layouts follow ARM's
CMSIS-DAP (DAP.h); DP/AP register usage follows ADIv5 (MEM-AP CSW/TAR/DRW).

Examples:
  python tools/dap_client.py --ip 192.168.1.61 info
  python tools/dap_client.py --ip 192.168.1.61 connect
  python tools/dap_client.py --ip 192.168.1.61 write32 0x20000000 0xDEADBEEF
  python tools/dap_client.py --ip 192.168.1.61 read32 0x20000000 4
  python tools/dap_client.py --ip 192.168.1.61 dump 0x20000000 256
  python tools/dap_client.py --ip 192.168.1.61 ping | latency -n 1000 | throughput --window 4
"""
import argparse
import socket
import statistics
import struct
import sys
import time

DEFAULT_PORT = 4242   # WDBG_TCP_PORT in include/app_config.h

# CMSIS-DAP command IDs (DAP.h)
ID_INFO, ID_CONNECT, ID_DISCONNECT = 0x00, 0x02, 0x03
ID_TRANSFER_CONFIGURE, ID_TRANSFER, ID_TRANSFER_BLOCK, ID_WRITE_ABORT = 0x04, 0x05, 0x06, 0x08
ID_SWJ_CLOCK, ID_SWJ_SEQUENCE = 0x11, 0x12
DAP_OK, PORT_SWD = 0x00, 0x01

INFO_IDS = [(0x01, "vendor", "str"), (0x02, "product", "str"), (0x03, "serial", "str"),
            (0x04, "CMSIS-DAP version", "str"), (0x09, "firmware version", "str"),
            (0xF0, "capabilities", "caps"), (0xFE, "packet count", "u8"), (0xFF, "packet size", "u16")]

# DAP_Transfer request bits: APnDP=0x01, RnW=0x02, A[3:2]=0x0C
DP_R_DPIDR, DP_W_ABORT, DP_W_CTRL, DP_R_CTRL, DP_W_SELECT, DP_R_RDBUFF = 0x02, 0x00, 0x04, 0x06, 0x08, 0x0E
AP_W_CSW, AP_W_TAR, AP_W_DRW, AP_R_DRW, AP_R_REG_C = 0x01, 0x05, 0x0D, 0x0F, 0x0F

ACK_TEXT = {1: "OK", 2: "WAIT", 4: "FAULT", 7: "no response (nothing connected?)"}
CSYSPWRUPREQ, CDBGPWRUPREQ = 1 << 30, 1 << 28
CSYSPWRUPACK, CDBGPWRUPACK = 1 << 31, 1 << 29
CSW_WORD_INC = 0x00000012   # Size=word, AddrInc=single (ADIv5 MEM-AP CSW)


class DapError(Exception):
    pass


class Probe:
    def __init__(self, ip, port, timeout=10.0):
        self.sock = socket.create_connection((ip, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.packet_size = 64

    def close(self):
        self.sock.close()

    # ---- framing ----
    def send_frame(self, payload):
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

    def cmd(self, payload):
        self.send_frame(payload)
        resp = self.recv_frame()
        if not resp or resp[0] != payload[0]:
            raise DapError(f"bad response to command 0x{payload[0]:02X}: {resp.hex()}")
        return resp

    # ---- CMSIS-DAP commands ----
    def info(self, info_id):
        r = self.cmd(bytes([ID_INFO, info_id]))
        return r[2:2 + r[1]]

    def transfer(self, ops):
        """ops: list of (request, value). Returns read values. Raises on a non-OK ACK."""
        req = bytearray([ID_TRANSFER, 0, len(ops)])
        for r, v in ops:
            req.append(r)
            if not r & 0x02:
                req += struct.pack("<I", v)
        resp = self.cmd(bytes(req))
        done, ack = resp[1], resp[2]
        if done != len(ops) or ack != 1:
            raise DapError(f"DAP_Transfer: {done}/{len(ops)} done, ACK 0x{ack:X} ({ACK_TEXT.get(ack & 7, 'error')})")
        n = (len(resp) - 3) // 4
        return list(struct.unpack(f"<{n}I", resp[3:3 + 4 * n]))

    def block(self, request, count, data=None):
        req = bytearray([ID_TRANSFER_BLOCK, 0]) + struct.pack("<H", count) + bytes([request])
        if data is not None:
            req += struct.pack(f"<{count}I", *data)
        resp = self.cmd(bytes(req))
        (done,) = struct.unpack("<H", resp[1:3])
        ack = resp[3]
        if done != count or ack != 1:
            raise DapError(f"DAP_TransferBlock: {done}/{count} done, ACK 0x{ack:X} ({ACK_TEXT.get(ack & 7, 'error')})")
        if data is None:
            return list(struct.unpack(f"<{count}I", resp[4:4 + 4 * count]))
        return []

    def connect(self, clock_hz=1_000_000):
        """SWD connect + debug power-up. Returns (DPIDR, AP0 IDR)."""
        self.packet_size = struct.unpack("<H", self.info(0xFF))[0]
        if self.cmd(bytes([ID_CONNECT, PORT_SWD]))[1] != PORT_SWD:
            raise DapError("DAP_Connect(SWD) refused")
        self.cmd(bytes([ID_SWJ_CLOCK]) + struct.pack("<I", clock_hz))
        self.cmd(bytes([ID_TRANSFER_CONFIGURE, 0]) + struct.pack("<HH", 100, 0))  # idle 0, 100 WAIT retries
        ones = bytes([ID_SWJ_SEQUENCE, 51]) + b"\xFF" * 7
        self.cmd(ones)                                          # line reset
        self.cmd(bytes([ID_SWJ_SEQUENCE, 16, 0x9E, 0xE7]))      # JTAG-to-SWD select
        self.cmd(ones)                                          # line reset
        self.cmd(bytes([ID_SWJ_SEQUENCE, 8, 0x00]))             # idle
        dpidr = self.transfer([(DP_R_DPIDR, 0)])[0]
        self.transfer([(DP_W_ABORT, 0x1E), (DP_W_SELECT, 0), (DP_W_CTRL, CSYSPWRUPREQ | CDBGPWRUPREQ)])
        for _ in range(100):
            ctrl = self.transfer([(DP_R_CTRL, 0)])[0]
            if ctrl & (CSYSPWRUPACK | CDBGPWRUPACK) == (CSYSPWRUPACK | CDBGPWRUPACK):
                break
        else:
            raise DapError(f"debug power-up not acknowledged (CTRL/STAT 0x{ctrl:08X})")
        # AP0 IDR is at 0xFC: SELECT.APBANKSEL = 0xF, A[3:2] = 3
        idr = self.transfer([(DP_W_SELECT, 0xF0), (AP_R_REG_C, 0), (DP_W_SELECT, 0)])[0]
        self.transfer([(AP_W_CSW, CSW_WORD_INC)])
        return dpidr, idr

    def read_words(self, addr, count):
        out = []
        max_words = (self.packet_size - 4) // 4
        while count:
            # MEM-AP auto-increment is only guaranteed within a 1 KB block (ADIv5).
            n = min(count, max_words, (0x400 - (addr & 0x3FF)) // 4)
            self.transfer([(AP_W_TAR, addr)])
            out += self.block(AP_R_DRW, n)
            addr += 4 * n
            count -= n
        return out

    def write_words(self, addr, words):
        max_words = (self.packet_size - 5) // 4
        i = 0
        while i < len(words):
            n = min(len(words) - i, max_words, (0x400 - (addr & 0x3FF)) // 4)
            self.transfer([(AP_W_TAR, addr)])
            self.block(AP_W_DRW, n, words[i:i + n])
            addr += 4 * n
            i += n


# ---- commands ----
def cmd_info(p, args):
    for info_id, name, kind in INFO_IDS:
        v = p.info(info_id)
        if kind == "str":
            text = v.rstrip(b"\0").decode(errors="replace") or "-"
        elif kind == "caps":
            bits = v[0] if v else 0
            text = ", ".join(n for b, n in [(0, "SWD"), (1, "JTAG"), (2, "SWO-UART"), (4, "atomic"),
                                             (5, "timestamp"), (7, "UART")] if bits >> b & 1) or "-"
        elif kind == "u8":
            text = str(v[0]) if v else "-"
        else:
            text = str(struct.unpack("<H", v)[0]) if len(v) == 2 else "-"
        print(f"  {name:18s} {text}")
    return 0


def cmd_connect(p, args):
    dpidr, idr = p.connect(args.clock)
    print(f"DPIDR   0x{dpidr:08X}  (version {dpidr >> 12 & 0xF}, designer 0x{dpidr >> 1 & 0x7FF:03X})")
    print(f"AP0 IDR 0x{idr:08X}  (class 0x{idr >> 13 & 0xF:X}{' = MEM-AP' if idr >> 13 & 0xF == 8 else ''})")
    print("debug power-up: OK")
    return 0


def cmd_read32(p, args):
    p.connect(args.clock)
    for i, w in enumerate(p.read_words(args.addr, args.count)):
        print(f"0x{args.addr + 4 * i:08X}: 0x{w:08X}")
    return 0


def cmd_write32(p, args):
    p.connect(args.clock)
    p.write_words(args.addr, [args.value])
    back = p.read_words(args.addr, 1)[0]
    print(f"0x{args.addr:08X} <- 0x{args.value:08X}, read back 0x{back:08X} {'OK' if back == args.value else 'MISMATCH'}")
    return 0 if back == args.value else 1


def cmd_dump(p, args):
    p.connect(args.clock)
    words = p.read_words(args.addr & ~3, (args.length + 3) // 4)
    data = struct.pack(f"<{len(words)}I", *words)
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        text = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        print(f"0x{(args.addr & ~3) + off:08X}: {chunk.hex(' '):48s} {text}")
    return 0


def cmd_ping(p, args):
    t0 = time.perf_counter()
    size = struct.unpack("<H", p.info(0xFF))[0]
    print(f"ping: OK  {(time.perf_counter() - t0) * 1000:.2f} ms  (DAP_Info packet size = {size})")
    return 0


def cmd_latency(p, args):
    samples = []
    for _ in range(args.n):
        t0 = time.perf_counter()
        p.info(0xFF)
        samples.append((time.perf_counter() - t0) * 1000)
    samples.sort()
    p95 = samples[int(len(samples) * 0.95) - 1]
    print(f"latency, {args.n} DAP_Info round trips:")
    print(f"  min {samples[0]:.2f}  avg {statistics.mean(samples):.2f}  median {statistics.median(samples):.2f}"
          f"  p95 {p95:.2f}  max {samples[-1]:.2f}  (ms)")
    return 0


def cmd_throughput(p, args):
    """Memory read speed: DAP_TransferBlock reads of one 1 KB block, `window` packets in flight."""
    p.connect(args.clock)
    words = min((p.packet_size - 4) // 4, 256)
    req = bytes([ID_TRANSFER_BLOCK, 0]) + struct.pack("<H", words) + bytes([AP_R_DRW])
    tar = bytes([ID_TRANSFER, 0, 1, AP_W_TAR]) + struct.pack("<I", args.addr)
    in_flight = done = 0
    t_end = time.perf_counter() + args.seconds
    t0 = time.perf_counter()
    while True:
        while in_flight < args.window and time.perf_counter() < t_end:
            p.send_frame(tar)       # rewind TAR so every block stays inside one 1 KB page
            p.send_frame(req)
            in_flight += 1
        if in_flight == 0:
            break
        p.recv_frame()
        r = p.recv_frame()
        if r[3] != 1:
            raise DapError(f"block read failed, ACK 0x{r[3]:X}")
        in_flight -= 1
        done += 1
    dt = time.perf_counter() - t0
    print(f"throughput: {words}-word block reads, window {args.window}, {dt:.1f} s:")
    print(f"  {done / dt:.0f} blocks/s   {done * words * 4 / dt / 1024:.1f} KiB/s of target memory")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", required=True, help="probe IP (shown on the probe's screen)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--clock", type=int, default=1_000_000, help="SWD clock in Hz (default 1 MHz)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    auto = lambda s: int(s, 0)

    sub.add_parser("info", help="DAP_Info: vendor, product, packet size, ...")
    sub.add_parser("connect", help="SWD connect, read DPIDR, power up the debug domain")
    r = sub.add_parser("read32", help="read 32-bit words")
    r.add_argument("addr", type=auto)
    r.add_argument("count", type=auto, nargs="?", default=1)
    w = sub.add_parser("write32", help="write one 32-bit word and read it back")
    w.add_argument("addr", type=auto)
    w.add_argument("value", type=auto)
    d = sub.add_parser("dump", help="hex dump of target memory")
    d.add_argument("addr", type=auto)
    d.add_argument("length", type=auto, nargs="?", default=64)
    sub.add_parser("ping", help="one DAP_Info round trip")
    lat = sub.add_parser("latency", help="N DAP_Info round trips -> min/avg/p95/max")
    lat.add_argument("-n", type=int, default=1000)
    thr = sub.add_parser("throughput", help="memory read speed (needs a target or the SIM build)")
    thr.add_argument("--addr", type=auto, default=0x20000000)
    thr.add_argument("--seconds", type=float, default=5.0)
    thr.add_argument("--window", type=int, default=1, help="packets in flight (pipelining)")

    args = ap.parse_args()
    try:
        p = Probe(args.ip, args.port)
    except OSError as e:
        print(f"cannot connect to {args.ip}:{args.port}: {e}")
        return 2
    handlers = {"info": cmd_info, "connect": cmd_connect, "read32": cmd_read32, "write32": cmd_write32,
                "dump": cmd_dump, "ping": cmd_ping, "latency": cmd_latency, "throughput": cmd_throughput}
    try:
        return handlers[args.cmd](p, args)
    except DapError as e:
        print(f"error: {e}")
        return 1
    finally:
        p.close()


if __name__ == "__main__":
    sys.exit(main())
