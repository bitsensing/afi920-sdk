"""
Firmware update sequence for one AFI920 radar (interface v1.1.0).

07_Firmware_Update and 11_Access_Mode fix the order, so it is protocol
knowledge rather than caller policy:

1. Enter the programming access mode (Normal -> Diagnostic -> Programming).
   Entering it halts the data streams until the radar reboots.
2. Bootloader domain (only when an image is given): one component, written in
   place with a backup copy; it has no A/B face and no slot switch.
3. Firmware domain: m7_0, m7_1, m7_2 and a53 are ONE slot face. All four are
   written to the inactive face (each START names it as target_slot) while
   the radar keeps running the active one. A subset is refused: a face holds
   a valid mark from an earlier write, so switching to a partly rewritten
   face boots a mix of old and new images.
4. Only when every commit reached idle: one FirmwareSwitchSlot (0x0085), wait
   for idle, and confirm on Boot-Info that the written face is active.
5. FirmwareUpdateReboot (0x0086), sent only while the session is idle so the
   Boot-Info write is not cut off.

Per component: START answers max_chunk_size (4096) and target_slot; a chunk
may be accepted only in part and received_bytes is the next offset; 200
chunks without progress abort; COMMIT is followed by STATUS polling until
idle (budget max(120 s, size / 50 KiB/s)); a failure aborts the session. The
radar drops a session after 10 s without an update request, so nothing here
waits on the caller between steps.

Bundles, decryption and version selection belong to the caller; this module
takes component images as bytes.

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import time
import zlib
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

from afi_sdk._sdk import (
    AccessMode, AfiSdk, OK, DISCONNECTED, TIMEOUT,
)

FIRMWARE_COMPONENTS = ("m7_0", "m7_1", "m7_2", "a53")
BOOTLOADER_COMPONENT = "bootloader"
DEFAULT_MAX_CHUNK_SIZE = 4096
MAX_STALLED_CHUNKS = 200
STATUS_POLL_S = 0.2
SWITCH_IDLE_TIMEOUT_S = 60.0

#: Step names of results that are not a component transfer.
STEP_MODE = "enter_programming_mode"
STEP_BOOT_INFO = "get_boot_info"
STEP_SWITCH = "switch_slot"
STEP_REBOOT = "reboot"

#: (step, state, percent, received_bytes, total_bytes)
ProgressCallback = Callable[[str, str, int, int, int], None]


@dataclass
class StepResult:
    """Outcome of one component transfer or sequence step."""
    step: str
    ok: bool
    error: Optional[str] = None
    target_slot: Optional[str] = None


@dataclass
class UpdateResult:
    ok: bool
    steps: List[StepResult] = field(default_factory=list)
    #: Face the firmware was written to ("A"/"B"); None for bootloader only.
    target_face: Optional[str] = None
    rebooted: bool = False

    @property
    def error(self) -> Optional[str]:
        failed = [s for s in self.steps if not s.ok]
        return f"{failed[-1].step}: {failed[-1].error}" if failed else None


def image_crc32(image: bytes) -> int:
    """IEEE 802.3 CRC-32 of an image, the value FirmwareUpdateStart checks."""
    return zlib.crc32(image) & 0xFFFFFFFF


def commit_timeout_s(size: int) -> float:
    """Wait budget after COMMIT: max(120 s, size at 50 KiB/s)."""
    return max(120.0, size / (50 * 1024))


def _sdk_error(sdk: AfiSdk) -> str:
    r = sdk.last_result or {}
    msg = r.get("error_message")
    return f"{r.get('return_name', sdk.last_error)}" + (f": {msg}" if msg else "")


def wait_idle(sdk: AfiSdk, timeout_s: float, step: str = "",
              on_progress: Optional[ProgressCallback] = None,
              total: int = 0) -> Optional[str]:
    """Poll FirmwareUpdateStatus until idle. Returns None, or the reason.

    Every command that starts medium work (COMMIT, 0x0085) is followed by
    this: the radar holds one session and refuses the next START until idle.
    """
    deadline = time.monotonic() + timeout_s
    state = "unknown"
    while time.monotonic() < deadline:
        st = sdk.firmware_update_status()
        if st is None:
            return f"FirmwareUpdateStatus failed ({_sdk_error(sdk)})"
        state = st.get("state", "unknown")
        if state == "idle":
            return None
        if state == "error":
            reason = st.get("error_message") or "the session ended in the error state"
            return f"radar error: {reason}"
        if on_progress:
            on_progress(step, state, int(st.get("progress_pct", 0)), 0, total)
        time.sleep(STATUS_POLL_S)
    return f"still {state!r} after {timeout_s:.0f} s"


def upload_component(sdk: AfiSdk, update_type: str, component: str,
                     image: bytes, crc32: Optional[int] = None,
                     on_progress: Optional[ProgressCallback] = None) -> StepResult:
    """START, CHUNK until received_bytes == size, COMMIT, wait for idle.

    Any failure after START aborts the session (0x0084) so the next attempt
    starts clean.
    """
    size = len(image)
    crc = image_crc32(image) if crc32 is None else crc32
    start = sdk.firmware_update_start(update_type, component, size, crc)
    if start is None:
        return StepResult(component, False, f"START failed ({_sdk_error(sdk)})")
    chunk_size = int(start.get("max_chunk_size") or DEFAULT_MAX_CHUNK_SIZE)
    target = start.get("target_slot")

    def fail(reason: str) -> StepResult:
        sdk.firmware_update_abort()
        return StepResult(component, False, reason, target)

    sent = 0
    stalled = 0
    while sent < size:
        answer = sdk.firmware_data_chunk(image[sent:sent + chunk_size])
        if answer is None:
            return fail(f"CHUNK failed at offset {sent} ({_sdk_error(sdk)})")
        received = int(answer.get("received_bytes", sent))
        if received <= sent:
            stalled += 1
            if stalled > MAX_STALLED_CHUNKS:
                return fail(f"no progress at offset {sent} after "
                            f"{MAX_STALLED_CHUNKS} chunks")
        else:
            stalled = 0
        if received > size:
            return fail(f"radar reports {received} B received of {size} B")
        sent = received
        if on_progress:
            on_progress(component, "transferring", sent * 100 // size if size else 100,
                        sent, size)

    if sdk.firmware_update_commit() != OK:
        return fail(f"COMMIT failed ({_sdk_error(sdk)})")
    reason = wait_idle(sdk, commit_timeout_s(size), component, on_progress, size)
    if reason:
        return fail(f"after COMMIT: {reason}")
    return StepResult(component, True, None, target)


def _other_face(face: Optional[str]) -> Optional[str]:
    return {"A": "B", "B": "A"}.get(face or "")


def _active_face(sdk: AfiSdk) -> Optional[str]:
    info = sdk.get_boot_info()
    return info.get("fw_update_active") if info else None


def update(sdk: AfiSdk, firmware: Optional[Dict[str, bytes]] = None,
           bootloader: Optional[bytes] = None, reboot: bool = True,
           on_progress: Optional[ProgressCallback] = None) -> UpdateResult:
    """Run the update sequence (module docstring) on a connected AfiSdk.

    ``firmware`` maps m7_0/m7_1/m7_2/a53 to images and must hold all four or
    be None; ``bootloader`` is the bootloader image or None. With
    ``reboot`` the radar restarts on the written face; confirm it with
    :func:`wait_for_restart` and :func:`verify`.
    """
    result = UpdateResult(ok=False)
    steps = result.steps
    if firmware is not None and set(firmware) != set(FIRMWARE_COMPONENTS):
        steps.append(StepResult(
            "firmware", False,
            f"needs exactly {', '.join(FIRMWARE_COMPONENTS)}; got "
            f"{', '.join(sorted(firmware))} (nothing was sent)"))
        return result
    if firmware is None and bootloader is None:
        steps.append(StepResult("firmware", False, "no image given"))
        return result

    if sdk.enter_mode(AccessMode.PROGRAMMING) != OK:
        steps.append(StepResult(STEP_MODE, False, _sdk_error(sdk)))
        return result

    if bootloader is not None:
        step = upload_component(sdk, "bootloader", BOOTLOADER_COMPONENT,
                                bootloader, on_progress=on_progress)
        steps.append(step)
        if not step.ok:
            return result

    if firmware is not None:
        origin = _active_face(sdk)
        target = _other_face(origin)
        if target is None:
            steps.append(StepResult(STEP_BOOT_INFO, False,
                                    f"no active face in Boot-Info ({origin!r}); "
                                    "no firmware was sent"))
            return result
        result.target_face = target
        for i, name in enumerate(FIRMWARE_COMPONENTS):
            step = upload_component(sdk, "firmware", name, firmware[name],
                                    on_progress=on_progress)
            steps.append(step)
            if not step.ok:
                return result
            if step.target_slot != target:
                steps[-1] = StepResult(name, False,
                                       f"target_slot {step.target_slot} is not the "
                                       f"inactive face {target}", step.target_slot)
                return result
            # Firmware older than the 07.A rule moved the marker at every
            # commit; put it back while components remain so each one lands
            # on the same face (interface reference upload_firmware_set).
            if i < len(FIRMWARE_COMPONENTS) - 1 and _active_face(sdk) != origin:
                reason = _switch(sdk, origin)
                if reason:
                    steps.append(StepResult(STEP_SWITCH, False, reason, target))
                    return result

        if _active_face(sdk) != target:
            reason = _switch(sdk, target)
            if reason:
                steps.append(StepResult(STEP_SWITCH, False, reason, target))
                return result
        steps.append(StepResult(STEP_SWITCH, True, None, target))

    if reboot:
        # Idle was confirmed after the last COMMIT / switch. The radar answers
        # before it resets; a connection lost before the answer also means
        # the reboot was accepted.
        err = sdk.firmware_update_reboot()
        if err not in (OK, DISCONNECTED, TIMEOUT):
            steps.append(StepResult(STEP_REBOOT, False, _sdk_error(sdk)))
            return result
        steps.append(StepResult(STEP_REBOOT, True))
        result.rebooted = True
    result.ok = True
    return result


def _switch(sdk: AfiSdk, want: str) -> Optional[str]:
    """0x0085, wait for idle, confirm *want* is active. None on success."""
    if sdk.firmware_switch_slot("firmware") is None:
        return f"FirmwareSwitchSlot failed ({_sdk_error(sdk)})"
    reason = wait_idle(sdk, SWITCH_IDLE_TIMEOUT_S, STEP_SWITCH)
    if reason:
        return f"switch did not settle: {reason}"
    after = _active_face(sdk)
    if after != want:
        return f"active face is {after} after the switch (expected {want})"
    return None


def radar_uptime(ip: str, timeout_s: float = 1.5, discover=None) -> Optional[int]:
    """Discovery scan_status.uptime_seconds of one radar (the value
    wait_for_restart compares), or None when it does not answer."""
    if discover is None:
        from afi_sdk.someip import discover as _discover
        discover = _discover
    for rec in discover(timeout_s=timeout_s, broadcast_ip=ip):
        if rec.get("_source_ip") == ip:
            uptime = (rec.get("scan_status") or {}).get("uptime_seconds")
            if isinstance(uptime, int):
                return uptime
    return None


def wait_for_restart(ip: str, uptime_before: int, timeout_s: float = 120.0,
                     interval_s: float = 0.5, discover=None) -> Optional[dict]:
    """Wait until Discovery shows the radar back with a lower uptime.

    Waiting for silence alone is unreliable: the reset can fit between two
    probes. Returns the Discovery record, or None after timeout_s.
    """
    if discover is None:
        from afi_sdk.someip import discover as _discover
        discover = _discover
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for rec in discover(timeout_s=interval_s, broadcast_ip=ip):
            if rec.get("_source_ip") != ip:
                continue
            uptime = (rec.get("scan_status") or {}).get("uptime_seconds")
            if isinstance(uptime, int) and uptime < uptime_before:
                return rec
    return None


def verify(sdk: AfiSdk, target_face: Optional[str],
           fw_hash: Optional[str] = None,
           bootloader_hash: Optional[str] = None) -> List[str]:
    """After the restart: Boot-Info and device hashes. Returns the problems.

    Hashes compare on their first 7+ characters with any "-dirty" suffix
    removed (07_Firmware_Update). A boot_info_status other than "ok" is
    reported; only a FirmwareSwitchSlot rewrites a backup_used record.
    """
    problems = []
    if target_face:
        boot = sdk.get_boot_info()
        if boot is None:
            problems.append(f"GetBootInfo failed ({_sdk_error(sdk)})")
        else:
            if boot.get("fw_update_active") != target_face:
                problems.append(f"active face {boot.get('fw_update_active')} "
                                f"(expected {target_face})")
            if not boot.get(f"fw_update_valid_{target_face.lower()}"):
                problems.append(f"face {target_face} is not marked valid")
            if boot.get("boot_info_status") not in (None, "ok"):
                problems.append(f"boot_info_status {boot.get('boot_info_status')}")
    if fw_hash or bootloader_hash:
        dev = sdk.get_device_info()
        if dev is None:
            problems.append(f"GetDeviceInfo failed ({_sdk_error(sdk)})")
        else:
            for key, want in (("fw_hash", fw_hash), ("bootloader_hash", bootloader_hash)):
                if want and not hashes_match(dev.get(key, ""), want):
                    problems.append(f"{key} {dev.get(key)} (expected {want})")
    return problems


def hashes_match(reported: str, expected: str) -> bool:
    """Git short hashes match on a common prefix of 7+ characters."""
    a = (reported or "").split("-dirty")[0].lower()
    b = (expected or "").split("-dirty")[0].lower()
    n = min(len(a), len(b))
    return n >= 7 and a[:n] == b[:n] and "unknown" not in (a, b)
