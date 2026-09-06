# RP2354A secret and security-state storage

This document maps the portable storage interfaces to the intended RP2354A
implementation. Host files are test doubles for lifecycle and failure handling;
they are not hardware emulation and are never part of a production vault.

## Device-secret OTP lifecycle

RP2350-family OTP bits are initially zero and may be irreversibly programmed
from zero to one. Firmware must use the supported Boot ROM/Pico SDK OTP access
path, ECC mode where selected by the final layout, and the documented page
permissions. Direct register writes are not part of the application design.

The proposed allocation has three separately protected monotonic regions:

1. **Secret region** — two independently generated 256-bit device roots, one
   permanently assigned to each cryptographic family, plus any ECC/redundancy
   required by the selected OTP representation.
2. **Active marker** — programmed only after every secret row has been read back
   and verified.
3. **Revocation marker** — programmed when destructive lockout occurs and always
   takes precedence over the other two regions.

The interpreted state is:

| Secret rows | Active marker | Revocation marker | State |
|---|---|---|---|
| blank | blank | blank | `EMPTY` |
| complete and valid | programmed | blank | `ACTIVE` |
| any | any | programmed | `REVOKED` |
| any other combination | any | blank | `INVALID` |

`INVALID` is fail-closed. In particular, power loss after partially programming
the secret cannot result in another provisioning attempt writing a different
secret over those rows. `REVOKED` is also permanent: the secret bits may remain
physically present, but trusted firmware refuses to read or use them and access
permissions should make bypass impractical.

The V1 software layout reserves user-data OTP page 60:

| Page-relative rows | Purpose |
|---|---|
| 0–15 | 256-bit SHA-family root, ECC mode |
| 16–31 | 256-bit Keccak-family root, ECC mode |
| 32 | Format marker, ECC mode |
| 33 | Active marker, ECC mode; programmed last |
| 34 | Revocation marker, ECC mode |
| 35–63 | Reserved and left blank |

Pages 61–63 contain Raspberry Pi lock metadata and are explicitly excluded.
Ordinary firmware creates roots during first setup only when the complete
root/revocation layout is empty. Existing active roots are reused; partial,
invalid, revoked and unreadable layouts never trigger regeneration.
Revocation remains available to production firmware because destructive
lockout must be able to set its one-way marker. Reads, writes, and read-back
verification use the Boot ROM OTP API in ECC mode rather than direct register
programming.

The final access keys, persistent page locks, secure/non-secure permissions,
and boot-policy interaction still require a reviewed provisioning manifest and
validation on sacrificial development hardware. No current build programs
those locks.

For host testing, `firmware/host/otp_file.c` implements the same logical row
interface over a persistent 8 KiB file. It enforces bounds, exact image size,
and one-way zero-to-one programming, and makes each row durable independently.
This permits power-cycle and lifecycle tests against the production
`device_roots.c` logic. It does not emulate the physical OTP array, ECC error
correction, access locks, timing, voltage faults, or Boot ROM implementation.

The portable provisioning transaction writes and verifies the authenticated
initial journal record before calling the OTP lifecycle's irreversible root
activation. It then reads the activated roots back and verifies that they still
authenticate the journal. This avoids the recoverable ordering error of
activating OTP first and losing power before any matching journal exists.

At boot, an empty root page selects first-time setup. An active root page is
read through the Boot ROM, converted into the two derived journal keys, and
immediately cleared from the temporary root buffer. The derivations are bound
to a context containing the Fuse Vault journal version and the RP2350's public
unique chip ID. The internal journal must then authenticate and report a
provisioned state; a missing, corrupt, revoked, inaccessible, or ambiguous root
or journal state enters the application fault state without attaching USB.
Once recovered, every attempt-counter command appends and verifies the next
authenticated journal record before the state machine is allowed to begin
password checking. The destructive-lockout command programs and verifies the
revocation marker, clears the derived journal keys, and disables further
journal access for that boot.

The relevant primary references are the [RP2350
datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), the
[Pico SDK OTP access API](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html),
and Raspberry Pi's [picotool OTP warning and permission
workflow](https://github.com/raspberrypi/picotool).

## Mutable internal-flash state

The attempt counter and provisioning state change over the life of the device,
so they do not share OTP rows with the device secret. The portable journal core
now uses exactly two internal-flash erase sectors and 256-byte records. Each
record contains a monotonically increasing sequence number, the preceding
sequence number, vault ID, attempt count, provisioning flag, and two independent
32-byte authentication tags over the record metadata.

Recovery scans both sectors and selects the newest fully authenticated record.
An incomplete record has invalid tags and is ignored. Once the authenticated
bytes and both tags have been programmed, the record is complete even if a
flash API reports failure while processing unused trailing bytes. When both
sectors fill, the sector that does not contain the latest record is erased, so
the previous valid state remains recoverable throughout rotation.

The journal accepts a cryptographic authenticator through a narrow interface.
Host tests use an explicitly non-cryptographic deterministic authenticator for
fault injection only.

The RP2354A backend reserves offsets `0x1fe000` through `0x1fffff`, the final
8 KiB of the 2 MiB stacked flash. A linker assertion prevents the firmware image
from crossing into that range, and backend initialisation repeats the overlap
check at boot. Programming is restricted to aligned 256-byte pages and erasure
to aligned 4 KiB sectors. Both operations run through Pico SDK
`flash_safe_execute`, which disables unsafe interrupt execution and coordinates
with a participating second core when applicable; failure to establish a safe
zone is an I/O failure. Written and erased contents are verified after XIP
resumes. A geometry or overlap failure sends the application to its fail-locked
fault state during boot.

The journal authenticator now derives independent 256-bit operational keys via
SP 800-108 HMAC-SHA-256 counter mode and SP 800-108 KMAC256. It requires both
full-size tags. SHA-256 uses the RP2350 hardware accelerator; KMAC256 uses a
portable SP 800-185 Keccak implementation verified against NIST's published
sample. The RP2354A boot path instantiates this authenticator only after reading
a fully active root set from OTP, then uses it to recover the internal journal.
An empty OTP layout enters first-time setup; every partial, revoked, corrupt, or
otherwise ambiguous layout fails locked.

The host NOR utility checks only alignment, erase-before-rewrite, one-way
programming, and selected torn-program points. It deliberately does not claim to
model RP2354 timing, cache/XIP behaviour, analogue failures, wear, or the actual
flash controller.

## Removable SD storage

The SD card contains redundant authenticated vault headers and encrypted data.
It never contains the device secret or the authoritative attempt journal. The
design assumes an attacker can remove, clone, corrupt, replace, and roll back
the entire card.

## Randomness

Production firmware now has an RP2354A-specific random-fill implementation
using Pico SDK `pico_rand`; on RP2350 this defaults to the hardware TRNG entropy
source. Host tests continue to use the operating system random source. KDF and
key-generation code consume the portable random-fill boundary and do not select
the entropy source themselves.
