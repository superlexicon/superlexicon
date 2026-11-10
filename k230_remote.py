#!/usr/bin/env python3
"""
k230_remote.py — Remote Wi-Fi OTA Update & Control CLI for LilyGO T-Display-K230 (RT-Smart)
Talks to k230_remoted.elf over UDP port 2223 (discovery) and TCP port 2222 (OTA/control).
"""

import argparse
import concurrent.futures
import json
import os
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

REMOTED_TCP_PORT = 2222
REMOTED_UDP_PORT = 2223
DEPLOY_DIR = Path("/home/mano/Projects/k230-deploy")


def discover_devices(timeout: float = 1.5, subnet_prefix: str = "192.168.50.") -> list[dict]:
    found_by_ip: dict[str, dict] = {}

    # 1. Fast UDP broadcast discovery on port 2223
    try:
        usock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        usock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        usock.settimeout(0.25)
        for bcast in ("255.255.255.255", f"{subnet_prefix}255"):
            try:
                usock.sendto(b"DISCOVER\n", (bcast, REMOTED_UDP_PORT))
            except OSError:
                pass

        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                data, addr = usock.recvfrom(2048)
                info = json.loads(data.decode("utf-8", errors="replace").strip())
                ip = info.get("ip") or addr[0]
                if ip == "0.0.0.0":
                    ip = addr[0]
                info["ip"] = ip
                found_by_ip[ip] = info
            except socket.timeout:
                for bcast in ("255.255.255.255", f"{subnet_prefix}255"):
                    try:
                        usock.sendto(b"DISCOVER\n", (bcast, REMOTED_UDP_PORT))
                    except OSError:
                        pass
            except Exception:
                pass
        usock.close()
    except Exception:
        pass

    # 2. If UDP broadcast was filtered or nothing answered yet, parallel TCP probe on port 2222
    if not found_by_ip:
        def probe_ip(ip: str):
            try:
                with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=0.35) as s:
                    s.sendall(b"STATUS\n")
                    line = recv_line(s)
                    if line.startswith("OK "):
                        info = json.loads(line[3:].strip())
                        info["ip"] = ip
                        return info
            except Exception:
                return None
            return None

        with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
            futures = [ex.submit(probe_ip, f"{subnet_prefix}{i}") for i in range(1, 255)]
            for fut in concurrent.futures.as_completed(futures):
                res = fut.result()
                if res:
                    found_by_ip[res["ip"]] = res

    return sorted(found_by_ip.values(), key=lambda d: d.get("ip", ""))


def recv_line(sock: socket.socket) -> str:
    buf = bytearray()
    while True:
        b = sock.recv(1)
        if not b:
            break
        if b == b"\n":
            break
        buf.extend(b)
    return buf.decode("utf-8", errors="replace").rstrip("\r")


def resolve_targets(target: str) -> list[str]:
    if target in ("--all", "all", "*"):
        devs = discover_devices()
        if not devs:
            print("No K230 devices discovered on Wi-Fi.", file=sys.stderr)
            sys.exit(1)
        return [d["ip"] for d in devs]

    # Check if already an IPv4 address
    parts = target.split(".")
    if len(parts) == 4 and all(p.isdigit() for p in parts):
        return [target]

    # Otherwise match by device name or MAC suffix
    devs = discover_devices()
    matched = [
        d["ip"]
        for d in devs
        if target.lower() in d.get("device", "").lower()
        or target.lower() in d.get("mac", "").lower()
    ]
    if not matched:
        print(f"Could not resolve K230 target '{target}'. Online devices: {[d.get('device') + '@' + d.get('ip') for d in devs]}", file=sys.stderr)
        sys.exit(1)
    return matched


def tcp_command(ip: str, cmd: str, timeout: float = 15.0) -> str:
    with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall((cmd + "\n").encode("utf-8"))
        return recv_line(s)


