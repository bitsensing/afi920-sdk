"""
Live firmware-update verification against one AFI920 sensor (interface
v1.1.0, firmware v3.1.1). DESTRUCTIVE: it writes flash. It writes the
inactive firmware face, optionally the bootloader in place, switches faces
and reboots several times. The sensor ends on the written face.

Sections:
  [N] Refusals        — chunk above max_chunk_size, CRC mismatch at COMMIT,
                        ABORT mid-transfer, the 10 s inactivity timeout; the
                        active face never changes
  [U] Update          — afi_sdk.firmware.update(): all four components on the
                        inactive face (and the bootloader when given), one
                        switch, reboot; Boot-Info, device and image hashes,
                        streams after the reboot
  [S] Switch back     — FirmwareSwitchSlot to the previous face and reboot,
                        then back to the written face and reboot

The images are plain component files (decrypted bundle contents):
  <dir>/m7_0.bin, m7_1.bin, m7_2.bin, a53.bin [, bootloader.bin]

Usage:
    python test/test_live_firmware.py 192.168.10.150 --images DIR \\
        --fw-hash f6c38c30 [--bootloader --bl-hash 651df59e] --yes-flash

Exit code 0 = every hard check passed.
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from afi_sdk import (  # noqa: E402
    AccessMode, AfiSdk, AfiSdkConfig, OK, INVALID_PARAM,
)
from afi_sdk import firmware as fw  # noqa: E402
from test_live_write import Checks, count_tcp, discover_one, EVENT_RDI  # noqa: E402


def wait_state(sdk, want, timeout_s):
    """Poll FirmwareUpdateStatus until ``state`` is one of ``want``."""
    deadline = time.monotonic() + timeout_s
    st = {}
    while time.monotonic() < deadline:
        st = sdk.firmware_update_status() or {}
        if st.get("state") in want:
            return st
        time.sleep(0.2)
    return st


def send_image(sdk, image, chunk_size, stop_at=None):
    """CHUNK until received_bytes reaches len(image) (or ``stop_at``)."""
    offset = 0
    end = len(image) if stop_at is None else stop_at
    stalls = 0
    while offset < end:
        ans = sdk.firmware_data_chunk(image[offset:offset + chunk_size])
        if ans is None:
            return offset, sdk.last_error
        nxt = int(ans.get("received_bytes", offset))
        stalls = stalls + 1 if nxt == offset else 0
        if stalls > fw.MAX_STALLED_CHUNKS:
            return offset, "stalled"
        offset = nxt
    return offset, OK


def reboot_and_reconnect(sdk, ip, c, sec, send):
    before = fw.radar_uptime(ip) or 0
    c.check(sec, send() == OK, "reboot request accepted",
            (sdk.last_result or {}).get("return_name"))
    t0 = time.monotonic()
    rec = fw.wait_for_restart(ip, before, timeout_s=180)
    c.check(sec, rec is not None, "sensor rebooted", f"{time.monotonic() - t0:.1f}s")
    c.check(sec, sdk.reconnect(timeout_s=60) == OK, "Config API reconnect")
    return rec or {}


def section_refusals(sdk, ip, imgs, c):
    sec = "N"
    boot0 = sdk.get_boot_info() or {}
    c.check(sec, sdk.enter_mode(AccessMode.PROGRAMMING) == OK, "enter Programming")
    img = imgs["m7_0"]

    start = sdk.firmware_update_start("firmware", "m7_0", len(img), fw.image_crc32(img))
    c.check(sec, start is not None, "START answers", start)
    if start is None:
        return
    c.check(sec, start.get("max_chunk_size") == 4096, "max_chunk_size 4096",
            start.get("max_chunk_size"))
    c.check(sec, start.get("target_slot") not in (None, boot0.get("fw_update_active")),
            "target_slot is the inactive face",
            f"target={start.get('target_slot')} active={boot0.get('fw_update_active')}")
    ans = sdk.firmware_data_chunk(img[:4097])
    c.check(sec, ans is None and sdk.last_error == INVALID_PARAM,
            "chunk above max_chunk_size refused", sdk.last_error)
    got, err = send_image(sdk, img, 4096, stop_at=3 * 4096)
    c.check(sec, err == OK and got >= 3 * 4096, "partial transfer", f"{got} B")
    c.check(sec, sdk.firmware_update_abort() == OK, "ABORT mid-transfer")
    st = wait_state(sdk, ("idle",), 10)
    c.check(sec, st.get("state") == "idle", "session idle after ABORT", st.get("state"))

    start = sdk.firmware_update_start("firmware", "m7_0", len(img),
                                      fw.image_crc32(img) ^ 0x1)
    c.check(sec, start is not None, "START with a wrong crc32 accepted")
    got, err = send_image(sdk, img, 4096)
    c.check(sec, err == OK and got == len(img), "whole image sent", f"{got} B")
    rc = sdk.firmware_update_commit()
    commit_answer = (sdk.last_result or {}).get("return_name")
    st = wait_state(sdk, ("error", "idle"), 30)
    c.check(sec, rc != OK or st.get("state") == "error", "CRC mismatch refused",
            f"COMMIT {commit_answer}, then {st.get('state')} {st.get('error_message')}")
    sdk.firmware_update_abort()
    st = wait_state(sdk, ("idle",), 10)
    c.check(sec, st.get("state") == "idle", "idle after ABORT of the failed session",
            st.get("state"))

    start = sdk.firmware_update_start("firmware", "m7_0", len(img), fw.image_crc32(img))
    c.check(sec, start is not None, "START for the inactivity test")
    send_image(sdk, img, 4096, stop_at=4096)
    time.sleep(12)
    st = sdk.firmware_update_status() or {}
    c.check(sec, st.get("state") in ("idle", "error"),
            "session ended after 10 s without progress", st)
    ans = sdk.firmware_data_chunk(img[4096:8192])
    c.check(sec, ans is None, "chunk after the timeout refused", sdk.last_error)
    sdk.firmware_update_abort()
    wait_state(sdk, ("idle",), 10)

    boot = sdk.get_boot_info() or {}
    c.check(sec, boot.get("fw_update_active") == boot0.get("fw_update_active"),
            "active face unchanged", boot)


def section_update(sdk, ip, imgs, bl, args, c):
    sec = "U"
    boot0 = sdk.get_boot_info() or {}
    last = {"step": None}

    def progress(step, state, pct, received, total):
        if step != last["step"]:
            last["step"] = step
            print(f"    {step}: {state} {total} B", flush=True)

    before = fw.radar_uptime(ip) or 0
    t0 = time.monotonic()
    res = fw.update(sdk, firmware=imgs, bootloader=bl, on_progress=progress)
    c.check(sec, res.ok, "update() completed", res.error or
            f"{time.monotonic() - t0:.1f}s, face {res.target_face}")
    if not res.ok:
        return None
    c.check(sec, res.target_face != boot0.get("fw_update_active"),
            "written face was the inactive one", res.target_face)
    for s in res.steps:
        c.info(sec, f"step {s.step}: ok={s.ok} slot={s.target_slot}")
    rec = fw.wait_for_restart(ip, before, timeout_s=180)
    c.check(sec, rec is not None, "sensor rebooted onto the new face")
    c.check(sec, sdk.reconnect(timeout_s=60) == OK, "Config API reconnect")
    problems = fw.verify(sdk, res.target_face, fw_hash=args.fw_hash,
                         bootloader_hash=args.bl_hash)
    c.check(sec, not problems, "Boot-Info and hashes", problems or "")
    rec = discover_one(ip) or {}
    c.check(sec, fw.hashes_match(rec.get("fw_hash", ""), args.fw_hash),
            "Discovery fw_hash", rec.get("fw_hash"))
    n = count_tcp(ip, 30509, EVENT_RDI, 2.0)
    c.check(sec, n >= 10, "RDI flows on the new firmware", f"{n} frames in 2 s")
    return res.target_face


def section_switch_back(sdk, ip, new_face, old_hash, args, c):
    sec = "S"
    old_face = "A" if new_face == "B" else "B"
    for face, want_hash in ((old_face, old_hash), (new_face, args.fw_hash)):
        c.check(sec, sdk.enter_mode(AccessMode.PROGRAMMING) == OK, "enter Programming")
        ans = sdk.firmware_switch_slot("firmware")
        c.check(sec, ans is not None, f"FirmwareSwitchSlot to {face}", ans)
        st = wait_state(sdk, ("idle",), fw.SWITCH_IDLE_TIMEOUT_S)
        c.check(sec, st.get("state") == "idle", "switch settled", st.get("state"))
        reboot_and_reconnect(sdk, ip, c, sec, sdk.firmware_update_reboot)
        problems = fw.verify(sdk, face, fw_hash=want_hash)
        c.check(sec, not problems, f"running face {face} ({want_hash})", problems or "")
        n = count_tcp(ip, 30509, EVENT_RDI, 2.0)
        c.check(sec, n >= 10, "RDI flows", f"{n} frames in 2 s")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ip")
    ap.add_argument("--images", required=True, type=Path)
    ap.add_argument("--fw-hash", required=True, help="git hash the images report")
    ap.add_argument("--bootloader", action="store_true",
                    help="also write <images>/bootloader.bin (no A/B face)")
    ap.add_argument("--bl-hash", default=None)
    ap.add_argument("--skip-refusals", action="store_true")
    ap.add_argument("--skip-switch-back", action="store_true")
    ap.add_argument("--yes-flash", action="store_true",
                    help="required: confirms that the sensor's flash may be written")
    args = ap.parse_args()
    if not args.yes_flash:
        print("Refusing to run without --yes-flash.")
        return 2

    imgs = {name: (args.images / f"{name}.bin").read_bytes()
            for name in fw.FIRMWARE_COMPONENTS}
    bl = (args.images / "bootloader.bin").read_bytes() if args.bootloader else None

    c = Checks()
    rec = discover_one(args.ip)
    if not c.check("-", rec is not None, "Discovery answers", args.ip):
        return 1
    old_hash = rec.get("fw_hash")
    print(f"Sensor {args.ip}: fw={old_hash} bl={rec.get('bootloader_hash')} -> "
          f"fw={args.fw_hash}" + (f" bl={args.bl_hash}" if bl else ""))
    sdk = AfiSdk()
    if not c.check("-", sdk.init(AfiSdkConfig(sensor_ip=args.ip)) == OK, "Config API connect"):
        return 1

    if not args.skip_refusals:
        section_refusals(sdk, args.ip, imgs, c)
        reboot_and_reconnect(sdk, args.ip, c, "N", sdk.restart)
    face = section_update(sdk, args.ip, imgs, bl, args, c)
    if face and not args.skip_switch_back:
        section_switch_back(sdk, args.ip, face, old_hash, args, c)
    sdk.close()

    total = c.passed + len(c.failed)
    print(f"\n--- {args.ip}: {c.passed}/{total} checks passed ---")
    for f in c.failed:
        print(f"  FAILED: {f}")
    return 0 if not c.failed else 1


if __name__ == "__main__":
    sys.exit(main())
