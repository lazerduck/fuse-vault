# Fuse Vault release and OTP artifacts

This directory separates release preparation from irreversible hardware
provisioning. Repository tools create and verify files only. They never invoke
`picotool load`, `picotool otp load`, `picotool otp permissions`, or any other
device-writing operation.

## Files

- `otp-policy-v1.json` is the complete machine-readable release policy. Its two
  signing-key hashes and its hardware-evidence flags intentionally start
  incomplete.
- `picotool-otp-permissions-v1.json` is the picotool 2.3.0 page-permission input
  for OTP pages 59 and 60.
- `tools/verify_otp_policy.py` checks allocation, permissions, majority-vote
  words, boot/recovery policy, key slots, and evidence gates without touching a
  device.
- `tools/prepare_signed_release.py` signs an image, asks picotool to verify it,
  and creates four ordered OTP input files plus a SHA-256 receipt. It does not
  program them.

## Key ceremony and release preparation

Generate independent secp256k1 release and offline-recovery signing identities
outside the repository. Keep private keys out of source control. Record the
picotool-compatible SHA-256 public-key digests in slots 0 and 1 of
`otp-policy-v1.json`, then set the two corresponding `release_evidence` flags
only after an independent comparison.

Prepare each final ELF or UF2 with a deliberate semantic and rollback version:

```text
python3 tools/prepare_signed_release.py \
  --input <unsigned-image> \
  --output <new-signed-image> \
  --private-key <release-private-key.pem> \
  --artifact-dir <new-receipt-directory> \
  --major <major> --minor <minor> --rollback <monotonic-version>
```

The command refuses to overwrite an image or artifact directory, refuses an
unrecorded/mismatched release key, includes full-SRAM clearing in the signed
image metadata, and checks picotool's signature and rollback report. Run it once
for the factory ELF and once for the USB-recovery UF2 using the same version.

## Irreversible ceremony boundary

The numbered JSON files are deliberately staged rather than combined:

1. Program and independently read back both boot-key hashes.
2. Mark slots 0 and 1 valid and slots 2 and 3 permanently invalid.
3. After ordinary firmware first setup creates the device roots and verifies
   their read-back (no image swap required), apply pages 59 and 60 access
   permissions using picotool's dedicated permissions command.
4. Only after signed normal boot, signed USB recovery, rollback, permission,
   power-cut, and debug tests pass, apply the final critical boot flags. Secure
   boot and permanent debug disablement are in this last file.

The actual hardware commands are intentionally absent. They must live in a
separately reviewed manufacturing work instruction with device identity,
operator confirmation, independent read-back, and reject/quarantine handling.
Run `python3 tools/verify_otp_policy.py --release` before that ceremony; any open
gate is a stop condition.
