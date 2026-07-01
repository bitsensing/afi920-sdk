#!/usr/bin/env python3
"""
AFI920 software update utility.

Discovers AFI920 sensors on the network and flashes each one from a software
bundle, in parallel (up to 8 sensors at once). For every sensor it compares each
domain's git version in the bundle against the hash the sensor reports via
GetDeviceInfo and flashes only the domains that differ, then reboots and
verifies.

Usage::

    python tools/software_update.py <bundle.tar> --password <pw>
    python tools/software_update.py <bundle.tar> --ip 192.168.10.150 --force
    AFI_UPDATE_PASSWORD=<pw> python tools/software_update.py <bundle.tar>

The bundle password is NOT shipped with the SDK; pass it via --password or the
AFI_UPDATE_PASSWORD environment variable. Decryption needs the 'cryptography'
package (pip install afi_sdk[update]).

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import os
import sys
import threading
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed

# Allow running from a clean checkout without installing the package first.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python", "src"))

from afi_sdk import AfiSdk                          # noqa: E402
from afi_sdk import software_update as su           # noqa: E402
from afi_sdk.software_update import Progress        # noqa: E402

MAX_PARALLEL = 8

# ANSI colors (only when writing to a real terminal).
_TTY = sys.stdout.isatty()
RED = "\x1b[1;31m" if _TTY else ""
RESET = "\x1b[0m" if _TTY else ""


# ── Live progress UI (stdlib ANSI, thread-safe) ──

def _bar(pct: int, width: int = 20) -> str:
    pct = max(0, min(100, pct))
    fill = pct * width // 100
    return "[" + "#" * fill + "-" * (width - fill) + f"] {pct:3d}%"


def _fmt_line(host: str, p) -> str:
    # "[i/N]" domain counter (e.g. bootloader=1/3, driver=2/3, firmware=3/3).
    count = f"[{p.domain_index}/{p.domain_total}] " if p.domain_index else ""
    who = f"{p.domain}/{p.component}" if p.component else (p.domain or "")
    who = f"{count}{who}"
    if p.state in ("sending", "writing", "verifying"):
        return f"{host:<16} {who:<26} {p.state:<10} {_bar(p.pct)}"
    label = {
        "connecting": "connecting...", "checking": "checking hashes...",
        "skip": "unchanged - skipped", "switch": "switching slot...",
        "reboot": "rebooting...", "verify": "verifying reboot...",
        "done": "done", "error": "ERROR", "queued": "queued",
    }.get(p.state, p.state)
    return f"{host:<16} {who:<26} {label}"


class ProgressUI:
    """Renders one live line per sensor. On a TTY, redraws the block in place;
    otherwise prints a line only when a sensor's state changes."""

    def __init__(self, hosts, enabled: bool):
        self.hosts = list(hosts)
        self.enabled = enabled and sys.stdout.isatty()
        self.lock = threading.Lock()
        self.lines = {h: _fmt_line(h, Progress("queued")) for h in self.hosts}
        self._last_key = {h: "" for h in self.hosts}
        self._drawn = False

    def start(self):
        if self.enabled:
            with self.lock:
                sys.stdout.write("\n".join(self.lines[h] for h in self.hosts) + "\n")
                sys.stdout.flush()
                self._drawn = True

    def update(self, host, p: Progress):
        line = _fmt_line(host, p)
        with self.lock:
            self.lines[host] = line
            if self.enabled:
                self._redraw()
            else:
                # Plain mode: log only on a meaningful change (not every pct),
                # so a long transfer doesn't flood the output.
                key = f"{p.state}:{p.domain}:{p.component}"
                if key != self._last_key[host]:
                    print(f"  {line}")
                    self._last_key[host] = key

    def _redraw(self):
        n = len(self.hosts)
        if self._drawn:
            sys.stdout.write(f"\x1b[{n}A")     # cursor up n lines
        for h in self.hosts:
            sys.stdout.write("\x1b[2K" + self.lines[h] + "\n")   # clear + write
        sys.stdout.flush()
        self._drawn = True


# ── Discovery + duplicate-IP guard ──

def discover_targets(bind_ip: str, only_ips):
    sensors = AfiSdk.discover(timeout_s=3.0, bind_ip=bind_ip)
    for s in sensors:
        s["_ip"] = s.get("ip_address") or s.get("_source_ip") or ""
    if only_ips:
        wanted = set(only_ips)
        sensors = [s for s in sensors if s["_ip"] in wanted]
    return sensors


def find_ip_conflicts(sensors):
    """Return {ip: [serials]} for IPs claimed by more than one distinct serial."""
    by_ip = defaultdict(set)
    for s in sensors:
        by_ip[s["_ip"]].add(s.get("serial_number", "?"))
    return {ip: sorted(serials) for ip, serials in by_ip.items() if len(serials) > 1}


# ── Per-sensor worker ──

