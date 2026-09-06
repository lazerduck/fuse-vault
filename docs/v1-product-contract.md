# Fuse Vault V1 product contract

Status: normative implementation target, 2026-09-05.

This document defines the first usable Fuse Vault product. Where older planning
documents conflict with it, this contract takes precedence.

## User-visible lifecycle

### Blank device and blank or foreign SD card

The device presents no USB data interface. It initializes the display and
controls, checks the internal lifecycle state, detects the SD card, and
classifies the card as blank, valid Fuse Vault media, unsupported Fuse Vault
media, or foreign/nonblank media.

Setup requires the user to:

1. confirm initialization of the selected SD card, including destructive loss
   of any existing contents;
2. select one of the supported on-device secret-entry methods;
3. enter and independently confirm the secret;
4. select and order one or more available encryption algorithms;
5. accept the no-recovery and destructive-attempt-limit policy; and
6. wait while the device creates and verifies the vault.

The card is not given a BIOS. Fuse Vault writes a versioned authenticated media
format containing redundant vault metadata and encrypted block storage. Space
for future feature domains is described by authenticated metadata rather than
by exposing additional host-visible partitions.

### Normal boot and unlock

The device validates OTP lifecycle state and the internal attempt journal. A
provisioned device with no card waits with USB locked; insertion initializes
the card and boot continues only after the SD media superblock, redundant vault
header, and recorded encryption stack validate. It fails locked if these
domains disagree. A new device may enter setup without media and retry media
inspection after insertion. The user enters the selected secret locally. The
attempt is durably reserved before verification.

Successful verification unwraps a random volume master key (VMK). The firmware
derives an independent key for every recorded encryption layer, initializes the
encrypted block pipeline, resets the attempt count durably, and only then
attaches USB mass storage. The host sees one ordinary writable block device and
never sees the entry secret, OTP roots, VMK, or layer keys.

### Lock, eject, removal, and fault

Lock first blocks new requests, resolves or fails outstanding requests, syncs
the storage pipeline, detaches USB, and clears plaintext and key-bearing
contexts. SD removal, connector conflict, unrecoverable storage error, or
integrity failure follows the same fail-closed path. USB is not automatically
reattached after recovery; local authentication is required again.

## Encryption-stack contract

The encryption stack is selected by the user during setup and stored as an
ordered, authenticated descriptor. It is not an arbitrary executable plugin.
Firmware contains a registry of reviewed algorithms identified by permanent
numeric IDs and format versions.

V1 permits one to four user-selected layers. Encryption runs from descriptor
index zero upward; decryption runs in reverse order. Each layer receives a key
derived from the VMK using a domain containing:

- the Fuse Vault format version;
- vault ID;
- permanent algorithm ID and algorithm version;
- layer index; and
- purpose (`data-key`, `tweak-key`, or another algorithm-defined purpose).

Duplicate algorithms are permitted only when the algorithm registration says
they are safe. They still receive distinct keys because the layer index is part
of derivation.

The existing Ascon authenticated record remains a mandatory storage envelope
below the selectable stack until replaced by another reviewed authenticated
storage format. Consequently a selectable confidentiality-only layer cannot
silently create unauthenticated storage.

An unavailable, unknown, removed, duplicated-disallowed, parameter-invalid, or
excessively large stack fails locked. Firmware must never replace an unavailable
algorithm with another algorithm or a no-op. Development placeholders can
reserve IDs and exercise UI/error paths, but cannot format or unlock a vault.

Initial implementation targets are:

| Permanent ID | Algorithm | V1 status |
|---:|---|---|
| 1 | Ascon-AEAD128 | Reserved for the mandatory storage envelope; not selectable |
| 2 | AES-256-XTS | Implemented selectable layer |
| 3 | ChaCha20 | Implemented selectable layer |
| 4 | SM4-XTS | Reserved placeholder; unavailable until implemented and reviewed |

The mandatory Ascon-AEAD128 storage envelope is versioned separately and is not
shown as an optional stack entry.

## Media-domain layout

The physical SD device has one Fuse Vault-owned format. It is not a conventional
MBR/GPT disk containing multiple plaintext partitions. The unlocked logical
vault presented over USB may contain a conventional host filesystem.

The authenticated media superblock allocates these internal domains:

1. redundant superblock/header area;
2. encrypted mass-storage data area;
3. reserved FIDO credential area; and
4. reserved metadata/recovery area.

Each domain has an ID, format version, physical start and length, feature flags,
and a distinct key-derivation namespace. Bounds may not overlap. Unknown
required domains fail locked; unknown optional domains are ignored without
being exposed.

The FIDO reservation does not make FIDO credentials visible to USB MSC. A
future CTAP2 implementation will use a separate key hierarchy, authorization
state, retry policy, and USB interface. Vault authentication grants no FIDO
authorization and vice versa.

## Required V1 capabilities

- Physical display and buttons for setup, stack selection, and unlock.
- SD discovery, classification, explicit destructive initialization, raw block
  read/write/sync, removal handling, and media identity binding.
- Versioned stack registry and authenticated stack descriptor.
- Secure random generation, VMK wrapping, independent per-layer derivation, and
  complete session teardown.
- A usable encrypted capacity with a conventional filesystem created by the
  host after unlock, or a documented device-side formatting mechanism.
- USB MSC interoperability, readiness, bounds, sync, eject, reset, suspend,
  resume, and surprise-removal handling.
- Fixed OTP lifecycle manifest and fail-closed provisioning.
- Secure boot/debug/update policy sufficient to prevent trivial extraction of
  OTP roots or plaintext through alternate firmware.

FIDO2 functionality itself is not a V1 storage-release gate, but its domain and
key separation are V1 format requirements so it can be added without
reformatting or weakening an existing vault.

## Explicit non-goals

- Loading user-supplied cryptographic code.
- Silently migrating or weakening an unavailable stack.
- Protecting plaintext after the user exposes the unlocked disk to a malicious
  host.
- Treating placeholder algorithms as encryption.
- Claiming resistance to invasive silicon attacks without supporting evidence.
