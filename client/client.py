"""COMStorage Windows CLI — USB CDC / Serial filesystem PoC."""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
import time
from pathlib import Path
from typing import Optional

import serial

from comstorage import (
    DEFAULT_BAUD,
    DEFAULT_TIMEOUT,
    ComStorageClient,
    ComStorageError,
    filter_dongle_ports,
    list_serial_ports,
    score_dongle_port,
)

LOG = logging.getLogger("comstorage.cli")


def cmd_ports(args: argparse.Namespace) -> int:
    all_ports = list_serial_ports()
    ports = filter_dongle_ports(all_ports, only_likely=not args.all)
    if not ports:
        if all_ports and not args.all:
            print("No likely Espressif/CDC dongle ports. Use: python client.py ports --all")
        else:
            print("No serial ports found.")
        return 1
    print(f"{'COM':<10} {'Score':<6} {'VID:PID':<12} {'Manufacturer':<22} Description")
    print("-" * 100)
    for p in ports:
        vidpid = f"{p['vid']}:{p['pid']}" if p["vid"] else "-"
        score = score_dongle_port(p)
        print(
            f"{p['device']:<10} {score:<6} {vidpid:<12} {str(p['manufacturer'] or '-'):<22} "
            f"{p['description']}"
        )
    if not args.all:
        hidden = len(all_ports) - len(ports)
        if hidden > 0:
            print(f"\n({hidden} other COM port(s) hidden — pass --all to list them)")
    return 0


def _with_client(args: argparse.Namespace) -> ComStorageClient:
    return ComStorageClient(args.port, baud=args.baud, timeout=args.timeout)


def cmd_info(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.info(), indent=2))
        print(json.dumps(c.storage(), indent=2))
    return 0


def cmd_ls(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.ls(args.path), indent=2))
    return 0


def cmd_stat(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.stat(args.path), indent=2))
    return 0


def cmd_get(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.get(args.remote, args.local), indent=2))
    return 0


def cmd_put(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.put(args.local, args.remote), indent=2))
    return 0


def cmd_delete(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.delete(args.path), indent=2))
    return 0


def cmd_mkdir(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.mkdir(args.path), indent=2))
    return 0


def cmd_rename(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.rename(args.path, args.new_path), indent=2))
    return 0


def cmd_touch(args: argparse.Namespace) -> int:
    with _with_client(args) as c:
        print(json.dumps(c.touch(args.path), indent=2))
    return 0


def cmd_format(args: argparse.Namespace) -> int:
    if args.confirm != "yes":
        print('Refusing: pass --confirm yes (exactly) to format the SD card.')
        return 1
    with _with_client(args) as c:
        print(json.dumps(c.format_storage(args.filesystem, confirm="yes"), indent=2))
    return 0


def _fmt_rate(nbytes: int, seconds: float) -> str:
    if seconds <= 0:
        return "n/a"
    bps = nbytes / seconds
    if bps >= 1024 * 1024:
        return f"{bps / (1024 * 1024):.2f} MB/s"
    if bps >= 1024:
        return f"{bps / 1024:.2f} KB/s"
    return f"{bps:.0f} B/s"


