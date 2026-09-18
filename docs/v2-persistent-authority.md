# OTP enrollment and flash authority — development implementation

Implemented for hardware testing, with debug retained. Hardware programming and
power-cycle results must be recorded before claiming the physical path verified.
The previous RAM-only bring-up baseline is in `results/security-bringup-20260917.md`.
See [production checklist](production-checklist.md) for the remaining release work.

## Fixed development allocation

| Resource | Allocation | Contents |
|---|---|---|
| OTP root | Page 16, rows 0x400–0x43f | One random 32-byte device root |
| OTP tokens | Pages 17–24, rows 0x440–0x63f | Eight enrollment slots |
| Flash journal | Offsets 0x1fe000–0x1fffff | Two 4 KiB erase banks |
| Image flash | Offsets 0–0x1fdfff | Enforced by linker region override |

Each allocated OTP page uses local rows 0–15 for a 32-byte ECC-encoded secret,
row 16 for a raw completion marker (`0x524f54` root / `0x544f4b` token), row 17 for
raw token revocation and row 18 for raw token activation. Other rows stay zero.
This intentionally uses separate token pages to leave room for later page access
policy. Slots are finite; no re-use of occupied/partially programmed token pages.

These lie in the application area described by
[RP2350 datasheet chapter 13](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
The inspected sample had these pages blank with zero access locks. Every actual
provisioning operation checks the live board again. No application writes target
factory data, page-lock rows, boot policy, access keys or other OTP pages.

## Provisioning and destruction ordering

1. Virgin enrollment requires blank root/token allocation and blank journal area,
   plus supported zero page-lock/software-lock settings. Unexpected state denies.
2. Generate root internally, ECC-write/readback, then write/verify completion marker.
3. Generate token internally in a blank slot, ECC-write/readback, then its marker.
4. Commit EMPTY authority snapshot in flash; only then set token activation row to 1.
5. Vault creation generates the VMK/volume ID, writes SD headers/metadata and commits
   the ACTIVE authority anchor. Enrollment and credential changes use **60,000**
   iterations. The persistent build's trusted bounds are currently exactly 60,000.

Provisioning is an explicit command, never a boot fallback. On a reused board,
old firmware may have left bytes in the newly reserved flash banks. The explicit
`prepare-flash --confirm-device "$DEVICE_ID"` debug command erases only these two
banks, and only if every row of the root and all eight token pages is readable and
blank with supported permissions. It refuses after any partial or complete OTP
enrollment. It verifies erased flash and never programs OTP. An interrupted initial
erase can be retried while those same virgin-OTP conditions still hold. A completed but not
activated token/EMPTY snapshot can resume provisioning. Partial token slots are
consumed and skipped; an incomplete root is a service failure, not overwritten or
silently regenerated. Once any token is activated, a missing journal cannot be
initialized as a fresh counter. No generic erase/reset journal command is exposed.

Destruction first commits DESTROY_PENDING, then advances token revocation row to
`0xffffff`. Any nonzero revocation immediately prevents binding. It then advances
all sixteen token secret rows, in **raw 24-bit mode**, to `0xffffff`, verifying each
raw row. Only after verification does the journal record DESTROYED. Boot recovery
retries an interrupted operation even without SD. A fresh enrollment then selects
the next blank token slot; the root stays unchanged so journal MACs remain valid.

The programming adapter uses the SDK's serialized `rom_func_otp_access`; no custom
SBPI programming sequence is introduced. ROM source shows raw writes reject 1→0
changes and require caller readback verification:
[Raspberry Pi bootrom OTP implementation](https://github.com/raspberrypi/pico-bootrom-rp2350/blob/master/src/main/arm/varm_otp.c).
Actual all-ones behavior and interruption recovery still require silicon testing.
ECC decoding of a filled row is not a destruction certificate: the revocation
marker and verified raw fill, together with the firmware access policy, determine
invalidation. This does not claim resistance to invasive physical recovery.

Development leaves page access permissions open so experiments remain possible.
Do not store actual user secrets on these boards under this build. The release
checklist includes sealing root/Non-secure/bootloader/debug access and adapting
allowed programming permissions without preventing Secure token destruction.

## Device identity and state keys

Public device ID is the first 16 bytes of
`SHA256(L("FV2/device-id/v1") || pico_unique_board_id[8])`.
All `L` labels include the trailing zero byte. It binds to this board's stable
flash identifier; the serial number is not entropy.

```
state_prk = HKDF-Extract(device_id[16], device_root[32])
state_mac_key = HKDF-Expand(state_prk, L("FV2/device-state-mac/v1"), 32)
```

Only this state-MAC key is cached in the authority object. Root/token/binding
scratch is cleared after use. Journal records never contain root, token, VMK or
working encryption keys. Vault binding still requires both root and active token.

## Journal format and recovery

Eight 512-byte records per bank. Each record has a 256-byte body page followed
by a separately programmed 256-byte commit page. Integers are little-endian;
reserved bytes are zero. No C structure is persisted directly.

Body: magic `FV2STATE` at 0; u16 version 1 / length 256 at 8/10; u32 lifecycle at
12; u64 sequence at 16; device ID at 24; volume ID at 40; u64 credential generation
at 56; header SHA-256 at 64; 16-byte policy at 96; u32 attempts/pending/token slot
at 112/116/120; reserved through 223. At 224, full HMAC-SHA256 over
`L("FV2/device-state/body/v1") || body[0:224]`.

Commit: magic `FV2COMIT` at 0; matching u64 sequence at 8; SHA256(body[0:256]) at
16; reserved through 223. At 224, full HMAC-SHA256 over
`L("FV2/device-state/commit/v1") || commit[0:224]`.

Program body → read/compare → program commit → read/compare → success. Commit
errors may be uncertain: RAM sessions remain locked, and a later load determines
whether the new snapshot actually committed. The latest valid sequence wins;
conflicting equal-sequence records deny. Each commit checks its predecessor.

An entirely untouched (all-FF) commit page is an unfinished body and can be
skipped. Any non-FF invalid commit, damaged authenticated body or undecidable
state denies access. When the current bank is full, erase/verify the other bank
while retaining the latest bank, then append the next record. No erase of the
latest bank occurs during that transition.

**Conservative availability limit:** a partly erased older bank can contain
ambiguous invalid commit pages, and recovery then denies even if the other bank
has a valid record. This is deliberately not recovered by choosing an older
counter. Physical interruption tests and servicing UX remain on the checklist.
The tests prove no silent counter rollback for the modeled cuts, not universal
power-fail availability or arbitrary internal-flash anti-rollback.

Pico SDK flash-safe execution pauses the other core and disables interrupts for
program/erase; both cores initialize the lockout protocol. Reads/verification occur
after XIP restoration. Both V2 images reserve the journal region, so normal ELF/UF2
updates do not place program content there. An external whole-chip erase remains
destructive and cannot trigger automatic authority reinitialization.

## Debug workflow

Enable `FV_DEBUG_OTP_INSPECT=ON` and `FV_DEBUG_ENROLLMENT=ON` when building
`firmware/security`. Both options default OFF. The normal internal destruction
callback remains part of authority enforcement even when debug commands are absent.

```sh
python3 tools/security_probe.py --port "$PORT" info
python3 tools/security_probe.py --port "$PORT" state
python3 tools/security_probe.py --port "$PORT" snapshot --out results/otp-pre-provision.json
python3 tools/security_probe.py --port "$PORT" provision --confirm-device "$DEVICE_ID"
python3 tools/security_probe.py --port "$PORT" create --erase-sd --confirm-device "$DEVICE_ID"
# Fully disconnect power, reconnect, then:
python3 tools/security_probe.py --port "$PORT" check --credential original
python3 tools/security_probe.py --port "$PORT" wrong
# Power-cycle and check that attempts == 1 before a successful unlock resets it:
python3 tools/security_probe.py --port "$PORT" state
python3 tools/security_probe.py --port "$PORT" change --credential original
python3 tools/security_probe.py --port "$PORT" check --credential replacement
```

`create` is restricted to a provisioned EMPTY enrollment and writes a public test
pattern over 1 MiB of SD. `check` reads/verifies it. `change` flips between two public
debug credentials while preserving the VMK/payload. Every command ends locked.
`wrong` charges ONE real attempt; the final permitted failed attempt destroys the
current token. It is not a harmless simulation or an unlimited password test.

For the explicit sacrificial-board invalidation experiment:

```sh
python3 tools/security_probe.py --port "$PORT" destroy --confirm-device "$DEVICE_ID"
python3 tools/security_probe.py --port "$PORT" snapshot --out results/otp-post-destroy.json
# Power-cycle, confirm DESTROYED/denied, then provision the next slot if wanted:
python3 tools/security_probe.py --port "$PORT" provision --confirm-device "$DEVICE_ID"
```

Provision/create/destroy require the exact currently connected device ID in the
command. This is a wrong-device guard, not production authentication. There is no
arbitrary row/value programming interface, secret export, page-lock command or
enrolled-counter-reset command. Compile-time removal and replacement with the actual
on-device credential/approval UI are release tasks.

`state` also reports `root_blank`, `tokens_blank` and `flash_blank`: 1 means blank,
0 occupied, -1 unreadable. These expose occupancy only. If OTP is entirely blank
but flash is occupied, run the guarded `prepare-flash` step before `provision`.
Never infer that a generic open failure proves stale flash; inspect these fields.

`state` lifecycle IDs: 1 EMPTY, 2 ACTIVE, 3 LOCKED, 4 DESTROY_PENDING, 5 DESTROYED.
Open result 1 means entirely virgin allocation; 2 means incomplete initial
provisioning without an activated token; negative means failure. A zero open
result says the journal is readable, not that authentication succeeded.

## Validation status

Host tests cover 514 program cut-points, sampled torn bank erases, repeated rollover,
wrong root/device MAC, stale sequence, occupied/incomplete root/token handling,
revoked-first interruption/resume, rotation, missing-journal rejection, and the full
create/write/restart/unlock/change/read pipeline with persisted attempts and old-SD
rejection. Software and mocked Pico SHA paths pass; sanitizer and ARM builds are
checked. The OTP model tests lifecycle/bit monotonicity, not physical ECC/electrical
programming. No physical OTP provisioning was performed during implementation.
