"""
Unit tests: afi_sdk.software_update — bundle decrypt/extract, hash compare,
and the CLI duplicate-IP guard. No sensor required.

Building a synthetic encrypted bundle needs the 'cryptography' package; those
tests skip automatically if it is not installed (pip install afi_sdk[update]).

Run: pytest test/test_software_update.py
"""

import io
import json
import os
import sys
import tarfile
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python" / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from afi_sdk import software_update as su  # noqa: E402

PASSWORD = "unit-test-password"


# ── Pure logic (no crypto needed) ──

def test_hashes_match_skip_semantics():
    assert su.hashes_match("a1b2c3d", "a1b2c3d4") is True      # 7+ prefix, len diff ok
    assert su.hashes_match("a1b2c3d", "ffffff0") is False
    assert su.hashes_match("a1b2c3d-dirty", "a1b2c3d4") is False  # dirty always re-flash
    assert su.hashes_match("a1b2c3d", "a1b2c3d-dirty") is False
    assert su.hashes_match("", "a1b2c3d") is False
    assert su.hashes_match("abc", "abc") is False              # <7 hex never matches


def test_hash_prefix_eq_verify_semantics():
    assert su.hash_prefix_eq("a1b2c3d-dirty", "a1b2c3d4") is True   # dirty ignored
    assert su.hash_prefix_eq("a1b2c3d", "ffffff0") is False


def test_sort_components_canonical_order():
    comps = [{"name": n} for n in ("a53_app", "m7_0", "m7_1")]
    ordered = [c["name"] for c in su.sort_components(comps, "firmware")]
    assert ordered == ["m7_1", "m7_0", "a53_app"]


def test_flash_domains_default_includes_all():
    # All three domains are considered by default (each still hash-skipped
    # unless force); bootloader is no longer force-gated.
    assert su.flash_domains_for() == {"bootloader", "firmware", "driver"}
    assert su.flash_domains_for(explicit={"bootloader"}) == {"bootloader"}
    assert su.flash_domains_for(explicit={"firmware", "driver"}) == {"firmware", "driver"}


# ── Encryption helper (mirrors decrypt_enc) ──

def _encrypt(plaintext: bytes, password: str) -> bytes:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives import padding as sym_padding
    from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
    from cryptography.hazmat.primitives import hashes
    salt, iv = b"0123456789abcdef", b"fedcba9876543210"
    kdf = PBKDF2HMAC(algorithm=hashes.SHA256(), length=32, salt=salt, iterations=100_000)
    key = kdf.derive(password.encode("utf-8"))
    padder = sym_padding.PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return salt + iv + enc.update(padded) + enc.finalize()


def _make_domain_gz(update_type: str, git_version: str) -> bytes:
    """Build a per-domain .tar.gz (manifest.json + one dummy .bin)."""
    comp_bin = b"\xde\xad\xbe\xef" * 64
    manifest = {
        "version": 1, "update_type": update_type, "git_version": git_version,
        "components": [{"name": update_type + "_bin",
                        "filename": "part.bin", "size": len(comp_bin),
                        "crc32": "0xDEADBEEF"}],
    }
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w:gz") as tar:
        for name, data in (("manifest.json", json.dumps(manifest).encode()),
                           ("part.bin", comp_bin)):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return raw.getvalue()


def _make_bundle(path: str, domains: dict, password: str):
    """domains = {update_type: git_version}. Writes an outer .tar with a
    software_manifest.json + one encrypted .tar.gz.enc per domain."""
    packages = []
    blobs = {}
    for dtype, ver in domains.items():
        fname = f"{dtype}.tar.gz.enc"
        blobs[fname] = _encrypt(_make_domain_gz(dtype, ver), password)
        packages.append({"update_type": dtype, "filename": fname})
    sw_manifest = json.dumps({"software_version": "3.0.1",
                              "packages": packages}).encode()
    with tarfile.open(path, "w") as tar:      # plaintext outer tar
        for name, data in [("software_manifest.json", sw_manifest), *blobs.items()]:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))


crypto = pytest.importorskip("cryptography")  # skip crypto tests if missing


def test_decrypt_extract_roundtrip():
    gz = _make_domain_gz("firmware", "a1b2c3d")
    enc = _encrypt(gz, PASSWORD)
    assert su.decrypt_enc(enc, PASSWORD) == gz

    with tempfile.TemporaryDirectory() as d:
        manifest, file_map = su._extract_domain_package(gz, d)
        assert manifest["update_type"] == "firmware"
        assert manifest["git_version"] == "a1b2c3d"
        assert os.path.exists(file_map["firmware_bin"])


def test_decrypt_wrong_password_raises():
    enc = _encrypt(_make_domain_gz("driver", "abc1234"), PASSWORD)
    with pytest.raises(su.SoftwareUpdateError):
        su.decrypt_enc(enc, "wrong-password")


def test_is_software_bundle_and_inner_load():
    with tempfile.TemporaryDirectory() as d:
        bundle = os.path.join(d, "afi920_software.tar")
        _make_bundle(bundle, {"firmware": "a1b2c3d", "driver": "abc1234"}, PASSWORD)
        assert su.is_software_bundle(bundle) is True

        # Extract outer tar, decrypt+parse one inner domain package.
        with tarfile.open(bundle, "r:*") as tar:
            su._safe_extractall(tar, d)
        gz = su._load_inner_gz(d, "firmware.tar.gz.enc", PASSWORD)
        manifest, file_map = su._extract_domain_package(gz, os.path.join(d, "fw"))
        assert manifest["update_type"] == "firmware"
        assert manifest["git_version"] == "a1b2c3d"


def test_single_domain_package_not_a_bundle():
    with tempfile.TemporaryDirectory() as d:
        enc_path = os.path.join(d, "firmware_x.tar.gz.enc")
        with open(enc_path, "wb") as f:
            f.write(_encrypt(_make_domain_gz("firmware", "a1b2c3d"), PASSWORD))
        assert su.is_software_bundle(enc_path) is False


# ── CLI duplicate-IP guard ──

def test_cli_duplicate_ip_guard():
    import software_update as cli   # tools/software_update.py
    sensors = [
        {"_ip": "192.168.10.150", "serial_number": "A"},
        {"_ip": "192.168.10.150", "serial_number": "B"},
        {"_ip": "192.168.10.151", "serial_number": "C"},
    ]
    conflicts = cli.find_ip_conflicts(sensors)
    assert list(conflicts) == ["192.168.10.150"]
    assert conflicts["192.168.10.150"] == ["A", "B"]

    # No conflict when every IP is unique.
    assert cli.find_ip_conflicts(sensors[1:]) == {}