def cmd_benchmark(args: argparse.Namespace) -> int:
    sizes = args.sizes or [4 * 1024, 64 * 1024, 1024 * 1024, 10 * 1024 * 1024]
    work = Path(args.workdir)
    work.mkdir(parents=True, exist_ok=True)

    with _with_client(args) as c:
        info = c.info()
        print("Device:", json.dumps(info, indent=2))
        free = int(c.storage().get("free_bytes", 0))
        print(f"Free storage: {free} bytes\n")

        for size in sizes:
            if size > free - 65536:
                print(f"SKIP {size} bytes — not enough free space")
                continue
            name = f"bench_{size}.bin"
            local = work / name
            remote = f"/{name}"
            with local.open("wb") as fh:
                block = os.urandom(min(65536, size))
                left = size
                while left > 0:
                    n = min(len(block), left)
                    fh.write(block[:n])
                    left -= n

            print("=" * 60)
            print(f"File: {name}")
            print(f"Size: {size} bytes ({size / (1024 * 1024):.3f} MB)")

            t0 = time.perf_counter()
            try:
                up = c.put(str(local), remote)
            except ComStorageError as exc:
                print(f"Upload FAILED: {exc}")
                continue
            up_time = time.perf_counter() - t0
            print("Upload:")
            print(f"  Time: {up_time:.2f} s")
            print(f"  Throughput: {_fmt_rate(size, up_time)}")
            print("  Errors: 0")
            print(f"  Device CRC: {up.get('crc32')}")

            down_local = work / f"down_{name}"
            t2 = time.perf_counter()
            try:
                down = c.get(remote, str(down_local))
                down_err = int(down.get("errors", 0))
            except ComStorageError as exc:
                print(f"Download FAILED: {exc}")
                c.delete(remote)
                continue
            down_time = time.perf_counter() - t2
            print("Download:")
            print(f"  Time: {down_time:.2f} s")
            print(f"  Throughput: {_fmt_rate(size, down_time)}")
            print(f"  Errors: {down_err}")
            same = local.read_bytes() == down_local.read_bytes()
            print(f"Integrity: {'OK' if same else 'MISMATCH'}")
            try:
                c.delete(remote)
            except ComStorageError:
                pass
            print()
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="COMStorage client — filesystem over USB CDC/Serial (PoC)"
    )
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    sub = p.add_subparsers(dest="command", required=True)

    sp = sub.add_parser("ports", help="List serial ports (dongle candidates by default)")
    sp.add_argument(
        "--all",
        action="store_true",
        help="Show every COM port, not only likely Espressif CDC dongles",
    )
    sp.set_defaults(func=cmd_ports)

    def add_port(sp_: argparse.ArgumentParser) -> None:
        sp_.add_argument("port", help="Serial port e.g. COM7")

    for name, help_, fn in [
        ("info", "Device + storage info", cmd_info),
    ]:
        sp = sub.add_parser(name, help=help_)
        add_port(sp)
        sp.set_defaults(func=fn)

    sp = sub.add_parser("ls", help="List directory")
    add_port(sp)
    sp.add_argument("path", nargs="?", default="/")
    sp.set_defaults(func=cmd_ls)

    sp = sub.add_parser("stat", help="File metadata")
    add_port(sp)
    sp.add_argument("path")
    sp.set_defaults(func=cmd_stat)

    sp = sub.add_parser("get", help="Download file")
    add_port(sp)
    sp.add_argument("remote")
    sp.add_argument("local")
    sp.set_defaults(func=cmd_get)

    sp = sub.add_parser("put", help="Upload file")
    add_port(sp)
    sp.add_argument("local")
    sp.add_argument("remote")
    sp.set_defaults(func=cmd_put)

    sp = sub.add_parser("delete", help="Delete file/dir")
    add_port(sp)
    sp.add_argument("path")
    sp.set_defaults(func=cmd_delete)

    sp = sub.add_parser("mkdir", help="Create directory")
    add_port(sp)
    sp.add_argument("path")
    sp.set_defaults(func=cmd_mkdir)

    sp = sub.add_parser("rename", help="Rename or move remote path")
    add_port(sp)
    sp.add_argument("path")
    sp.add_argument("new_path")
    sp.set_defaults(func=cmd_rename)

    sp = sub.add_parser("touch", help="Create empty remote file")
    add_port(sp)
    sp.add_argument("path")
    sp.set_defaults(func=cmd_touch)

    sp = sub.add_parser("format", help="Format SD card (destructive)")
    add_port(sp)
    sp.add_argument("filesystem", choices=["fat32", "ntfs"], help="Target FS (NTFS rejected by device)")
    sp.add_argument(
        "--confirm",
        required=True,
        help='Must be exactly "yes" to proceed',
    )
    sp.set_defaults(func=cmd_format)

    sp = sub.add_parser("benchmark", help="Throughput benchmark")
    add_port(sp)
    sp.add_argument("local_hint", nargs="?", default=None)
    sp.add_argument("--workdir", default="./bench_data")
    sp.add_argument("--sizes", nargs="*", type=int)
    sp.set_defaults(func=cmd_benchmark)

    return p


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    try:
        return int(args.func(args))
    except ComStorageError as exc:
        LOG.error("%s", exc)
        return 2
    except serial.SerialException as exc:
        LOG.error("Serial error: %s", exc)
        return 3


if __name__ == "__main__":
    sys.exit(main())
