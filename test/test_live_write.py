"""
Live write-path verification against one AFI920 sensor (interface v1.1.0,
firmware v3.1.1). DESTRUCTIVE: it rewrites the sensor's NVM settings, runs
ResetConfigDefaults and ResetAllSettings, restarts the sensor several times,
and enters the programming access mode. Every setting it touches is read
first and written back at the end; a failure part way leaves the sensor in
whatever state the failed step reached.

Sections:
  [M] Access mode gate   — SETs, ResetAllSettings and the firmware chain
                           refused in Normal; one-way SetMode
  [W] SET sweep          — network, detection threshold and filters, PTP
                           profile, mounting, rotation, position code; NVM,
                           applied after Restart, read back by GET and
                           Discovery; a health stream moved to UDP arrives
  [G] Range mode         — SetRangeMode applies after Restart
  [D] Defaults           — ResetConfigDefaults by section; ResetAllSettings
                           keeps the sensor's own address
  [X] Restore            — baseline written back and read back
  [P] Programming mode   — streams halt, SETs refused, Restart returns Normal

Usage:
    python test/test_live_write.py 192.168.10.150 --yes-mutate

Exit code 0 = every hard check passed.
"""

import argparse
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

from afi_sdk import (  # noqa: E402
    AccessMode, AfiSdk, AfiSdkConfig, OK, ACCESS_DENIED, INVALID_PARAM,
    NOT_SUPPORTED, SESSION_REQUIRED,
)
from afi_sdk import firmware as fw  # noqa: E402
from afi_sdk import someip  # noqa: E402

EVENT_RDI, EVENT_SHII = 0x8002, 0x8003
UDP_TEST_PORT = 40510      # outside the WSL mirrored range 28240-32335


class Checks:
    def __init__(self):
        self.passed = 0
        self.failed = []

    def check(self, sec, ok, name, detail=""):
        tag = "PASS" if ok else "FAIL"
        print(f"  [{sec}] {tag}: {name}" + (f" - {detail}" if detail != "" else ""))
        if ok:
            self.passed += 1
        else:
            self.failed.append(f"[{sec}] {name} ({detail})")
        return ok

    def info(self, sec, text):
        print(f"  [{sec}] info: {text}")


# ── stream probes ──

def _someip_event(frame: bytes) -> int:
    return struct.unpack_from(">I", frame, 0)[0] & 0xFFFF if len(frame) >= 16 else -1


def count_tcp(ip, port, event, seconds):
    """Count SOME/IP messages of ``event`` on a TCP stream for ``seconds``."""
    n = 0
    try:
        s = socket.create_connection((ip, port), timeout=3)
    except OSError:
        return -1
    s.settimeout(0.5)
    buf = b""
    end = time.monotonic() + seconds
    try:
        while time.monotonic() < end:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while len(buf) >= 16:
                length = struct.unpack_from(">I", buf, 4)[0]
                if len(buf) < 8 + length:
                    break
                if _someip_event(buf) == event:
                    n += 1
                buf = buf[8 + length:]
    finally:
        s.close()
    return n


def count_udp(sensor_ip, port, event, seconds):
    """Count SOME/IP datagrams of ``event`` from ``sensor_ip`` on a UDP port."""
    n = 0
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("", port))
    s.settimeout(0.5)
    end = time.monotonic() + seconds
    try:
        while time.monotonic() < end:
            try:
                data, src = s.recvfrom(65536)
            except socket.timeout:
                continue
            if src[0] == sensor_ip and _someip_event(data) == event:
                n += 1
    finally:
        s.close()
    return n


# ── helpers ──

def discover_one(ip, tries=3):
    for _ in range(tries):
        for rec in someip.discover(timeout_s=1.5, broadcast_ip=ip):
            if rec.get("_source_ip") == ip:
                return rec
    return None


def restart_and_reconnect(sdk, ip, c, sec, via_reset_all=False):
    """Restart (or ResetAllSettings), wait for a lower Discovery uptime, reconnect."""
    before = fw.radar_uptime(ip) or 0
    rc = sdk.reset_all_settings() if via_reset_all else sdk.restart()
    name = "ResetAllSettings accepted" if via_reset_all else "Restart accepted"
    c.check(sec, rc == OK, name, sdk.last_result and sdk.last_result.get("return_name"))
    t0 = time.monotonic()
    rec = fw.wait_for_restart(ip, before, timeout_s=120)
    c.check(sec, rec is not None, "sensor rebooted (Discovery uptime dropped)",
            f"{time.monotonic() - t0:.1f}s")
    ok = sdk.reconnect(timeout_s=60) == OK
    c.check(sec, ok, "Config API reconnect")
    mode = sdk.get_mode() or {}
    c.check(sec, mode.get("access_mode") == AccessMode.NORMAL,
            "access mode is Normal after reboot", mode.get("access_mode_name"))
    return rec