def run_one(sensor, args, password, ui: ProgressUI) -> su.SensorResult:
    host = sensor["_ip"]

    def cb(p: Progress):
        ui.update(host, p)

    single = args.single or not su.is_software_bundle(args.bundle)
    if single:
        res = su.update_sensor_single(
            host, args.bundle, password, port=args.port, force=args.force,
            components=args.components, timeout=args.timeout, on_progress=cb)
    else:
        res = su.update_sensor(
            host, args.bundle, password, port=args.port,
            flash_domains=su.flash_domains_for(),
            force=args.force, components=args.components,
            timeout=args.timeout, on_progress=cb)

    if res.ok and res.flashed and not args.no_reboot:
        cb(Progress("reboot"))
        su.reboot(host, port=args.port)
        res.rebooted = True
        if not args.no_verify:
            cb(Progress("verify"))
            res.verified = su.verify_target(
                host, res.flashed, port=args.port, bind_ip=args.bind_ip)
    return res


# ── Summary ──

def print_summary(results) -> bool:
    print("\n=== Summary ===")
    all_ok = True
    for res in results:
        if res.error:
            print(f"  {res.host:<16} ERROR: {res.error}")
            all_ok = False
            continue
        parts = []
        for d in res.domains:
            parts.append(f"{d.domain}={d.status}"
                         + (f"({d.error})" if d.error else ""))
        if not res.domains:
            parts.append("nothing to flash")
        verify = ""
        if res.rebooted:
            verify = " verify=" + ("OK" if res.verified else
                                   ("SKIP" if res.verified is None else "FAIL"))
        line = f"  {res.host:<16} " + ", ".join(parts) + verify
        print(line)
        if not res.ok or res.verified is False:
            all_ok = False
    return all_ok


def main() -> int:
    p = argparse.ArgumentParser(description="Flash AFI920 sensors from a software bundle.")
    p.add_argument("bundle", help="software bundle (.tar) or single-domain package (.tar.gz[.enc])")
    p.add_argument("--password", default=os.environ.get("AFI_UPDATE_PASSWORD", ""),
                   help="bundle decryption password (or set AFI_UPDATE_PASSWORD)")
    p.add_argument("--ip", default="", help="comma-separated IPs to target (default: all discovered)")
    p.add_argument("--bind-ip", default="", help="local IP to bind for discovery broadcast")
    p.add_argument("--port", type=int, default=30500, help="Config API port (default 30500)")
    p.add_argument("--force", action="store_true",
                   help="flash every domain even if its hash is unchanged")
    p.add_argument("--components", default="",
                   help="comma-separated component names to flash (partial; disables skip)")
    p.add_argument("--single", action="store_true",
                   help="treat input as a single-domain package (auto-detected otherwise)")
    p.add_argument("--no-reboot", action="store_true", help="do not reboot after flashing")
    p.add_argument("--no-verify", action="store_true", help="skip post-reboot verification")
    p.add_argument("--timeout", type=float, default=120.0, help="per-component flash timeout (s)")
    p.add_argument("--no-progress", action="store_true", help="plain line output (no live redraw)")
    args = p.parse_args()

    if not os.path.exists(args.bundle):
        print(f"Bundle not found: {args.bundle}", file=sys.stderr)
        return 1
    args.components = set(c.strip() for c in args.components.split(",") if c.strip()) or None
    only_ips = [x.strip() for x in args.ip.split(",") if x.strip()]

    encrypted_input = args.bundle.endswith(".enc") or su.is_software_bundle(args.bundle)
    if encrypted_input and not args.password:
        print("A bundle password is required (--password or AFI_UPDATE_PASSWORD).",
              file=sys.stderr)
        return 1

    print("Discovering sensors...")
    sensors = discover_targets(args.bind_ip, only_ips)
    if not sensors:
        print("No sensors found.", file=sys.stderr)
        return 1

    # Duplicate-IP guard: two sensors answering on the same IP cannot be
    # addressed reliably — tell the user to fix IPs first, then stop.
    conflicts = find_ip_conflicts(sensors)
    if conflicts:
        print("\nDuplicate IP detected — configure sensor IP addresses first, "
              "then re-run the update:", file=sys.stderr)
        for ip, serials in conflicts.items():
            print(f"  {ip} is used by {len(serials)} sensors: {', '.join(serials)}",
                  file=sys.stderr)
        print("\nUse set_network_config / SetNetworkConfig to assign unique IPs.",
              file=sys.stderr)
        return 2

    if len(sensors) > MAX_PARALLEL:
        print(f"Found {len(sensors)} sensors; updating the first {MAX_PARALLEL} "
              f"(max parallel).")
        sensors = sensors[:MAX_PARALLEL]

    hosts = [s["_ip"] for s in sensors]
    print(f"Updating {len(hosts)} sensor(s): {', '.join(hosts)}\n")

    print(f"{RED}"
          "================================================================\n"
          " !! DO NOT disconnect power or network during the update !!\n"
          "    Interrupting an update in progress may brick the device.\n"
          "================================================================"
          f"{RESET}\n")

    ui = ProgressUI(hosts, enabled=not args.no_progress)
    ui.start()

    results = []
    with ThreadPoolExecutor(max_workers=min(MAX_PARALLEL, len(sensors))) as ex:
        futures = {ex.submit(run_one, s, args, args.password, ui): s for s in sensors}
        for fut in as_completed(futures):
            s = futures[fut]
            try:
                results.append(fut.result())
            except Exception as e:  # defensive: never lose a sensor's outcome
                r = su.SensorResult(host=s["_ip"], error=f"unexpected: {e}")
                results.append(r)

    ok = print_summary(results)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
