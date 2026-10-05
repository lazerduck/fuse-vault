# AI documentation archive

These are earlier AI-generated design notes, proposals and implementation records.
They are retained for reference and may contain stale status or superseded decisions.
For the documentation currently being reconciled with the implementation, start with
the [project README](../../README.md) and [key handling](../KeyHandling.md).
A proposal or completion claim in this archive is not by itself an accepted current requirement.

## Previous project overview

A fresh start from the existing circuit board design.

FIDO implementation stages, accepted design decisions, validation evidence and
the next action are tracked in the [V2 FIDO delivery ledger](v2-fido-progress.md).
The [browser/passkey-management runbook](v2-fido-browser-testing.md) covers F4 testing.
Current development priorities are tracked in the
[V2 software completion and polish plan](v2-software-polish.md).
For offline screen previews, run `python3 src/host/tools/ui_preview.py` (host C compiler
and Pillow required); no USB connection is needed.

## Source map

All active source is under `src/`:

- [`src/shared/`](../../src/README.md): reusable UI, crypto, storage, security and FIDO libraries.
- [`src/pico/`](../../src/pico/device/README.md): device applications, board drivers and accelerated backends.
- [`src/host/`](../../src/host/CMakeLists.txt): desktop tools, tests and simulated hardware.

See the [source/build guide](../../src/README.md) and [UI scene guide](../../src/shared/ui/README.md).
The main device application is `src/pico/device/`; `v1/` remains an archive.

## V2 source

The [standalone crypto module](../../src/shared/crypto/README.md) contains the cipher interface,
AES-256-XTS and Camellia-256-XTS implementations, ordered pipelines, desktop tests,
and a RAM benchmark. See its README for build instructions and current scope.

The [board benchmark firmware](../../src/pico/benchmark/README.md) adds native four-bit SD,
a laptop-controlled binary USB interface and two-core encryption/storage benchmarks.

The [authenticated storage module](../../src/shared/storage/README.md) adds packed HMAC tags
and unset sectors. See [security-state placement](v2-security-state.md) for
the split between SD metadata and Pico flash/OTP state.

The next-stage [volume format and key lifecycle draft](v2-volume-and-key-design.md)
defines proposed headers, derivations, unlock and credential-change behaviour for review.
The [portable vault module](../../src/shared/security/README.md) implements the four-slot
[VMK envelope](v2-vmk-envelope-proposal.md), create/unlock, encrypted storage,
credential changes and attempt accounting, with desktop lifecycle/failure tests.
The [OTP enrollment and authenticated flash journal](v2-persistent-authority.md)
are implemented for hardware testing; the persistent format is not frozen.

The [security bring-up firmware](../../src/pico/device/README.md) links that lifecycle
to the Pico/SD and adds TRNG/CTR-DRBG diagnostics, unlock timing, and optional
OTP snapshots and guarded provisioning/destruction tools. Persistent enrollment
uses 60,000 PBKDF2 iterations. Physical provisioning and power-cycle verification
are the next board tests. Track release work in the
[production checklist](production-checklist.md).

## Circuit board

- [EasyEDA Pro board project](../../circuit%20board/fuse-vault.eprj2)
- [Hardware evidence and bring-up notes](hardware-bringup-stage4.md)
- [Display connector investigation](display-connector-failure-analysis.md)
- [Extracted display connector evidence](hardware-evidence/u3-project-records.json)

The retained hardware notes are historical records. References to firmware,
tests, and implementation status describe V1; that code now lives under `v1/`.

## V1 reference archive

[The previous project](../../v1/README.md) is preserved under `v1/`, including firmware,
display drivers, tests, tools, provisioning files, documentation, editor settings,
and local build outputs. It is available for reference and selective reuse.

Archived files retain their original contents. Commands, absolute paths, editor
settings, and cached build paths may need updating before reuse. Links to the
board project in the archived README refer to its former relative location;
the board project remains in the top-level `circuit board/` folder.