def snapshot(sdk):
    return {
        "network": sdk.get_network_config(),
        "range_mode": sdk.get_range_mode(),
        "threshold": sdk.get_detection_threshold(),
        "filters": sdk.get_detection_filters(),
        "ptp_profile": (sdk.get_ptp_config() or {}).get("configured_profile"),
        "mounting": sdk.get_mounting_position(),
        "rotation": sdk.get_rotation(),
        "position": sdk.get_sensor_position(),
    }


STREAM_KEYS = [f"{s}_{k}" for s in ("detection", "health", "performance")
               for k in ("ip", "port", "protocol")]


def write_settings(sdk, snap, c, sec):
    """Write every setting of ``snap`` (diagnostic mode required)."""
    net = {k: snap["network"][k] for k in STREAM_KEYS}
    c.check(sec, sdk.set_network_config(**net) is not None, "SetNetworkConfig", sdk.last_error)
    th = snap["threshold"]
    kw = {"snr_db": th["snr_threshold_db"],
          "cfar_table_first_frame": th["cfar_table_first_frame"]}
    if "cfar_table_second_frame" in th:
        kw["cfar_table_second_frame"] = th["cfar_table_second_frame"]
    c.check(sec, sdk.set_detection_threshold(**kw) == OK, "SetDetectionThreshold", sdk.last_error)
    c.check(sec, sdk.set_detection_filters(**snap["filters"]) == OK,
            "SetDetectionFilters", sdk.last_error)
    c.check(sec, sdk.set_ptp_config(snap["ptp_profile"]) == OK, "SetPTPConfig", sdk.last_error)
    m, r = snap["mounting"], snap["rotation"]
    c.check(sec, sdk.set_mounting_position(m["offset_x_m"], m["offset_y_m"],
                                           m["offset_z_m"]) == OK,
            "SetMountingPosition", sdk.last_error)
    c.check(sec, sdk.set_rotation(r["yaw_deg"], r["pitch_deg"], r["roll_deg"]) == OK,
            "SetRotation", sdk.last_error)
    c.check(sec, sdk.set_sensor_position(snap["position"]) == OK,
            "SetSensorPosition", sdk.last_error)


def compare(snap_now, want, c, sec, label):
    for key in ("threshold", "filters", "ptp_profile", "mounting", "rotation", "position"):
        c.check(sec, snap_now[key] == want[key], f"{label}: {key} read back",
                snap_now[key] if snap_now[key] != want[key] else "")
    got = {k: snap_now["network"].get(k) for k in STREAM_KEYS}
    exp = {k: want["network"][k] for k in STREAM_KEYS}
    c.check(sec, got == exp, f"{label}: stream destinations read back",
            got if got != exp else "")


# ── sections ──

def section_mode_gate(sdk, c):
    sec = "M"
    mode = sdk.get_mode() or {}
    c.check(sec, mode.get("access_mode") == AccessMode.NORMAL, "starts in Normal",
            mode.get("access_mode_name"))
    c.check(sec, sdk.set_range_mode(1) == SESSION_REQUIRED, "SET refused in Normal",
            sdk.last_result and sdk.last_result.get("return_name"))
    c.check(sec, sdk.reset_all_settings() == SESSION_REQUIRED,
            "ResetAllSettings refused in Normal")
    c.check(sec, sdk.firmware_update_start("firmware", "m7_0", 4096, 0) is None
            and sdk.last_error == SESSION_REQUIRED, "FirmwareUpdateStart refused in Normal")
    rc = sdk.set_mode(AccessMode.NORMAL)
    c.check(sec, rc != OK, "SetMode(Normal) refused", sdk.last_result.get("return_name"))
    rc = sdk.set_mode(AccessMode.PROGRAMMING)
    c.check(sec, rc != OK, "SetMode Normal -> Programming refused",
            sdk.last_result.get("return_name"))
    c.check(sec, (sdk.get_mode() or {}).get("access_mode") == AccessMode.NORMAL,
            "still Normal after refused SetMode")
    c.check(sec, sdk.set_mode(AccessMode.DIAGNOSTIC) == OK, "SetMode Normal -> Diagnostic")
    mode = sdk.get_mode() or {}
    c.check(sec, mode.get("access_mode") == AccessMode.DIAGNOSTIC
            and mode.get("streams_halted") == 0, "Diagnostic, streams running", mode)
    c.check(sec, sdk.set_mode(AccessMode.DIAGNOSTIC) == ACCESS_DENIED,
            "SetMode to the same mode answers ACCESS_DENIED")


