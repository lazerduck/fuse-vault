#!/usr/bin/env python3
"""Create signed firmware and staged, non-programming RP2354A OTP inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any

from verify_otp_policy import (
    load_json,
    validate_boot_policy,
    validate_layout,
    validate_permissions,
)


def stop(message: str) -> None:
    raise RuntimeError(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_new_json(path: pathlib.Path, value: dict[str, Any]) -> None:
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")


def checked_key_hashes(policy: dict[str, Any]) -> tuple[str, str]:
    blockers = validate_boot_policy(policy)
    if blockers:
        stop("policy signing identities are incomplete: " + "; ".join(blockers))
    keys = policy["secure_boot"]["boot_keys"]
    return keys[0]["public_key_sha256"].lower(), keys[1][
        "public_key_sha256"
    ].lower()


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=pathlib.Path,
                        help="unsigned ELF, BIN, or UF2")
    parser.add_argument("--output", required=True, type=pathlib.Path,
                        help="new signed image; must not already exist")
    parser.add_argument("--private-key", required=True, type=pathlib.Path,
                        help="release secp256k1 private key PEM")
    parser.add_argument("--artifact-dir", required=True, type=pathlib.Path,
                        help="new directory for staged OTP inputs and receipt")
    parser.add_argument("--major", required=True, type=int)
    parser.add_argument("--minor", required=True, type=int)
    parser.add_argument("--rollback", required=True, type=int)
    parser.add_argument("--policy", type=pathlib.Path,
                        default=root / "provisioning" / "otp-policy-v1.json")
    parser.add_argument("--permissions", type=pathlib.Path,
                        default=root / "provisioning" /
                        "picotool-otp-permissions-v1.json")
    parser.add_argument("--picotool", type=pathlib.Path,
                        default=root / "firmware" / "build" / "_deps" /
                        "picotool" / "picotool")
    args = parser.parse_args()

    try:
        if min(args.major, args.minor) < 0 or args.rollback < 1:
            stop("major/minor must be nonnegative and rollback must be at least 1")
        for path, label in ((args.input, "input image"),
                            (args.private_key, "private key"),
                            (args.picotool, "picotool")):
            if not path.is_file():
                stop(f"{label} does not exist: {path}")
        if args.output.exists():
            stop(f"output already exists: {args.output}")
        if not args.output.parent.is_dir():
            stop(f"output parent directory does not exist: {args.output.parent}")
        if args.artifact_dir.exists():
            stop(f"artifact directory already exists: {args.artifact_dir}")
        if not args.artifact_dir.parent.is_dir():
            stop(f"artifact parent directory does not exist: {args.artifact_dir.parent}")

        policy = load_json(args.policy)
        permissions = load_json(args.permissions)
        validate_layout(policy)
        validate_permissions(policy, permissions)
        release_hash, recovery_hash = checked_key_hashes(policy)
        rollback_policy = policy["secure_boot"]["rollback"]
        if args.rollback < rollback_policy["initial_version"]:
            stop("rollback version is below the policy minimum")

        with tempfile.TemporaryDirectory(prefix="fuse-vault-release-") as temp_name:
            temp = pathlib.Path(temp_name)
            signed = temp / ("signed" + args.output.suffix)
            generated_otp = temp / "picotool-generated-otp.json"
            command = [
                str(args.picotool), "seal", "--sign", "--clear",
                str(args.input), str(signed), str(args.private_key),
                str(generated_otp), "--major", str(args.major), "--minor",
                str(args.minor), "--rollback", str(args.rollback),
            ]
            result = subprocess.run(command, check=False, text=True,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            if result.returncode != 0:
                stop("picotool seal failed:\n" + result.stdout)
            generated = load_json(generated_otp)
            generated_release = bytes(generated.get("bootkey0", [])).hex()
            if generated_release != release_hash:
                stop(
                    "private key does not match policy slot 0: generated "
                    f"{generated_release}, policy {release_hash}"
                )

            info = subprocess.run(
                [str(args.picotool), "info", "--all", str(signed)],
                check=False, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            if info.returncode != 0 or "signature:" not in info.stdout or \
                    "verified" not in info.stdout:
                stop("picotool did not verify the signed output:\n" + info.stdout)
            if re.search(
                rf"rollback version:\s+{args.rollback}(?:\s|$)", info.stdout
            ) is None:
                stop("signed output does not report the requested rollback version")

            args.artifact_dir.mkdir()
            boot_keys = {
                "bootkey0": list(bytes.fromhex(release_hash)),
                "bootkey1": list(bytes.fromhex(recovery_hash)),
            }
            key_state = {
                "boot_flags1": {"key_invalid": 12, "key_valid": 3}
            }
            final_lockdown = {
                "boot_flags0": {
                    "disable_bootsel_uart_boot": 1,
                    "rollback_required": 1,
                },
                "crit1": {
                    "debug_disable": 1,
                    "secure_boot_enable": 1,
                    "secure_debug_disable": 1,
                },
            }
            paths = {
                "boot_keys": args.artifact_dir / "01-boot-keys.json",
                "boot_key_state": args.artifact_dir / "02-boot-key-state.json",
                "page_permissions": args.artifact_dir /
                                    "03-page-permissions.json",
                "final_lockdown": args.artifact_dir / "04-final-lockdown.json",
            }
            write_new_json(paths["boot_keys"], boot_keys)
            write_new_json(paths["boot_key_state"], key_state)
            write_new_json(paths["page_permissions"], permissions)
            write_new_json(paths["final_lockdown"], final_lockdown)
            shutil.copyfile(signed, args.output)

            receipt = {
                "policy_id": policy["policy_id"],
                "target": policy["target"],
                "version": {"major": args.major, "minor": args.minor,
                            "rollback": args.rollback},
                "signed_image": {
                    "filename": args.output.name,
                    "sha256": sha256_file(args.output),
                },
                "otp_inputs": {
                    name: {"filename": path.name, "sha256": sha256_file(path)}
                    for name, path in paths.items()
                },
                "picotool_verification": "signature verified",
                "warning": "receipt creation did not program OTP or flash hardware",
            }
            write_new_json(args.artifact_dir / "release-receipt.json", receipt)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1

    print(f"Signed image: {args.output}")
    print(f"Staged OTP inputs and receipt: {args.artifact_dir}")
    print("No hardware was flashed and no OTP was programmed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