def put_file(ip: str, local_path: Path, remote_path: str) -> None:
    if not local_path.is_file():
        raise FileNotFoundError(f"Local file not found: {local_path}")
    data = local_path.read_bytes()
    size = len(data)
    expected_crc = zlib.crc32(data) & 0xFFFFFFFF

    print(f"[{ip}] Uploading {local_path.name} -> {remote_path} ({size:,} bytes, CRC32={expected_crc:08x})...")
    t0 = time.time()
    with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=30.0) as s:
        s.settimeout(120.0)
        s.sendall(f"PUT {remote_path} {size}\n".encode("utf-8"))
        ready = recv_line(s)
        if ready != "READY":
            raise RuntimeError(f"[{ip}] Unexpected response to PUT: {ready}")

        sent = 0
        chunk_sz = 32768
        while sent < size:
            end = min(sent + chunk_sz, size)
            s.sendall(memoryview(data)[sent:end])
            sent = end
            pct = (sent * 100.0) / size if size else 100.0
            sys.stdout.write(f"\r[{ip}]   {sent:,}/{size:,} bytes ({pct:5.1f}%)")
            sys.stdout.flush()

        sys.stdout.write("\n")
        resp = recv_line(s)
        dt = max(time.time() - t0, 0.001)
        if not resp.startswith("OK "):
            raise RuntimeError(f"[{ip}] PUT failed: {resp}")
        parts = resp.split()
        remote_crc = int(parts[2], 16) if len(parts) >= 3 else 0
        if remote_crc != expected_crc:
            raise RuntimeError(f"[{ip}] CRC32 mismatch! local={expected_crc:08x} remote={remote_crc:08x}")
        print(f"[{ip}] OK ({size / 1024.0 / dt:.1f} KB/s, verified CRC32 {remote_crc:08x})")


def put_kernel(ip: str, kernel_path: Path) -> None:
    if not kernel_path.is_file():
        raise FileNotFoundError(f"Kernel binary not found: {kernel_path}")
    data = kernel_path.read_bytes()
    size = len(data)
    expected_crc = zlib.crc32(data) & 0xFFFFFFFF

    print(f"[{ip}] Flashing RT-Smart kernel {kernel_path.name} -> /dev/sd1 @ 10MiB ({size:,} bytes, CRC32={expected_crc:08x})...")
    t0 = time.time()
    with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=30.0) as s:
        s.settimeout(180.0)
        s.sendall(f"PUT_KERNEL {size}\n".encode("utf-8"))
        ready = recv_line(s)
        if ready != "READY":
            raise RuntimeError(f"[{ip}] Unexpected response to PUT_KERNEL: {ready}")

        sent = 0
        chunk_sz = 32768
        while sent < size:
            end = min(sent + chunk_sz, size)
            s.sendall(memoryview(data)[sent:end])
            sent = end
            pct = (sent * 100.0) / size if size else 100.0
            sys.stdout.write(f"\r[{ip}]   {sent:,}/{size:,} bytes ({pct:5.1f}%)")
            sys.stdout.flush()

        sys.stdout.write("\n")
        resp = recv_line(s)
        dt = max(time.time() - t0, 0.001)
        if not resp.startswith("OK "):
            raise RuntimeError(f"[{ip}] PUT_KERNEL failed: {resp}")
        parts = resp.split()
        remote_crc = int(parts[2], 16) if len(parts) >= 3 else 0
        if remote_crc != expected_crc:
            raise RuntimeError(f"[{ip}] Kernel CRC32 mismatch! local={expected_crc:08x} remote={remote_crc:08x}")
        print(f"[{ip}] Kernel flashed & verified ({size / 1024.0 / dt:.1f} KB/s, CRC32 {remote_crc:08x})")


def get_file(ip: str, remote_path: str, offset: int = 0, max_bytes: int = 0) -> bytes:
    with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=15.0) as s:
        s.settimeout(60.0)
        s.sendall(f"GET {remote_path} {offset} {max_bytes}\n".encode("utf-8"))
        hdr = recv_line(s)
        if not hdr.startswith("OK "):
            raise RuntimeError(f"[{ip}] GET {remote_path} failed: {hdr}")
        parts = hdr.split()
        length = int(parts[1])
        expected_crc = int(parts[2], 16)
        buf = bytearray()
        while len(buf) < length:
            chunk = s.recv(min(32768, length - len(buf)))
            if not chunk:
                break
            buf.extend(chunk)
        actual_crc = zlib.crc32(buf) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise RuntimeError(f"[{ip}] GET CRC32 mismatch: expected {expected_crc:08x}, got {actual_crc:08x}")
        return bytes(buf)


def cmd_discover(_args) -> None:
    devs = discover_devices()
    if not devs:
        print("No K230 devices discovered on Wi-Fi (UDP 2223 / TCP 2222).")
        return
    print(f"{'DEVICE':<24} {'IP':<16} {'MAC':<19} {'SSID':<14} {'TEMP':<8} {'UPTIME':<8} {'MODE'}")
    print("-" * 105)
    for d in devs:
        print(
            f"{d.get('device', '?'):<24} "
            f"{d.get('ip', '?'):<16} "
            f"{d.get('mac', '?'):<19} "
            f"{d.get('ssid', '?'):<14} "
            f"{d.get('temp_c', 0.0):>4.1f} C  "
            f"{d.get('uptime_s', 0):>6}s  "
            f"{d.get('mode', '?')}"
        )