def section_set_sweep(sdk, ip, base, c):
    sec = "W"
    want = {k: (dict(v) if isinstance(v, dict) else v) for k, v in base.items()}
    want["network"] = dict(base["network"])
    want["network"].update(health_protocol="udp", health_port=UDP_TEST_PORT)
    th = dict(base["threshold"])
    th["snr_threshold_db"] = 6.5 if th["snr_threshold_db"] != 6.5 else 4.25
    th["cfar_table_first_frame"] = [(i % 9) + 1 for i in range(19)]
    want["threshold"] = th
    want["filters"] = {k: (not v) for k, v in base["filters"].items()}
    want["ptp_profile"] = 1 if base["ptp_profile"] != 1 else 0
    want["mounting"] = {"offset_x_m": 1.25, "offset_y_m": -0.5, "offset_z_m": 0.75}
    want["rotation"] = {"yaw_deg": -3.5, "pitch_deg": 1.0, "roll_deg": 0.5}
    want["position"] = 3 if base["position"] != 3 else 1

    write_settings(sdk, want, c, sec)
    c.check(sec, (sdk.last_result or {}).get("e2e_ok") is True, "SET response E2E valid")
    pending = snapshot(sdk)
    c.info(sec, "GET before restart: mounting=%s ptp_profile=%s health=%s:%s" % (
        pending["mounting"], pending["ptp_profile"],
        pending["network"].get("health_protocol"), pending["network"].get("health_port")))

    restart_and_reconnect(sdk, ip, c, sec)
    now = snapshot(sdk)
    compare(now, want, c, sec, "after Restart")
    rec = discover_one(ip) or {}
    c.check(sec, rec.get("health_protocol") == "udp"
            and rec.get("health_port") == UDP_TEST_PORT,
            "Discovery reports the moved health stream",
            f"{rec.get('health_protocol')}:{rec.get('health_port')}")
    c.check(sec, rec.get("offset_x_m") == 1.25 and rec.get("yaw_deg") == -3.5
            and rec.get("position_code") == want["position"],
            "Discovery reports mounting and position",
            {k: rec.get(k) for k in ("offset_x_m", "yaw_deg", "position_code")})
    n = count_udp(ip, UDP_TEST_PORT, EVENT_SHII, 2.0)
    c.check(sec, n >= 10, "SHII arrives on UDP after the move", f"{n} frames in 2 s")
    n = count_tcp(ip, base["network"]["detection_port"], EVENT_RDI, 2.0)
    c.check(sec, n >= 10, "RDI still on TCP", f"{n} frames in 2 s")
    ptp = sdk.get_ptp_config() or {}
    c.info(sec, f"PTP running profile={ptp.get('profile')} state={ptp.get('state')}")
    return want


def section_range_mode(sdk, ip, base, c):
    sec = "G"
    target = 1 if base["range_mode"] != 1 else 0
    c.check(sec, sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK, "enter Diagnostic")
    c.check(sec, sdk.set_range_mode(target) == OK, f"SetRangeMode({target})", sdk.last_error)
    restart_and_reconnect(sdk, ip, c, sec)
    c.check(sec, sdk.get_range_mode() == target, "range mode applied after Restart",
            sdk.get_range_mode())
    rec = discover_one(ip) or {}
    c.check(sec, rec.get("range_mode") == target, "Discovery reports the range mode",
            rec.get("range_mode_name"))
    n = count_tcp(ip, base["network"]["detection_port"], EVENT_RDI, 2.0)
    c.check(sec, n >= 10, "RDI flows in the new range mode", f"{n} frames in 2 s")


def section_defaults(sdk, ip, base, c):
    sec = "D"
    c.check(sec, sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK, "enter Diagnostic")
    c.check(sec, sdk.reset_config_defaults(["unknown_section"]) is None
            and sdk.last_error == INVALID_PARAM, "unknown key refused", sdk.last_error)
    range_before = sdk.get_range_mode()
    r = sdk.reset_config_defaults(["mounting"])
    c.check(sec, r is not None, "ResetConfigDefaults(['mounting'])", sdk.last_error)
    restart_and_reconnect(sdk, ip, c, sec)
    # Defaults: 06_Mounting_and_Position (offset 3.8 / 0.0 / 0.5 m, rotation 0).
    m, rot = sdk.get_mounting_position(), sdk.get_rotation()
    c.check(sec, m == {"offset_x_m": 3.8, "offset_y_m": 0.0, "offset_z_m": 0.5},
            "mounting offsets back to defaults", m)
    c.check(sec, rot == {"yaw_deg": 0.0, "pitch_deg": 0.0, "roll_deg": 0.0},
            "rotation back to defaults", rot)
    c.check(sec, sdk.get_sensor_position() == 0, "position code back to default",
            sdk.get_sensor_position())
    c.check(sec, sdk.get_detection_filters() != base["filters"],
            "detection section untouched (sweep filters kept)")
    c.check(sec, sdk.get_range_mode() == range_before,
            "operation section untouched (range mode kept)", sdk.get_range_mode())

    c.check(sec, sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK, "enter Diagnostic")
    restart_and_reconnect(sdk, ip, c, sec, via_reset_all=True)
    net = sdk.get_network_config() or {}
    c.check(sec, net.get("ip_address") == ip, "ResetAllSettings keeps the sensor address",
            net.get("ip_address"))
    after = snapshot(sdk)
    c.info(sec, "after ResetAllSettings: range=%s ptp=%s threshold.snr=%s "
                "destinations=%s" % (
                    after["range_mode"], after["ptp_profile"],
                    (after["threshold"] or {}).get("snr_threshold_db"),
                    {k: net.get(k) for k in STREAM_KEYS}))


