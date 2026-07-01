"""
AFI920 software update (bootloader / driver / firmware flashing).

Wraps Config API methods 0x0080-0x0087 (SOME/IP) to flash a sensor from a
software bundle. Mirrors the reference updater but is callback-driven and
manages its own connection per sensor, so a caller can drive many sensors in
parallel.

Bundle layout (two-level manifest)::

    <bundle>.tar                       (plaintext outer tar)
    |-- software_manifest.json         packages: [{update_type, filename}, ...]
    |-- bootloader.tar.gz.enc          per-domain package (AES-256-CBC encrypted)
    |-- driver.tar.gz.enc
    `-- firmware.tar.gz.enc
    each *.tar.gz.enc  -> decrypt -> *.tar.gz -> manifest.json + component .bin

The per-domain manifest.json carries ``git_version`` (used to decide whether the
domain needs flashing, by comparing against the sensor's installed hash from
GetDeviceInfo) and one entry per component with its ``crc32`` (transfer check).

Decryption needs the ``cryptography`` package (``pip install afi_sdk[update]``)
and the bundle password (never bundled with the SDK; pass at runtime).

Copyright (c) 2026 bitsensing Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause
"""

import io
import json
import os
import shutil
import tarfile
import tempfile
import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

from afi_sdk.someip import (
    ConfigClient,
    METHOD_GET_DEVICE_INFO,
    METHOD_SW_UPDATE_START,
    METHOD_SW_DATA_CHUNK,
    METHOD_SW_UPDATE_COMMIT,
    METHOD_SW_UPDATE_STATUS,
    METHOD_SW_UPDATE_ABORT,
    METHOD_SW_SWITCH_SLOT,
    METHOD_SW_UPDATE_REBOOT,
)

# ── Constants ──

RC_SUCCESS = 0x00
MAX_CHUNK_SIZE = 65536  # 64 KB (server-advertised max)

# Apply order: bootloader (no slot) -> driver -> firmware (a53_app serves the API).
BUNDLE_APPLY_ORDER = ["bootloader", "driver", "firmware"]

# Canonical per-domain component ordering (matches firmware apply expectations).
_ORDER_MAP = {
    "driver":     ["bl2", "fip", "kernel", "dtb", "rootfs"],
    "firmware":   ["m7_1", "m7_0", "a53_app"],
    "bootloader": ["bootloader"],
}

# domain -> GetDeviceInfo field holding that domain's installed git hash.
DOMAIN_HASH_FIELD = {
    "firmware":   "fw_hash",
    "driver":     "driver_hash",
    "bootloader": "bootloader_hash",
}

# ── Optional dependency: cryptography (only needed to decrypt .enc packages) ──

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives import padding as _sym_padding
    from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
    from cryptography.hazmat.primitives import hashes as _hashes
    HAS_CRYPTO = True
except ImportError:  # pragma: no cover - exercised only without the extra
    HAS_CRYPTO = False


class SoftwareUpdateError(Exception):
    """Raised for unrecoverable bundle/packaging problems (not per-chunk I/O)."""


def _safe_extractall(tar: tarfile.TarFile, dest_dir: str) -> None:
    """Extract with the 'data' filter where available (Python 3.12+) to guard
    against path-traversal entries; falls back to a plain extract on 3.8-3.11."""
    try:
        tar.extractall(dest_dir, filter="data")
    except TypeError:   # Python < 3.12: no 'filter' argument
        tar.extractall(dest_dir)


# ── Result types ──

@dataclass
class DomainResult:
    domain: str
    status: str                 # "updated" | "skipped" | "failed"
    version: str = ""
    error: Optional[str] = None


@dataclass
class SensorResult:
    host: str
    domains: List[DomainResult] = field(default_factory=list)
    rebooted: bool = False
    verified: Optional[bool] = None   # None = not attempted
    error: Optional[str] = None

    @property
    def flashed(self) -> Dict[str, str]:
        """{domain: version} for domains actually written this run."""
        return {d.domain: d.version for d in self.domains if d.status == "updated"}

    @property
    def ok(self) -> bool:
        return self.error is None and all(d.status != "failed" for d in self.domains)


