# Fuse Vault

A fresh start from the existing circuit board design.

## V2 source

The [standalone crypto module](src/crypto/README.md) contains the cipher interface,
AES-256-XTS and Camellia-256-XTS implementations, ordered pipelines, desktop tests,
and a RAM benchmark. See its README for build instructions and current scope.

The [board benchmark firmware](firmware/bench/README.md) adds native four-bit SD,
a laptop-controlled binary USB interface and two-core encryption/storage benchmarks.

The [authenticated storage module](src/storage/README.md) adds packed HMAC tags
and unset sectors. See [security-state placement](docs/v2-security-state.md) for
the split between SD metadata and Pico flash/OTP state.

The next-stage [volume format and key lifecycle draft](docs/v2-volume-and-key-design.md)
defines proposed headers, derivations, unlock and credential-change behaviour for review.
The [portable vault module](src/security/README.md) implements the four-slot
[VMK envelope](docs/v2-vmk-envelope-proposal.md), create/unlock, encrypted storage,
credential changes and attempt accounting, with desktop lifecycle/failure tests.
The [OTP enrollment and authenticated flash journal](docs/v2-persistent-authority.md)
are implemented for hardware testing; the persistent format is not frozen.

The [security bring-up firmware](firmware/security/README.md) links that lifecycle
to the Pico/SD and adds TRNG/CTR-DRBG diagnostics, unlock timing, and optional
OTP snapshots and guarded provisioning/destruction tools. Persistent enrollment
uses 60,000 PBKDF2 iterations. Physical provisioning and power-cycle verification
are the next board tests. Track release work in the
[production checklist](docs/production-checklist.md).

## Circuit board

- [EasyEDA Pro board project](circuit%20board/fuse-vault.eprj2)
- [Hardware evidence and bring-up notes](docs/hardware-bringup-stage4.md)
- [Display connector investigation](docs/display-connector-failure-analysis.md)
- [Extracted display connector evidence](docs/hardware-evidence/u3-project-records.json)

The retained hardware notes are historical records. References to firmware,
tests, and implementation status describe V1; that code now lives under `v1/`.

## V1 reference archive

[The previous project](v1/README.md) is preserved under `v1/`, including firmware,
display drivers, tests, tools, provisioning files, documentation, editor settings,
and local build outputs. It is available for reference and selective reuse.

Archived files retain their original contents. Commands, absolute paths, editor
settings, and cached build paths may need updating before reuse. Links to the
board project in the archived README refer to its former relative location;
the board project remains in the top-level `circuit board/` folder.
