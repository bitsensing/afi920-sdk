"""
Firmware update sequence (afi_sdk.firmware) against a scripted radar.

FakeUpdateRadar follows 07_Firmware_Update and 11_Access_Mode: methods
0x0080-0x0086 need the programming mode, START names the inactive face, a
chunk is accepted only in part (256 B pages, sometimes 0 B) and above 4096 B
refused, COMMIT checks the CRC-32 of what was received, STATUS reaches idle
only after a few polls, and 0x0085 moves the active face.

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import os
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))

import afi_sdk.firmware as fw  # noqa: E402
from afi_sdk import AfiSdk  # noqa: E402

fw.STATUS_POLL_S = 0.0   # the fake needs no wall-clock time

SUCCESS, INVALID_PARAM, SESSION_REQUIRED, ACCESS_DENIED, INVALID_STATE = 0, 1, 0x0C, 0x0D, 0x0E


class FakeUpdateRadar:
    def __init__(self, active="A", legacy_commit_moves_marker=False,
                 corrupt_component=None, fail_chunk_at=None):
        self.mode = 0
        self.active = active
        self.valid = {"A": True, "B": True}
        self.faces = {"A": {}, "B": {}}
        self.bootloader = None
        self.session = None
        self.state = "idle"
        self.busy_polls = 0
        self.pending = None
        self.legacy = legacy_commit_moves_marker
        self.corrupt = corrupt_component
        self.fail_chunk_at = fail_chunk_at
        self.log = []
        self.chunks = 0
        self.last_result = None

    def _reply(self, rc, data=None):
        self.last_result = {"return_code": rc, "return_name": str(rc),
                            "error_message": (data or {}).get("error_message"),
                            "data": data}
        return rc, data

    def request(self, method, payload=None):
        payload = payload or {}
        self.log.append(method)
        if method == 0x00E0:
            return self._reply(SUCCESS, {"access_mode": self.mode})
        if method == 0x00E1:
            want = payload["access_mode"]
            if want == self.mode or self.mode == 2:
                return self._reply(ACCESS_DENIED, {})
            if want == 2 and self.mode == 0:
                return self._reply(SESSION_REQUIRED, {})
            self.mode = want
            return self._reply(SUCCESS, {"access_mode": want})
        if method == 0x0087:
            return self._reply(SUCCESS, {"fw_update_active": self.active,
                                         "fw_update_valid_a": self.valid["A"],
                                         "fw_update_valid_b": self.valid["B"],
                                         "boot_info_status": "ok"})
        if method == 0x0083:
            if self.busy_polls:
                self.busy_polls -= 1
                if self.busy_polls == 0:
                    if self.pending == "commit":
                        self._finish_commit()
                    else:
                        self.state = "idle"
                    self.pending = None
            return self._reply(SUCCESS, {"state": self.state, "progress_pct": 50,
                                         "error_message": self.session and self.session.get("err")})
        if self.mode != 2:
            return self._reply(SESSION_REQUIRED, None)
        if method == 0x0080:
            if self.state != "idle":
                return self._reply(INVALID_STATE, {"state": self.state})
            crc = payload.get("crc32")
            assert isinstance(crc, str) and crc.startswith("0x"), crc
            target = ("B" if self.active == "A" else "A") \
                if payload["update_type"] == "firmware" else None
            self.session = {"type": payload["update_type"], "comp": payload["component"],
                            "size": payload["total_size"], "crc": int(crc, 16),
                            "data": bytearray(), "target": target}
            self.state = "transferring"
            return self._reply(SUCCESS, {"max_chunk_size": 4096, "target_slot": target,
                                         "state": "transferring"})
        if method == 0x0082:
            s = self.session
            if s is None or len(s["data"]) != s["size"]:
                return self._reply(INVALID_STATE, {"error_message": "incomplete transfer"})
            data = bytes(s["data"])
            if self.corrupt == s["comp"]:
                data = data[:-1] + bytes([data[-1] ^ 1])
            if zlib.crc32(data) & 0xFFFFFFFF != s["crc"]:
                self.state = "error"
                s["err"] = "crc mismatch"
                return self._reply(SUCCESS, {"state": "verifying"})
            self.state = "writing"
            self.busy_polls = 3
            self.pending = "commit"
            return self._reply(SUCCESS, {"state": "writing"})
        if method == 0x0084:
            self.session = None
            self.state = "idle"
            return self._reply(SUCCESS, {"state": "idle"})
        if method == 0x0085:
            if self.state != "idle":
                return self._reply(INVALID_STATE, {})
            before = self.active
            self.active = "B" if self.active == "A" else "A"
            self.state = "writing"
            self.busy_polls = 2
            self.pending = "switch"
            return self._reply(SUCCESS, {"previous_active": before, "new_active": self.active})
        if method == 0x0086:
            assert self.state == "idle", "reboot while the session is busy"
            self.log.append("REBOOT")
            return self._reply(SUCCESS, {})
        return self._reply(5, None)

    def request_raw(self, method, body):
        self.log.append(method)
        self.chunks += 1
        if self.mode != 2:
            return self._reply(SESSION_REQUIRED, None)
        if len(body) > 4096:
            return self._reply(INVALID_PARAM, {"error_message": "chunk too large"})
        s = self.session
        if self.fail_chunk_at is not None and len(s["data"]) >= self.fail_chunk_at:
            return self._reply(INVALID_STATE, {"error_message": "medium busy"})
        # erase lag: every third chunk accepts 0 B, the rest at most 1000 B
        accept = 0 if self.chunks % 3 == 0 else min(len(body), 1000)
        s["data"] += body[:accept]
        return self._reply(SUCCESS, {"received_bytes": len(s["data"]),
                                     "total_bytes": s["size"], "state": "transferring"})

    def _finish_commit(self):
        s = self.session
        if s["type"] == "firmware":
            self.faces[s["target"]][s["comp"]] = bytes(s["data"])
            self.valid[s["target"]] = True
            if self.legacy:
                self.active = s["target"]
        else:
            self.bootloader = bytes(s["data"])
        self.session = None
        self.state = "idle"


def sdk_for(radar):
    sdk = AfiSdk()
    sdk._client = radar
    return sdk


def images(size=10000):
    return {name: os.urandom(size + i) for i, name in enumerate(fw.FIRMWARE_COMPONENTS)}


def test_full_update_writes_one_face_switches_once_and_reboots():
    radar = FakeUpdateRadar(active="A")
    imgs = images()
    boot = os.urandom(3000)
    res = fw.update(sdk_for(radar), firmware=imgs, bootloader=boot)
    assert res.ok, res.error
    assert res.target_face == "B" and res.rebooted
    assert radar.faces["B"] == imgs
    assert radar.faces["A"] == {}
    assert radar.bootloader == boot
    assert radar.active == "B"
    assert radar.log.count(0x0085) == 1
    assert radar.log[-1] == "REBOOT"
    # mode walked Normal -> Diagnostic -> Programming before any update call
    first_update = min(radar.log.index(m) for m in (0x0080,))
    assert radar.log[:first_update].count(0x00E1) == 2


def test_subset_is_refused_before_anything_is_sent():
    radar = FakeUpdateRadar()
    imgs = images()
    del imgs["m7_2"]
    res = fw.update(sdk_for(radar), firmware=imgs)
    assert not res.ok and "nothing was sent" in res.error
    assert radar.log == []


def test_crc_mismatch_aborts_without_switching():
    radar = FakeUpdateRadar(corrupt_component="m7_1")
    res = fw.update(sdk_for(radar), firmware=images())
    assert not res.ok and "crc mismatch" in res.error
    assert 0x0084 in radar.log and 0x0085 not in radar.log and "REBOOT" not in radar.log
    assert radar.active == "A"


def test_chunk_refusal_aborts():
    radar = FakeUpdateRadar(fail_chunk_at=5000)
    res = fw.update(sdk_for(radar), firmware=images())
    assert not res.ok and "CHUNK failed" in res.error
    assert radar.log[-1] == 0x0084


def test_legacy_marker_move_is_put_back_between_components():
    radar = FakeUpdateRadar(active="A", legacy_commit_moves_marker=True)
    imgs = images()
    res = fw.update(sdk_for(radar), firmware=imgs, reboot=False)
    assert res.ok, res.error
    assert radar.faces["B"] == imgs and radar.active == "B"
    assert not res.rebooted and "REBOOT" not in radar.log


def test_bootloader_only_has_no_switch():
    radar = FakeUpdateRadar()
    res = fw.update(sdk_for(radar), bootloader=os.urandom(5000), reboot=False)
    assert res.ok and res.target_face is None
    assert 0x0085 not in radar.log


def test_hash_match_rules():
    assert fw.hashes_match("f8c75c6f", "f8c75c6")
    assert fw.hashes_match("f8c75c6f-dirty", "f8c75c6f")
    assert not fw.hashes_match("f8c75c", "f8c75c6f")      # under 7 characters
    assert not fw.hashes_match("unknown", "unknown")
    assert not fw.hashes_match("f8c75c6f", "f8c75c70")


def test_verify_reports_wrong_face_and_hash():
    radar = FakeUpdateRadar(active="A")

    class Dev(FakeUpdateRadar):
        def request(self, method, payload=None):
            if method == 0x0010:
                return self._reply(0, {"fw_hash": "deadbeef", "bootloader_hash": "651df59e"})
            return super().request(method, payload)

    radar = Dev(active="A")
    problems = fw.verify(sdk_for(radar), "B", fw_hash="f8c75c6f", bootloader_hash="651df59e")
    assert any("active face A" in p for p in problems)
    assert any("fw_hash deadbeef" in p for p in problems)
    assert not any("bootloader_hash" in p for p in problems)


def test_wait_for_restart_needs_a_lower_uptime():
    calls = iter([[{"_source_ip": "10.0.0.1", "scan_status": {"uptime_seconds": 900}}],
                  [],
                  [{"_source_ip": "10.0.0.1", "scan_status": {"uptime_seconds": 3}}]])
    rec = fw.wait_for_restart("10.0.0.1", 900, timeout_s=5,
                              discover=lambda **kw: next(calls, []))
    assert rec["scan_status"]["uptime_seconds"] == 3


def test_radar_uptime_reads_discovery():
    recs = [{"_source_ip": "10.0.0.2", "scan_status": {"uptime_seconds": 5}},
            {"_source_ip": "10.0.0.1", "scan_status": {"uptime_seconds": 77}}]
    assert fw.radar_uptime("10.0.0.1", discover=lambda **kw: recs) == 77
    assert fw.radar_uptime("10.0.0.3", discover=lambda **kw: recs) is None