@dataclass
class Progress:
    """One progress update passed to a ProgressCallback.

    states: connecting, checking, skip, sending, writing, verifying, switch,
            reboot, verify, done, error
    domain_index/domain_total give the position of the current domain within
    this sensor's plan (e.g. 1/3 = bootloader, 2/3 = driver, 3/3 = firmware);
    both are 0 for sensor-level events (connecting/done/error).
    """
    state: str
    domain: str = ""
    component: str = ""
    pct: int = 0
    received: int = 0
    total: int = 0
    domain_index: int = 0
    domain_total: int = 0


ProgressCallback = Callable[[Progress], None]


def _noop_progress(progress: Progress) -> None:
    pass


# ── Hash comparison (decide whether a domain needs flashing) ──

def _strip_dirty(s: str) -> str:
    s = (s or "").strip().lower()
    return s[:-6] if s.endswith("-dirty") else s


def hashes_match(pkg_ver: str, dev_hash: str) -> bool:
    """Skip decision: True if the sensor's installed hash equals the package
    version (so the domain can be skipped). A '-dirty' on either side never
    matches (always re-flash). Package uses git --short=7, device --short=8, so
    compare by the common hex prefix (>=7)."""
    a, b = (pkg_ver or "").strip().lower(), (dev_hash or "").strip().lower()
    if not a or not b or a.endswith("-dirty") or b.endswith("-dirty"):
        return False
    n = min(len(a), len(b))
    return n >= 7 and a[:n] == b[:n]


def hash_prefix_eq(a: str, b: str) -> bool:
    """Verification: same commit? Ignores '-dirty' and short-hash length diff."""
    a, b = _strip_dirty(a), _strip_dirty(b)
    if not a or not b:
        return False
    n = min(len(a), len(b))
    return n >= 7 and a[:n] == b[:n]


def sort_components(components: List[dict], update_type: str) -> List[dict]:
    order = _ORDER_MAP.get(update_type, _ORDER_MAP["firmware"])
    rank = {name: i for i, name in enumerate(order)}
    return sorted(components, key=lambda c: rank.get(c.get("name", ""), 999))


def flash_domains_for(explicit: Optional[set] = None) -> set:
    """Domains a bundle apply considers. All three (bootloader/driver/firmware)
    by default — each is still skipped when its installed hash already matches
    (see update_sensor; ``force`` disables that skip). ``explicit`` restricts to
    a chosen subset."""
    if explicit:
        return {d for d in BUNDLE_APPLY_ORDER if d in explicit}
    return set(BUNDLE_APPLY_ORDER)


# ── Package decrypt / extract ──

def decrypt_enc(blob: bytes, password: str) -> bytes:
    """Decrypt a ``.tar.gz.enc`` blob: [salt 16B][IV 16B][ciphertext],
    PBKDF2-HMAC-SHA256 (100k) -> 32B key, AES-256-CBC, PKCS7. Returns .tar.gz."""
    if not HAS_CRYPTO:
        raise SoftwareUpdateError(
            "Decrypting .enc packages requires the 'cryptography' package. "
            "Install it with:  pip install afi_sdk[update]")
    if len(blob) <= 32:
        raise SoftwareUpdateError("Encrypted package too short (bad .enc file).")
    salt, iv, ciphertext = blob[:16], blob[16:32], blob[32:]
    kdf = PBKDF2HMAC(algorithm=_hashes.SHA256(), length=32,
                     salt=salt, iterations=100_000)
    key = kdf.derive(password.encode("utf-8"))
    decryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    padded = decryptor.update(ciphertext) + decryptor.finalize()
    try:
        unpadder = _sym_padding.PKCS7(128).unpadder()
        return unpadder.update(padded) + unpadder.finalize()
    except ValueError:
        raise SoftwareUpdateError(
            "Decryption failed (wrong password or corrupt package).")


