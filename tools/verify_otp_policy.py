#!/usr/bin/env python3
"""Read-only consistency and release-gate checks for the Fuse Vault OTP policy."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from typing import Any


EXPECTED_PERMISSIONS = {
    "59": {
        "no_key_state": 0,
        "key_r": 0,
        "key_w": 0,
        "lock_bl": 3,
        "lock_ns": 3,
        "lock_s": 0,
    },
    "60": {
        "no_key_state": 0,
        "key_r": 0,
        "key_w": 0,
        "lock_bl": 3,
        "lock_ns": 3,
        "lock_s": 1,
    },
}


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    if not isinstance(value, dict):
        fail(f"{path} must contain a JSON object")
    return value


def require_equal(actual: Any, expected: Any, label: str) -> None:
    if actual != expected:
        fail(f"{label}: expected {expected!r}, got {actual!r}")


def parse_hex(value: Any, label: str, digits: int) -> int:
    if not isinstance(value, str) or re.fullmatch(
        rf"0x[0-9a-fA-F]{{{digits}}}", value
    ) is None:
        fail(f"{label} must be a {digits}-digit hexadecimal string")
    return int(value, 16)


def majority_word(byte: int) -> int:
    return byte | (byte << 8) | (byte << 16)


def validate_permissions(policy: dict[str, Any], permissions: dict[str, Any]) -> None:
    supplied = {key: value for key, value in permissions.items() if key != "$schema"}
    require_equal(supplied, EXPECTED_PERMISSIONS, "picotool permissions")

    policy_permissions = policy.get("page_permissions")
    if not isinstance(policy_permissions, dict):
        fail("page_permissions must be an object")

    names = {0: "read_write", 1: "read_only", 3: "inaccessible"}
    for page, expected in EXPECTED_PERMISSIONS.items():
        entry = policy_permissions.get(page)
        if not isinstance(entry, dict):
            fail(f"page_permissions.{page} must be an object")
        lock0 = expected["key_w"] | (expected["key_r"] << 3) | (
            expected["no_key_state"] << 6
        )
        lock1 = expected["lock_s"] | (expected["lock_ns"] << 2) | (
            expected["lock_bl"] << 4
        )
        require_equal(
            parse_hex(entry.get("lock0_byte"), f"page {page} lock0_byte", 2),
            lock0,
            f"page {page} LOCK0 byte",
        )
        require_equal(
            parse_hex(entry.get("lock1_byte"), f"page {page} lock1_byte", 2),
            lock1,
            f"page {page} LOCK1 byte",
        )
        require_equal(
            parse_hex(
                entry.get("raw_lock0_majority"),
                f"page {page} raw_lock0_majority",
                6,
            ),
            majority_word(lock0),
            f"page {page} majority-voted LOCK0",
        )
        require_equal(
            parse_hex(
                entry.get("raw_lock1_majority"),
                f"page {page} raw_lock1_majority",
                6,
            ),
            majority_word(lock1),
            f"page {page} majority-voted LOCK1",
        )
        require_equal(entry.get("bootloader"), names[expected["lock_bl"]],
                      f"page {page} bootloader permission")
        require_equal(entry.get("nonsecure"), names[expected["lock_ns"]],
                      f"page {page} nonsecure permission")
        require_equal(entry.get("secure"), names[expected["lock_s"]],
                      f"page {page} secure permission")


def validate_layout(policy: dict[str, Any]) -> None:
    require_equal(policy.get("schema_version"), 1, "schema_version")
    require_equal(policy.get("policy_id"), "fuse-vault-rp2354a-otp-v1", "policy_id")
    target = policy.get("target", {})
    require_equal(target.get("mcu"), "RP2354A", "target MCU")
    require_equal(target.get("execution_domain"), "ARM Secure", "execution domain")

    identity = policy.get("device_identity", {})
    require_equal(identity.get("root_page"), 60, "root page")
    require_equal(identity.get("root_a_rows"), [3840, 3855], "Root A rows")
    require_equal(identity.get("root_b_rows"), [3856, 3871], "Root B rows")
    require_equal(identity.get("format_row"), 3872, "format row")
    require_equal(parse_hex(identity.get("format_value"), "format value", 4),
                  0x4656, "format value")
    require_equal(identity.get("active_row"), 3873, "active row")
    require_equal(parse_hex(identity.get("active_value"), "active value", 4),
                  0xA55A, "active value")
    require_equal(identity.get("reserved_rows"), [3874, 3903],
                  "root-page reserved rows")

    revocation = policy.get("revocation", {})
    require_equal(revocation.get("page"), 59, "revocation page")
    require_equal(revocation.get("row"), 3776, "revocation row")
    require_equal(parse_hex(revocation.get("value"), "revocation value", 4),
                  0xDEAD, "revocation value")
    require_equal(revocation.get("reserved_rows"), [3777, 3839],
                  "revocation-page reserved rows")


def validate_boot_policy(policy: dict[str, Any]) -> list[str]:
    boot = policy.get("secure_boot")
    if not isinstance(boot, dict):
        fail("secure_boot must be an object")
    for field in ("secure_boot_enable", "debug_disable", "secure_debug_disable"):
        require_equal(boot.get(field), True, f"secure_boot.{field}")
    require_equal(boot.get("boot_architecture"), "ARM", "boot architecture")

    recovery = boot.get("recovery", {})
    require_equal(recovery.get("usb_picoboot"), "enabled_signed_images_only",
                  "USB PICOBOOT recovery")
    require_equal(recovery.get("usb_mass_storage"), "enabled_signed_images_only",
                  "USB mass-storage recovery")
    require_equal(recovery.get("uart_boot"), "disabled", "UART recovery")

    rollback = boot.get("rollback", {})
    require_equal(rollback.get("required"), True, "rollback.required")
    require_equal(rollback.get("initial_version"), 1, "initial rollback version")
    require_equal(rollback.get("rows"), [78, 81], "rollback rows")

    keys = boot.get("boot_keys")
    if not isinstance(keys, list) or len(keys) != 4:
        fail("secure_boot.boot_keys must contain exactly four slots")
    blockers: list[str] = []
    expected = ((0, "release", "valid"), (1, "offline_recovery", "valid"),
                (2, "unused", "permanently_invalid"),
                (3, "unused", "permanently_invalid"))
    for key, (slot, role, state) in zip(keys, expected):
        require_equal(key.get("slot"), slot, f"boot key {slot} slot")
        require_equal(key.get("role"), role, f"boot key {slot} role")
        require_equal(key.get("state"), state, f"boot key {slot} state")
        digest = key.get("public_key_sha256")
        if state == "valid":
            if not isinstance(digest, str) or re.fullmatch(
                r"[0-9a-fA-F]{64}", digest
            ) is None:
                blockers.append(f"boot key slot {slot} SHA-256 is not recorded")
        elif digest is not None:
            fail(f"unused boot key slot {slot} must not contain a hash")
    return blockers


def validate_release_evidence(policy: dict[str, Any]) -> list[str]:
    evidence = policy.get("release_evidence")
    if not isinstance(evidence, dict) or not evidence:
        fail("release_evidence must be a non-empty object")
    blockers = []
    for name, passed in evidence.items():
        if not isinstance(passed, bool):
            fail(f"release_evidence.{name} must be boolean")
        if not passed:
            blockers.append(f"release evidence missing: {name}")
    return blockers


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", type=pathlib.Path,
                        default=root / "provisioning" / "otp-policy-v1.json")
    parser.add_argument("--permissions", type=pathlib.Path,
                        default=root / "provisioning" /
                        "picotool-otp-permissions-v1.json")
    parser.add_argument("--release", action="store_true",
                        help="also fail while key/evidence release gates remain open")
    args = parser.parse_args()

    try:
        policy = load_json(args.policy)
        permissions = load_json(args.permissions)
        validate_layout(policy)
        validate_permissions(policy, permissions)
        blockers = validate_boot_policy(policy)
        blockers.extend(validate_release_evidence(policy))
    except ValueError as error:
        print(f"OTP policy invalid: {error}", file=sys.stderr)
        return 2

    print("OTP policy structure and picotool permission encoding are consistent.")
    if blockers:
        print("Open production gates:")
        for blocker in blockers:
            print(f"- {blocker}")
    if args.release and blockers:
        return 1
    if args.release:
        print("All recorded production gates pass.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