def cmd_status(args) -> None:
    ips = resolve_targets(args.target)
    for ip in ips:
        resp = tcp_command(ip, "STATUS")
        if resp.startswith("OK "):
            info = json.loads(resp[3:])
            print(json.dumps(info, indent=2))
        else:
            print(f"[{ip}] {resp}")


def cmd_ls(args) -> None:
    for ip in resolve_targets(args.target):
        with socket.create_connection((ip, REMOTED_TCP_PORT), timeout=10.0) as s:
            s.sendall(f"LS {args.path}\n".encode("utf-8"))
            hdr = recv_line(s)
            if not hdr.startswith("OK"):
                print(f"[{ip}] {hdr}")
                continue
            print(f"=== {ip}:{args.path} ===")
            while True:
                line = recv_line(s)
                if line == "END" or not line:
                    break
                parts = line.split("\t")
                if len(parts) >= 2:
                    print(f"  {parts[0]:<32} {int(parts[1]):>12,} bytes")
                else:
                    print(f"  {line}")


def cmd_logs(args) -> None:
    for ip in resolve_targets(args.target):
        print(f"=== [{ip}] {args.file} (last {args.bytes} bytes) ===")
        try:
            raw = get_file(ip, args.file, offset=-abs(args.bytes), max_bytes=abs(args.bytes))
            sys.stdout.write(raw.decode("utf-8", errors="replace"))
            if not raw.endswith(b"\n"):
                print()
        except Exception as e:
            print(f"  ({e})")


def cmd_put(args) -> None:
    local_p = Path(args.local_file)
    for ip in resolve_targets(args.target):
        put_file(ip, local_p, args.remote_path)


def cmd_get(args) -> None:
    ips = resolve_targets(args.target)
    raw = get_file(ips[0], args.remote_path)
    if args.local_file == "-":
        sys.stdout.buffer.write(raw)
    else:
        out = Path(args.local_file or Path(args.remote_path).name)
        out.write_bytes(raw)
        print(f"Saved {len(raw):,} bytes to {out}")


def cmd_put_kernel(args) -> None:
    kp = Path(args.kernel_bin)
    for ip in resolve_targets(args.target):
        put_kernel(ip, kp)
        if args.reboot:
            print(f"[{ip}] Rebooting...")
            print(f"[{ip}] {tcp_command(ip, 'REBOOT')}")


def cmd_deploy(args) -> None:
    ips = resolve_targets(args.target)
    files_to_push = []
    if not args.only or "node" in args.only:
        files_to_push.append((DEPLOY_DIR / "superlexicon.elf", "/sdcard/superlexicon.elf"))
    if not args.only or "ui" in args.only:
        files_to_push.append((DEPLOY_DIR / "app.elf", "/sdcard/app.elf"))
    if not args.only or "radio" in args.only:
        files_to_push.append((DEPLOY_DIR / "lora_radio.elf", "/sdcard/lora_radio.elf"))
    if not args.only or "remoted" in args.only:
        files_to_push.append((DEPLOY_DIR / "k230_remoted.elf", "/sdcard/k230_remoted.elf"))
    if not args.only or "init" in args.only:
        files_to_push.append((DEPLOY_DIR / "init.sh", "/sdcard/init.sh"))
        files_to_push.append((DEPLOY_DIR / "init.sh", "/bin/init.sh"))
    if args.include_config:
        files_to_push.append((DEPLOY_DIR / "k230-config.json", "/sdcard/k230-config.json"))

    for ip in ips:
        print(f"\n>>> Deploying to K230 @ {ip} <<<")
        for local_p, remote_p in files_to_push:
            if local_p.is_file():
                put_file(ip, local_p, remote_p)
        if args.reboot:
            print(f"[{ip}] Rebooting K230 to activate new binaries...")
            print(f"[{ip}] {tcp_command(ip, 'REBOOT')}")


def cmd_mode(args) -> None:
    for ip in resolve_targets(args.target):
        resp = tcp_command(ip, f"MODE {args.mode} {args.subnet} {args.freq}")
        print(f"[{ip}] {resp}")
        if args.reboot:
            print(f"[{ip}] {tcp_command(ip, 'REBOOT')}")


def cmd_wifi(args) -> None:
    for ip in resolve_targets(args.target):
        resp = tcp_command(ip, f"WIFI {args.ssid} {args.password}")
        print(f"[{ip}] {resp}")