def _extract_domain_package(gz_bytes: bytes, dest_dir: str) -> Tuple[dict, Dict[str, str]]:
    """Extract a per-domain ``.tar.gz`` (in memory) into ``dest_dir``.
    Returns (manifest, {component_name: filepath})."""
    with tarfile.open(fileobj=io.BytesIO(gz_bytes), mode="r:gz") as tar:
        _safe_extractall(tar, dest_dir)
    manifest_path = os.path.join(dest_dir, "manifest.json")
    if not os.path.exists(manifest_path):
        raise SoftwareUpdateError("manifest.json not found in domain package.")
    with open(manifest_path) as f:
        manifest = json.load(f)
    file_map = {}
    for comp in manifest.get("components", []):
        fpath = os.path.join(dest_dir, comp["filename"])
        if not os.path.exists(fpath):
            raise SoftwareUpdateError(
                f"Component file missing: {comp['filename']}")
        file_map[comp["name"]] = fpath
    return manifest, file_map


def is_software_bundle(path: str) -> bool:
    """True if ``path`` is a software bundle (plaintext outer tar carrying
    software_manifest.json). A single-domain package (.enc / .tar.gz) is not."""
    if path.endswith(".enc"):
        return False
    try:
        with tarfile.open(path, "r:*") as tar:
            return any(os.path.basename(n) == "software_manifest.json"
                       for n in tar.getnames())
    except (tarfile.TarError, OSError):
        return False


def _load_inner_gz(tmp_dir: str, filename: str, password: str) -> bytes:
    """Read an inner per-domain package from the extracted bundle dir and return
    its plaintext ``.tar.gz`` bytes (decrypting if it ends in .enc)."""
    path = os.path.join(tmp_dir, filename)
    with open(path, "rb") as f:
        blob = f.read()
    return decrypt_enc(blob, password) if filename.endswith(".enc") else blob


# ── Chunked upload of one component ──

