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

1. **Secret region** — the random 256-bit device secret plus any ECC/redundancy
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

Exact OTP row numbers, redundant copies, ECC choice, access keys, page locks,
secure/non-secure permissions, and revocation encoding are intentionally not
assigned yet. They will be selected from the assembled silicon revision and
current RP2350 datasheet, recorded in a reviewed provisioning manifest, and
verified on sacrificial development hardware before production use.

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
fault injection only. The RP2354A flash adapter, device-secret-derived
dual-family MAC construction, reserved flash addresses, and safe XIP/interrupt
handling still have to be implemented before this state can be used on hardware.

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