def cmd_reboot(args) -> None:
    for ip in resolve_targets(args.target):
        try:
            print(f"[{ip}] {tcp_command(ip, 'REBOOT', timeout=4.0)}")
        except Exception:
            usock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            usock.settimeout(2.0)
            usock.sendto(b"REBOOT\n", (ip, REMOTED_UDP_PORT))
            try:
                data, _ = usock.recvfrom(256)
                print(f"[{ip}] (UDP) {data.decode('utf-8', errors='replace').strip()}")
            except Exception as e:
                print(f"[{ip}] UDP REBOOT sent ({e})")
            finally:
                usock.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="K230 RT-Smart Remote Wi-Fi OTA & Control Tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("discover", help="Discover online K230 devices on Wi-Fi").set_defaults(func=cmd_discover)

    p_st = sub.add_parser("status", help="Query status of one or all K230 devices")
    p_st.add_argument("target", nargs="?", default="--all", help="IP, device suffix, or --all")
    p_st.set_defaults(func=cmd_status)

    p_ls = sub.add_parser("ls", help="List files on K230 directory (/sdcard, /bin, /tmp)")
    p_ls.add_argument("target", help="IP, device suffix, or --all")
    p_ls.add_argument("path", nargs="?", default="/sdcard")
    p_ls.set_defaults(func=cmd_ls)

    p_logs = sub.add_parser("logs", help="Fetch tail of log file from K230")
    p_logs.add_argument("target", nargs="?", default="--all")
    p_logs.add_argument("-f", "--file", default="/sdcard/remoted.log")
    p_logs.add_argument("-n", "--bytes", type=int, default=8192)
    p_logs.set_defaults(func=cmd_logs)

    p_put = sub.add_parser("put", help="Upload a file to K230 (/sdcard/... or /bin/...)")
    p_put.add_argument("target", help="IP, device suffix, or --all")
    p_put.add_argument("local_file")
    p_put.add_argument("remote_path")
    p_put.set_defaults(func=cmd_put)

    p_get = sub.add_parser("get", help="Download a file from K230")
    p_get.add_argument("target", help="IP or device suffix")
    p_get.add_argument("remote_path")
    p_get.add_argument("local_file", nargs="?", default=None)
    p_get.set_defaults(func=cmd_get)

    p_pk = sub.add_parser("put-kernel", help="Flash RT-Smart opensbi_rtt_system.bin to /dev/sd1 @ 10MiB")
    p_pk.add_argument("target", help="IP, device suffix, or --all")
    p_pk.add_argument("kernel_bin", help="Path to opensbi_rtt_system.bin")
    p_pk.add_argument("--reboot", action="store_true", help="Reboot after flashing kernel")
    p_pk.set_defaults(func=cmd_put_kernel)

    p_dep = sub.add_parser("deploy", help="Push latest binaries from /home/mano/Projects/k230-deploy over Wi-Fi")
    p_dep.add_argument("target", nargs="?", default="--all", help="IP, device suffix, or --all")
    p_dep.add_argument("--only", nargs="*", choices=["node", "ui", "radio", "remoted", "init"], help="Push subset of binaries")
    p_dep.add_argument("--include-config", action="store_true", help="Also overwrite /sdcard/k230-config.json")
    p_dep.add_argument("--reboot", action="store_true", default=True, help="Reboot after deploy (default: true)")
    p_dep.add_argument("--no-reboot", dest="reboot", action="store_false", help="Do not reboot after deploy")
    p_dep.set_defaults(func=cmd_deploy)

    p_mode = sub.add_parser("mode", help="Switch K230 between roaming and validator join mode")
    p_mode.add_argument("target", help="IP, device suffix, or --all")
    p_mode.add_argument("mode", choices=["roaming", "join", "validator"])
    p_mode.add_argument("--subnet", default="00000001")
    p_mode.add_argument("--freq", type=int, default=915000000)
    p_mode.add_argument("--reboot", action="store_true")
    p_mode.set_defaults(func=cmd_mode)

    p_wifi = sub.add_parser("wifi", help="Update /sdcard/wifi.conf credentials remotely")
    p_wifi.add_argument("target", help="IP, device suffix, or --all")
    p_wifi.add_argument("ssid")
    p_wifi.add_argument("password")
    p_wifi.set_defaults(func=cmd_wifi)

    p_reb = sub.add_parser("reboot", help="Trigger hardware watchdog reboot on K230")
    p_reb.add_argument("target", nargs="?", default="--all")
    p_reb.set_defaults(func=cmd_reboot)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