def section_restore(sdk, ip, base, c):
    sec = "X"
    c.check(sec, sdk.enter_mode(AccessMode.DIAGNOSTIC) == OK, "enter Diagnostic")
    write_settings(sdk, base, c, sec)
    c.check(sec, sdk.set_range_mode(base["range_mode"]) == OK, "SetRangeMode (baseline)")
    restart_and_reconnect(sdk, ip, c, sec)
    now = snapshot(sdk)
    compare(now, base, c, sec, "restored")
    c.check(sec, now["range_mode"] == base["range_mode"], "restored: range mode",
            now["range_mode"])
    for name, ev in (("RDI", EVENT_RDI), ("SHII", EVENT_SHII)):
        port = base["network"]["detection_port" if ev == EVENT_RDI else "health_port"]
        n = count_tcp(ip, port, ev, 2.0)
        c.check(sec, n >= 10, f"{name} back on TCP {port}", f"{n} frames in 2 s")


def section_programming(sdk, ip, base, c):
    sec = "P"
    c.check(sec, sdk.enter_mode(AccessMode.PROGRAMMING) == OK, "enter Programming")
    mode = sdk.get_mode() or {}
    c.check(sec, mode.get("access_mode") == AccessMode.PROGRAMMING
            and mode.get("streams_halted") == 1, "Programming, streams halted", mode)
    time.sleep(0.5)
    n = count_tcp(ip, base["network"]["detection_port"], EVENT_RDI, 2.0)
    c.check(sec, n <= 0, "RDI halted in Programming", f"{n} frames in 2 s")
    c.check(sec, sdk.set_range_mode(base["range_mode"]) == SESSION_REQUIRED,
            "config SET refused in Programming", sdk.last_error)
    c.check(sec, sdk.set_mode(AccessMode.DIAGNOSTIC) == ACCESS_DENIED,
            "Programming -> Diagnostic refused")
    st = sdk.firmware_update_status() or {}
    c.check(sec, st.get("state") == "idle", "update session idle", st.get("state"))
    restart_and_reconnect(sdk, ip, c, sec)
    n = count_tcp(ip, base["network"]["detection_port"], EVENT_RDI, 2.0)
    c.check(sec, n >= 10, "RDI resumes after Restart", f"{n} frames in 2 s")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ip")
    ap.add_argument("--yes-mutate", action="store_true",
                    help="required: confirms that the sensor's settings may be rewritten")
    args = ap.parse_args()
    if not args.yes_mutate:
        print("Refusing to run without --yes-mutate (the test rewrites NVM settings).")
        return 2

    c = Checks()
    rec = discover_one(args.ip)
    if not c.check("-", rec is not None, "Discovery answers", args.ip):
        return 1
    print(f"Sensor {args.ip}: sw={rec.get('software_version')} fw={rec.get('fw_hash')} "
          f"bl={rec.get('bootloader_hash')}")
    sdk = AfiSdk()
    if not c.check("-", sdk.init(AfiSdkConfig(sensor_ip=args.ip)) == OK, "Config API connect"):
        return 1
    base = snapshot(sdk)
    print(f"Baseline: {base}")

    try:
        section_mode_gate(sdk, c)
        section_set_sweep(sdk, args.ip, base, c)
        section_range_mode(sdk, args.ip, base, c)
        section_defaults(sdk, args.ip, base, c)
    finally:
        section_restore(sdk, args.ip, base, c)
    section_programming(sdk, args.ip, base, c)
    sdk.close()

    total = c.passed + len(c.failed)
    print(f"\n--- {args.ip}: {c.passed}/{total} checks passed ---")
    for f in c.failed:
        print(f"  FAILED: {f}")
    return 0 if not c.failed else 1


if __name__ == "__main__":
    sys.exit(main())
