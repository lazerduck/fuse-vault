# Fuse Vault OTP provisioning manifest V1

Status: frozen software allocation and candidate permission words;
irreversible values require sacrificial-silicon validation before production
use.

The machine-readable policy is
`provisioning/otp-policy-v1.json`. The directly picotool-compatible page
permission input is `provisioning/picotool-otp-permissions-v1.json` and the
read-only consistency check is `tools/verify_otp_policy.py`. The release
packager signs and verifies ELF/UF2 files and emits four ordered OTP inputs plus
a hash receipt. Nothing in the repository invokes OTP programming
automatically; see `provisioning/README.md`.

## Device-root and revocation allocation

Fuse Vault owns RP2350 user-data OTP pages 59 and 60. Page 60 contains immutable
device identity; page 59 contains only the field-writable revocation transition.
This separation is required because RP2350 hard-lock permissions apply to an
entire 64-row page. Pages 61 through 63 are excluded because Raspberry Pi
reserves them for future use. Root, marker, and revocation values use the Boot
ROM OTP access API in ECC mode; page-lock rows use their required raw
majority-vote encoding.

| Page | Page rows | Absolute rows | Meaning | Write phase |
|---:|---:|---:|---|---|
| 60 | 0–15 | 3840–3855 | Root A, 256 random bits, SHA-family domain | Provisioning only |
| 60 | 16–31 | 3856–3871 | Root B, 256 random bits, Keccak-family domain | Provisioning only |
| 60 | 32 | 3872 | Format marker `0x4656` | After root read-back |
| 60 | 33 | 3873 | Active marker `0xa55a` | Final lifecycle commit |
| 60 | 34–63 | 3874–3903 | Reserved; must remain zero in V1 | Never |
| 59 | 0 | 3776 | Revocation marker `0xdead` | Field destructive lockout |
| 59 | 1–63 | 3777–3839 | Reserved; must remain zero in V1 | Never |

There are no fallback root slots. A partial or inconsistent provisioning write
permanently rejects that MCU. The revocation marker overrides every other value
and can never select a later root set.

## State interpretation

| Root/format state | Active | Revoked | Interpretation |
|---|---|---|---|
| All zero | Zero | Zero | Empty; setup permitted |
| Both roots nonzero and format exact | Exact marker | Zero | Active |
| Any | Any | Exact marker | Revoked; root use permanently refused |
| Any other combination | Any | Zero/nonexact | Invalid; fail locked |

Nonzero but nonexact marker values are invalid. Unknown format versions do not
fall back to V1.

## Device identity provisioning order

1. Verify pages 59 and 60 are completely zero and lifecycle is `EMPTY`.
2. Use the ordinary firmware first-setup flow; the same image remains installed
   afterward. Device identity creation is distinct from vault-key creation.
3. Generate Root A and Root B.
4. Program both root ranges and compare an ECC read-back byte for byte.
5. Program and verify the format marker.
6. Program and verify the active marker as the irreversible commit.
7. Reset and independently verify the ACTIVE lifecycle and root readback.
8. Apply and verify the reviewed permanent page permissions.

Any partial OTP programming from step 4 onward rejects that MCU; firmware never
writes a second root set. A lost acknowledgement is accepted only when lifecycle
status and byte-for-byte readback prove the requested roots are active.

## User vault setup order

1. Boot firmware and accept either EMPTY OTP or ACTIVE roots with an empty
   vault journal. Invalid, partial, revoked and unreadable OTP fails closed.
2. Detect the selected SD card and obtain explicit destructive confirmation.
3. Select and confirm the password-entry method and secret, then select the
   ordered encryption stack and accept the no-recovery policy.
4. If OTP is EMPTY, generate and verify roots first; otherwise reuse ACTIVE
   roots. Then generate the vault ID, salts, and VMK.
5. Create and verify the authenticated SD layout, redundant header, wrapped VMK,
   encryption descriptor, data domain, and future-FIDO reservation.
6. Append and verify the first provisioned internal-journal record last.
7. Restart and perform a complete verification unlock before reporting setup
   complete.

An SD or journal failure does not revoke factory roots. The user may explicitly
reinitialize media and retry. OTP revocation is reserved for the configured
destructive-attempt policy or an explicit lifecycle action.

## Required access policy

The final RP2350 lock manifest must enforce these outcomes:

- Non-secure software: no read or write access to pages 59 or 60.
- Bootloader/PICOBOOT after manufacturing: no read or write access to pages 59
  or 60.
- Secure application firmware before final locking: page-60 read/write access
  for first-setup root creation on empty OTP. After locking: page-60 read only.
- Field firmware: Secure read/write access to page 59, with the OTP service API
  exposing only the one-way row-0 revocation transition. The hardware cannot
  enforce row-level write scope within that page.
- The same application image performs first setup and normal operation. Apply
  permanent root-page permissions only after roots are ACTIVE and verified;
  locking an empty page would prevent first setup.

Page 60 is therefore hard-locked Secure-read-only and inaccessible to
Non-secure software and the bootloader. Page 59 remains Secure-read/write and
is inaccessible to Non-secure software and the bootloader. No OTP access keys
are assigned. The candidate encodings generated from picotool 2.3.0 semantics
are:

| Page | LOCK0 byte | Raw majority LOCK0 | LOCK1 byte | Raw majority LOCK1 |
|---:|---:|---:|---:|---:|
| 59 | `0x00` | `0x000000` | `0x3c` | `0x3c3c3c` |
| 60 | `0x00` | `0x000000` | `0x3d` | `0x3d3d3d` |

For both pages `LOCK_BL=3` and `LOCK_NS=3`. `LOCK_S=0` for page
59 and `LOCK_S=1` for page 60. The three repeated bytes are the RP2350
majority-vote representation. These exact values remain candidates until read,
write, denial and reset behavior pass on sacrificial RP2354A silicon.

After reading roots, Secure early boot derives the required operational keys,
clears the raw root buffer, and applies the strongest compatible soft lock for
the rest of that boot. Root-derived key domains for vault wrapping, journal,
header authentication, storage layers, and future FIDO credentials remain
separate.

## Secure boot and recovery policy

Production V1 is an Arm Secure image with signature enforcement, all debug
disabled, and rollback versioning required from initial version 1 using the two
default RBIT-3 version rows `0x4e` and `0x51`. Slot 0 is the online release
signing identity and slot 1 is a separately controlled offline recovery
identity. Slots 2 and 3 are permanently invalidated.

The USB PICOBOOT and USB mass-storage recovery paths remain enabled. With
secure boot active, accepted executable images must still carry a valid
signature. This preserves recovery through the mux's hardware-default USB-C
route. UART boot is disabled. Secure boot is enabled last, only after both key
hashes, a signed rollback-versioned image, recovery, permissions and debug
policy have been independently verified.

The repository intentionally contains no private signing keys. The two public
key hashes remain null in the machine policy until a key ceremony supplies
them. Running the verifier with `--release` therefore fails today and continues
to fail until every evidence flag is explicitly satisfied.

## Release blockers

- Validate the frozen candidate LOCK0/LOCK1 values on sacrificial silicon.
- Create the release and offline-recovery signing identities and record their
  public-key hashes without storing private keys in this repository.
- Characterize the glitch detector before deciding whether to enable it.
- Test reads, root writes, marker writes, revocation, permission denial, reset,
  and a power cut at every write boundary on sacrificial RP2354A devices.
- Test signed update, rollback rejection and signed USB recovery, then
  independently review the generated per-device manifest before production.
