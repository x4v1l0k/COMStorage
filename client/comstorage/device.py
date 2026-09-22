"""Shared COMStorage protocol client library."""

from __future__ import annotations

import json
import logging
import struct
import time
import zlib
from pathlib import Path
from typing import Any, Callable, Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Missing dependency: pip install -r requirements.txt") from exc

PROTOCOL_VERSION = "1.0"
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 30.0
CHUNK_SIZE = 4096
MAX_LINE = 65536

LOG = logging.getLogger("comstorage")

ProgressCb = Optional[Callable[[int, int], None]]


def crc32_bytes(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


class ComStorageError(RuntimeError):
    pass


class ComStorageClient:
    def __init__(
        self,
        port: str,
        baud: int = DEFAULT_BAUD,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> None:
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._ser: Optional[serial.Serial] = None
        self._req = 0

    @property
    def connected(self) -> bool:
        return bool(self._ser and self._ser.is_open)

    def connect(self) -> None:
        LOG.info("Opening %s @ %s", self.port, self.baud)
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            timeout=1.0,
            write_timeout=self.timeout,
            rtscts=False,
            dsrdtr=False,
        )
        time.sleep(1.2)
        self._ser.reset_input_buffer()
        self._ser.reset_output_buffer()
        LOG.info("Connected to %s", self.port)

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
            LOG.info("Disconnected")
        self._ser = None

    def __enter__(self) -> "ComStorageClient":
        self.connect()
        return self

    def __exit__(self, *args: Any) -> None:
        self.close()

    def _next_id(self) -> int:
        self._req += 1
        return self._req

    def _readline_json(self, deadline: float) -> dict[str, Any]:
        assert self._ser is not None
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self._ser.read(1)
            if not chunk:
                continue
            if chunk == b"\n":
                line = bytes(buf).decode("utf-8", errors="replace").strip("\r")
                buf.clear()
                if not line:
                    continue
                LOG.debug("<< %s", line[:500])
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    LOG.warning("Non-JSON line ignored: %s", line[:200])
                    continue
            buf.extend(chunk)
            if len(buf) > MAX_LINE:
                raise ComStorageError("response line too long")
        raise ComStorageError("timeout waiting for JSON response")

    def _wait_status(self, request_id: int, accept_ready: bool = False) -> dict[str, Any]:
        deadline = time.monotonic() + self.timeout
        while True:
            obj = self._readline_json(deadline)
            if obj.get("event") and "status" not in obj:
                LOG.info("device event: %s", obj)
                continue
            if "request_id" in obj and obj.get("request_id") not in (None, request_id):
                LOG.debug("Skipping unrelated response: %s", obj)
                continue
            status = obj.get("status")
            if status == "ready":
                if accept_ready:
                    return obj
                continue
            if status in ("ok", "error"):
                return obj
            LOG.debug("Ignoring: %s", obj)

    def request(self, cmd: str, **fields: Any) -> dict[str, Any]:
        assert self._ser is not None
        rid = self._next_id()
        payload = {"cmd": cmd, "request_id": rid, **fields}
        line = json.dumps(payload, separators=(",", ":")) + "\n"
        LOG.debug(">> %s", line.strip())
        self._ser.write(line.encode("utf-8"))
        self._ser.flush()
        resp = self._wait_status(rid, accept_ready=False)
        if resp.get("status") == "error":
            raise ComStorageError(
                f"{resp.get('error', 'error')}: {resp.get('message', resp)}"
            )
        return resp

    def request_ready(self, cmd: str, **fields: Any) -> tuple[int, dict[str, Any]]:
        assert self._ser is not None
        rid = self._next_id()
        payload = {"cmd": cmd, "request_id": rid, **fields}
        line = json.dumps(payload, separators=(",", ":")) + "\n"
        LOG.debug(">> %s", line.strip())
        self._ser.write(line.encode("utf-8"))
        self._ser.flush()
        ready = self._wait_status(rid, accept_ready=True)
        if ready.get("status") == "error":
            raise ComStorageError(
                f"{ready.get('error', 'error')}: {ready.get('message', ready)}"
            )
        if ready.get("status") != "ready":
            raise ComStorageError(f"expected ready, got {ready}")
        return rid, ready

    def _read_exact(self, n: int) -> bytes:
        assert self._ser is not None
        deadline = time.monotonic() + self.timeout
        buf = bytearray()
        while len(buf) < n:
            if time.monotonic() > deadline:
                raise ComStorageError("timeout reading binary")
            chunk = self._ser.read(n - len(buf))
            if chunk:
                buf.extend(chunk)
        return bytes(buf)

    def _write_exact(self, data: bytes) -> None:
        assert self._ser is not None
        self._ser.write(data)
        self._ser.flush()

    def info(self) -> dict[str, Any]:
        return self.request("info")

    def storage(self) -> dict[str, Any]:
        return self.request("storage")

    def ls(self, path: str = "/") -> dict[str, Any]:
        return self.request("list", path=path)

    def stat(self, path: str) -> dict[str, Any]:
        return self.request("stat", path=path)

    def delete(self, path: str) -> dict[str, Any]:
        return self.request("delete", path=path)

    def mkdir(self, path: str) -> dict[str, Any]:
        return self.request("mkdir", path=path)

    def rename(self, path: str, new_path: str) -> dict[str, Any]:
        return self.request("rename", path=path, new_path=new_path)

    def move(self, path: str, new_path: str) -> dict[str, Any]:
        return self.request("move", path=path, new_path=new_path)

    def touch(self, path: str) -> dict[str, Any]:
        return self.request("touch", path=path)

    def chmod(self, path: str, mode: str = "0644") -> dict[str, Any]:
        return self.request("chmod", path=path, mode=mode)

    def format_storage(self, filesystem: str, confirm: str = "yes") -> dict[str, Any]:
        """Destructive SD format. confirm must be exactly 'yes'. filesystem: fat32|ntfs."""
        old_timeout = self.timeout
        self.timeout = max(self.timeout, 180.0)
        try:
            return self.request("format", filesystem=filesystem, confirm=confirm)
        finally:
            self.timeout = old_timeout

    def hash(self, path: str) -> dict[str, Any]:
        return self.request("hash", path=path)

    def get(
        self,
        remote_path: str,
        local_path: str,
        progress: ProgressCb = None,
    ) -> dict[str, Any]:
        rid, ready = self.request_ready("get", path=remote_path)
        size = int(ready["size"])
        chunk_size = int(ready.get("chunk_size", CHUNK_SIZE))
        LOG.info("Downloading %s (%s bytes)", remote_path, size)

        out = Path(local_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        received = 0
        errors = 0
        running = 0

        with out.open("wb") as fh:
            while True:
                (chunk_len,) = struct.unpack("<I", self._read_exact(4))
                if chunk_len == 0:
                    break
                if chunk_len > chunk_size:
                    errors += 1
                    raise ComStorageError(f"chunk too large: {chunk_len}")
                data = self._read_exact(chunk_len)
                (chunk_crc,) = struct.unpack("<I", self._read_exact(4))
                if crc32_bytes(data) != chunk_crc:
                    errors += 1
                    raise ComStorageError("chunk CRC mismatch")
                fh.write(data)
                running = zlib.crc32(data, running) & 0xFFFFFFFF
                received += chunk_len
                if progress:
                    progress(received, size)

        done = self._wait_status(rid, accept_ready=False)
        if done.get("status") == "error":
            raise ComStorageError(f"get failed: {done}")
        if received != size:
            raise ComStorageError(f"size mismatch: got {received} expected {size}")
        device_crc = int(done.get("crc32", running))
        if device_crc != running:
            raise ComStorageError(
                f"file CRC mismatch: device={device_crc:#x} local={running:#x}"
            )
        done["local_path"] = str(out)
        done["errors"] = errors
        done["crc32_local"] = running
        return done

    def put(
        self,
        local_path: str,
        remote_path: str,
        progress: ProgressCb = None,
    ) -> dict[str, Any]:
        data_path = Path(local_path)
        if not data_path.is_file():
            raise ComStorageError(f"local file not found: {local_path}")
        size = data_path.stat().st_size
        rid, ready = self.request_ready("put", path=remote_path, size=size)
        chunk_size = int(ready.get("chunk_size", CHUNK_SIZE))
        LOG.info("Uploading %s -> %s (%s bytes)", local_path, remote_path, size)

        sent = 0
        with data_path.open("rb") as fh:
            while sent < size:
                chunk = fh.read(chunk_size)
                if not chunk:
                    break
                hdr = struct.pack("<I", len(chunk))
                crc = struct.pack("<I", crc32_bytes(chunk))
                self._write_exact(hdr + chunk + crc)
                sent += len(chunk)
                if progress:
                    progress(sent, size)

        done = self._wait_status(rid, accept_ready=False)
        if done.get("status") == "error":
            raise ComStorageError(f"put failed: {done}")
        return done


def list_serial_ports() -> list[dict[str, Any]]:
    rows = []
    for p in list_ports.comports():
        rows.append(
            {
                "device": p.device,
                "name": p.name,
                "description": p.description,
                "hwid": p.hwid,
                "vid": f"{p.vid:04X}" if p.vid is not None else None,
                "pid": f"{p.pid:04X}" if p.pid is not None else None,
                "manufacturer": p.manufacturer,
                "product": p.product,
                "serial_number": p.serial_number,
                "location": p.location,
                "interface": getattr(p, "interface", None),
            }
        )
    return rows


# Minimum score to treat a COM port as a likely ESP32-S3 / COMStorage dongle.
DONGLE_SCORE_MIN = 40


def score_dongle_port(p: dict[str, Any]) -> int:
    """Higher = more likely Espressif native USB CDC (T-Dongle-S3)."""
    score = 0
    vid = (p.get("vid") or "").upper()
    pid = (p.get("pid") or "").upper()
    desc = " ".join(
        str(x or "")
        for x in (p.get("description"), p.get("manufacturer"), p.get("product"), p.get("hwid"))
    ).lower()

    # Espressif USB VID — strongest signal for ESP32-S3 native USB CDC/JTAG.
    if vid == "303A":
        score += 100
    # Common Espressif USB-Serial/JTAG PIDs (not exhaustive).
    if vid == "303A" and pid in ("1001", "0002", "4001"):
        score += 20

    if "espressif" in desc:
        score += 50
    if "usb jtag" in desc or "usb-serial/jtag" in desc:
        score += 30
    if "usb serial device" in desc and vid == "303A":
        score += 15
    if "cdc" in desc and vid == "303A":
        score += 10

    # Clearly other UART bridges / Bluetooth — demote hard.
    if "ch340" in desc or "ch341" in desc:
        score -= 80
    if "cp210" in desc or "silicon labs" in desc:
        score -= 80
    if "ftdi" in desc or "ft232" in desc:
        score -= 80
    if "bluetooth" in desc or "standard serial over bluetooth" in desc:
        score -= 100
    if "arduino" in desc and vid != "303A":
        score -= 20
    if vid in ("1A86", "10C4", "0403", "067B"):  # WCH / Silabs / FTDI / Prolific
        score -= 60

    return score


def is_likely_dongle(p: dict[str, Any], min_score: int = DONGLE_SCORE_MIN) -> bool:
    """True only for ports that look like Espressif native USB CDC."""
    vid = (p.get("vid") or "").upper()
    if vid == "303A":
        return True
    desc = " ".join(
        str(x or "")
        for x in (p.get("description"), p.get("manufacturer"), p.get("product"), p.get("hwid"))
    ).lower()
    if "espressif" in desc:
        return True
    # Score alone is not enough — avoid promoting generic "USB Serial Device" without VID.
    return False


def filter_dongle_ports(
    ports: list[dict[str, Any]] | None = None,
    *,
    only_likely: bool = True,
    min_score: int = DONGLE_SCORE_MIN,
) -> list[dict[str, Any]]:
    """Return COM ports, optionally filtered to likely Espressif CDC dongles."""
    ports = list_serial_ports() if ports is None else ports
    scored = sorted(
        ((score_dongle_port(p), p) for p in ports),
        key=lambda t: t[0],
        reverse=True,
    )
    if not only_likely:
        return [p for _, p in scored]
    return [p for s, p in scored if is_likely_dongle(p, min_score=min_score)]


def join_remote(base: str, name: str) -> str:
    base = base.rstrip("/") or ""
    if not base:
        return "/" + name.lstrip("/")
    return base + "/" + name.lstrip("/")


def parent_remote(path: str) -> str:
    path = path.rstrip("/") or "/"
    if path == "/":
        return "/"
    parent = path.rsplit("/", 1)[0]
    return parent if parent else "/"
