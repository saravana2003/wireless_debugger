"""pyOCD probe plugin for the Wireless Debugger: CMSIS-DAP packets over TCP.

Select the probe explicitly, because a network probe can't be enumerated like a USB one:
    pyocd gdbserver --probe wdbg:192.168.1.61 --target r7fa8d1bh
    pyocd commander --probe wdbg:127.0.0.1:4441 --target cortex_m

Framing: OpenOCD's cmsis-dap-tcp, see lib/net_server/framing_dap_tcp.h.

This builds on pyOCD internals (pydapaccess), so tools/pyocd_wdbg/pyproject.toml pins the version.
"""
import socket
import struct

from pyocd.core.plugin import Plugin
from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe
from pyocd.probe.pydapaccess.dap_access_cmsis_dap import DAPAccessCMSISDAP
from pyocd.probe.pydapaccess.interface.interface import Interface

DEFAULT_PORT = 4441  # WDBG_TCP_PORT in include/app_config.h
CONNECT_TIMEOUT_S = 5.0
HEADER = struct.Struct("<IHBB")  # signature, payload length, packet type, reserved
SIGNATURE, TYPE_REQUEST, TYPE_RESPONSE = 0x00504144, 1, 2


class TcpInterface(Interface):
    """pydapaccess transport: one CMSIS-DAP packet per framed TCP message."""

    def __init__(self, host, port):
        super().__init__()
        self.host, self.port = host, port
        self.vendor_name = "WDBG"
        self.product_name = "Wireless Debugger"
        self.serial_number = f"{host}:{port}"
        self.sock = None

    def open(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=CONNECT_TIMEOUT_S)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(self.DEFAULT_USB_TIMEOUT_S)

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def write(self, data):
        self.sock.sendall(HEADER.pack(SIGNATURE, len(data), TYPE_REQUEST, 0) + bytes(data))

    def _recv_exact(self, count):
        buf = bytearray()
        while len(buf) < count:
            chunk = self.sock.recv(count - len(buf))
            if not chunk:
                raise ConnectionError("probe closed the connection")
            buf += chunk
        return bytes(buf)

    def read(self):
        signature, length, kind, _ = HEADER.unpack(self._recv_exact(HEADER.size))
        if signature != SIGNATURE or kind != TYPE_RESPONSE:
            raise ConnectionError(f"bad frame from probe: signature 0x{signature:08X}, type {kind}")
        return list(self._recv_exact(length))


class WdbgProbe(CMSISDAPProbe):
    """Everything above the transport is pyOCD's normal CMSIS-DAP probe."""

    @classmethod
    def get_all_connected_probes(cls, unique_id=None, is_explicit=False):
        probe = cls.get_probe_with_id(unique_id, is_explicit) if unique_id else None
        return [probe] if probe else []

    @classmethod
    def get_probe_with_id(cls, unique_id, is_explicit=False):
        # Only when asked by name (--probe wdbg:<ip>[:<port>]): never answer other probes' lookups.
        if not is_explicit:
            return None
        host, _, port = unique_id.partition(":")
        return cls(DAPAccessCMSISDAP(None, interface=TcpInterface(host, int(port) if port else DEFAULT_PORT)))


class WdbgProbePlugin(Plugin):
    def load(self):
        return WdbgProbe

    @property
    def name(self):
        return "wdbg"

    @property
    def description(self):
        return "Wireless Debugger: CMSIS-DAP over TCP"