def upload_component(client: ConfigClient, update_type: str, name: str,
                     filepath: str, crc32_hex: str, *, timeout: float = 120.0,
                     on_progress: Optional[Callable] = None) -> Tuple[bool, Optional[str]]:
    """START -> CHUNK*N -> COMMIT -> poll STATUS for one component.

    on_progress(state, pct, received, total). Returns (ok, error)."""
    cb = on_progress or (lambda *a: None)
    file_size = os.path.getsize(filepath)

    rc, _ = client.request(METHOD_SW_UPDATE_START, {
        "update_type": update_type, "component": name,
        "total_size": file_size, "crc32": crc32_hex,
    })
    if rc != RC_SUCCESS:
        return False, f"START failed (rc={rc})"

    with open(filepath, "rb") as f:
        sent = 0
        while sent < file_size:
            chunk = f.read(MAX_CHUNK_SIZE)
            rc, data = client.request_binary(METHOD_SW_DATA_CHUNK, chunk)
            if rc != RC_SUCCESS:
                client.request(METHOD_SW_UPDATE_ABORT, {})
                return False, f"CHUNK failed at offset {sent} (rc={rc})"
            data = data or {}
            received = int(data.get("received_bytes", sent + len(chunk)))
            total = int(data.get("total_bytes", file_size)) or file_size
            cb("sending", received * 100 // total, received, total)
            sent += len(chunk)

    rc, _ = client.request(METHOD_SW_UPDATE_COMMIT, {})
    if rc != RC_SUCCESS:
        client.request(METHOD_SW_UPDATE_ABORT, {})
        return False, f"COMMIT failed (rc={rc})"

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rc, data = client.request(METHOD_SW_UPDATE_STATUS, {})
        data = data or {}
        state = data.get("state", "unknown")
        if state == "idle":
            cb("writing", 100, file_size, file_size)
            cb("verifying", 100, file_size, file_size)
            return True, None
        if state == "error":
            return False, data.get("error_message") or "flash error"
        cb(state, int(data.get("progress_pct", 0)), 0, file_size)
        time.sleep(0.2)

    client.request(METHOD_SW_UPDATE_ABORT, {})
    return False, f"timeout after {timeout:.0f}s"


# ── Per-sensor orchestration ──

def _read_device_hashes(client: ConfigClient) -> Optional[dict]:
    rc, data = client.request(METHOD_GET_DEVICE_INFO)
    return data if rc == RC_SUCCESS and data else None


def update_sensor(host: str, bundle_path: str, password: str, *,
                  port: int = 30500, flash_domains: Optional[set] = None,
                  force: bool = False, components: Optional[set] = None,
                  timeout: float = 120.0,
                  on_progress: ProgressCallback = _noop_progress) -> SensorResult:
    """Flash one sensor from a software bundle (does NOT reboot).

    Applies bootloader -> driver -> firmware for domains in ``flash_domains``,
    skipping any domain whose installed hash already matches the package
    (unless ``force`` or a ``components`` subset is given). Slot-switches
    driver/firmware after a successful flash. Returns a SensorResult; the
    caller reboots and verifies once.
    """
    result = SensorResult(host=host)
    if flash_domains is None:
        flash_domains = flash_domains_for()

    tmp = tempfile.mkdtemp(prefix="afi_bundle_")
    client: Optional[ConfigClient] = None
    try:
        with tarfile.open(bundle_path, "r:*") as tar:   # outer tar is plaintext
            _safe_extractall(tar, tmp)
        sw_path = os.path.join(tmp, "software_manifest.json")
        if not os.path.exists(sw_path):
            raise SoftwareUpdateError(
                "software_manifest.json not found — not a software bundle.")
        with open(sw_path) as f:
            sw = json.load(f)
        pkgs = {p.get("update_type"): p for p in sw.get("packages", [])}

        # Ordered list of domains actually applied to this sensor (drives the
        # "i/N" progress count).
        plan = [d for d in BUNDLE_APPLY_ORDER if d in flash_domains and d in pkgs]
        total = len(plan)

        on_progress(Progress("connecting", domain_total=total))
        client = ConfigClient(host, port, timeout=5.0)
        if not client.connect():
            result.error = "connection failed"
            on_progress(Progress("error", domain_total=total))
            return result

        dev = _read_device_hashes(client) or {}

        for idx, domain in enumerate(plan, 1):
            dr = _apply_domain(client, tmp, pkgs[domain], password, dev,
                               domain_index=idx, domain_total=total,
                               force=force, components=components,
                               timeout=timeout, on_progress=on_progress)
            result.domains.append(dr)
            if dr.status == "failed":
                break   # stop the sequence on first failure
        on_progress(Progress("done", pct=100, domain_total=total))
        return result
    except SoftwareUpdateError as e:
        result.error = str(e)
        on_progress(Progress("error"))
        return result
    finally:
        if client:
            client.close()
        shutil.rmtree(tmp, ignore_errors=True)


def _apply_domain(client, tmp, pkg, password, dev, *, domain_index=0,
                  domain_total=0, force, components, timeout,
                  on_progress) -> DomainResult:
    domain = pkg.get("update_type", "")
    dest = tempfile.mkdtemp(prefix=f"afi_{domain}_", dir=tmp)
    gz = _load_inner_gz(tmp, pkg["filename"], password)
    manifest, file_map = _extract_domain_package(gz, dest)
    return _apply_loaded(client, domain, manifest, file_map, dev,
                         domain_index=domain_index, domain_total=domain_total,
                         force=force, components=components,
                         timeout=timeout, on_progress=on_progress)


def _apply_loaded(client, domain, manifest, file_map, dev, *, domain_index=0,
                  domain_total=0, force, components, timeout,
                  on_progress) -> DomainResult:
    pkg_ver = manifest.get("git_version", "")

    def emit(state, component="", pct=0, received=0, total=0):
        on_progress(Progress(state, domain=domain, component=component, pct=pct,
                             received=received, total=total,
                             domain_index=domain_index, domain_total=domain_total))

    # Skip unchanged domain unless forced / partial component selection.
    if not force and not components:
        dev_hash = str(dev.get(DOMAIN_HASH_FIELD.get(domain, ""), ""))
        if hashes_match(pkg_ver, dev_hash):
            emit("skip", pct=100)
            return DomainResult(domain, "skipped", pkg_ver)

    comps = manifest.get("components", [])
    if components:
        comps = [c for c in comps if c.get("name") in components]
    comps = sort_components(comps, domain)

    emit("checking")
    for comp in comps:
        name = comp["name"]

        def _cb(state, pct, received, total, _n=name):
            emit(state, component=_n, pct=pct, received=received, total=total)

        ok, err = upload_component(client, domain, name, file_map[name],
                                   comp["crc32"], timeout=timeout, on_progress=_cb)
        if not ok:
            emit("error", component=name)
            return DomainResult(domain, "failed", pkg_ver,
                                error=f"{name}: {err}")

    # Slot switch for driver/firmware (bootloader is Primary/Backup, no slot).
    if domain != "bootloader":
        emit("switch", pct=100)
        rc, _ = client.request(METHOD_SW_SWITCH_SLOT, {"update_type": domain})
        if rc != RC_SUCCESS:
            return DomainResult(domain, "failed", pkg_ver,
                                error=f"slot switch failed (rc={rc})")
    return DomainResult(domain, "updated", pkg_ver)


def update_sensor_single(host: str, package_path: str, password: str, *,
                          port: int = 30500, force: bool = False,
                          components: Optional[set] = None, timeout: float = 120.0,
                          on_progress: ProgressCallback = _noop_progress) -> SensorResult:
    """Flash one sensor from a single-domain package (``*.tar.gz`` or
    ``*.tar.gz.enc``), e.g. a firmware-only package. Does NOT reboot."""
    result = SensorResult(host=host)
    dest = tempfile.mkdtemp(prefix="afi_pkg_")
    client: Optional[ConfigClient] = None
    try:
        with open(package_path, "rb") as f:
            blob = f.read()
        gz = decrypt_enc(blob, password) if package_path.endswith(".enc") else blob
        manifest, file_map = _extract_domain_package(gz, dest)
        domain = manifest.get("update_type", "")

        on_progress(Progress("connecting", domain_total=1))
        client = ConfigClient(host, port, timeout=5.0)
        if not client.connect():
            result.error = "connection failed"
            on_progress(Progress("error", domain_total=1))
            return result

        dev = _read_device_hashes(client) or {}
        dr = _apply_loaded(client, domain, manifest, file_map, dev,
                           domain_index=1, domain_total=1,
                           force=force, components=components,
                           timeout=timeout, on_progress=on_progress)
        result.domains.append(dr)
        on_progress(Progress("done", pct=100, domain_total=1))
        return result
    except SoftwareUpdateError as e:
        result.error = str(e)
        on_progress(Progress("error"))
        return result
    finally:
        if client:
            client.close()
        shutil.rmtree(dest, ignore_errors=True)


# ── Reboot + post-reboot verification ──

def reboot(host: str, port: int = 30500) -> None:
    """Send the reboot command (the connection drops as the sensor reboots)."""
    try:
        c = ConfigClient(host, port, timeout=5.0)
        if c.connect():
            c.request(METHOD_SW_UPDATE_REBOOT, {})
        c.close()
    except OSError:
        pass


def verify_target(host: str, expected: Dict[str, str], *, port: int = 30500,
                  wait: float = 120.0, bind_ip: str = "",
                  on_tick: Optional[Callable[[], None]] = None) -> bool:
    """Wait for the sensor to reboot (drop off Discovery, then reappear), then
    read GetDeviceInfo and confirm each flashed domain's hash matches.

    firmware is authoritative; driver/bootloader are advisory (the sensor reports
    the firmware-baked hash, which can lag the freshly-flashed content)."""
    from afi_sdk.someip import discover

    def _present() -> bool:
        return any((s.get("ip_address") == host or s.get("_source_ip") == host)
                   for s in discover(timeout_s=2.0, bind_ip=bind_ip))

    deadline = time.monotonic() + wait
    went_down = False
    while time.monotonic() < deadline:
        up = _present()
        if not went_down:
            if not up:
                went_down = True
        elif up:
            break
        if on_tick:
            on_tick()
        time.sleep(1.0)
    else:
        return False

    c = ConfigClient(host, port, timeout=5.0)
    if not c.connect():
        return False
    try:
        rc, info = c.request(METHOD_GET_DEVICE_INFO)
    finally:
        c.close()
    if rc != RC_SUCCESS or not info:
        return False

    ok = True
    for domain, pkg_ver in expected.items():
        dev = str(info.get(DOMAIN_HASH_FIELD.get(domain, ""), ""))
        if not hash_prefix_eq(pkg_ver, dev) and domain == "firmware":
            ok = False   # only firmware is gated; others are advisory
    return ok
