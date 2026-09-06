# Fuse Vault implementation roadmap and gap audit

Status: historical gap audit after commit `7f7b9ec` (2026-09-05). Several
“missing” entries below have since been implemented. Use `v1-product-contract.md`
for normative behavior and `product-readiness.md` for current release status.
This document remains useful for detailed threat and validation rationale.

## Audit scope and evidence

This audit covers the portable firmware and RP2354A entry point, host simulators
and all 13 host test targets, the custom Pico SDK board definition, the EasyEDA
Pro project, project documentation, CMake configuration, and the current Git
state. At audit time `main` was clean, matched `origin/main`, and pointed to
`7f7b9ec`; the credential-lifecycle work described by the task was committed in
that revision rather than present as uncommitted changes.

The following checks passed from the repository root:

```sh
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
cmake --build firmware/build
```

The existing configured build directories reported no work to do. The host run
passed 13 of 13 tests, and the existing RP2354A build was current. This verifies
the configured environment, not a clean configure, dependency reproducibility,
hardware behaviour, cryptographic review, or interoperability with a USB host.

Status terms used below:

- **Implemented**: production-path or portable code exists and is compiled for
  the RP2354A target. It may still require hardware validation.
- **Simulated**: a host-only implementation or event injection exercises the
  intended boundary, but does not prove a production backend.
- **Missing**: no implementation exists beyond an interface, command, state, or
  prose design.
- **Hardware-blocked**: meaningful completion requires an exported/reviewed
  schematic, an assembled board, sacrificial silicon, instruments, or a real
  host/device interoperability test.

## Architecture at the original audit

The portable application is an event-driven state machine. It owns navigation,
secret-entry state, attempt-count transitions, and platform commands. Platform
code owns persistence, root access, random generation, display transport,
storage, USB, and long-running cryptography. The host display simulator is the
only command loop that presently connects provisioning and authentication
coordinators end to end. The RP2354A entry point connects GPIO input, OTP root
recovery, the authenticated internal-flash journal, and destructive root
revocation; it deliberately fails a provisioned boot because it has no SD vault
header service.

The durable trust domains are intended to be:

| Domain | Intended contents | Reality at audit time |
|---|---|---|
| RP2354A OTP | Two device roots, format/active/revocation markers | Lifecycle and Pico SDK adapter implemented; access locks and production manifest unresolved |
| Internal stacked flash | Authenticated provisioning state and failed-attempt journal | Portable journal, dual authenticator, linker reservation, and RP2354A flash adapter implemented |
| Removable SD card | Redundant authenticated vault headers and encrypted filesystem/data | Header represented only as an in-memory C structure and host file; physical backend and data format missing |
| Volatile RAM | Entry state, encodings, roots while read, KDF workspaces, VMK, layer keys, plaintext sectors | Several explicit clears exist; complete ownership and compiler/toolchain assurance remain unfinished |
| USB host | No interface while locked; plaintext blocks only while unlocked; FIDO messages in FIDO mode | Application commands and states only; no production USB device stack or descriptors |

## Original end-to-end gap map

| Lifecycle phase | Implemented | Simulated/tested | Missing | Hardware-blocked |
|---|---|---|---|---|
| Power-on and fail-closed boot | OTP state classification, active-root read, journal-key derivation, authenticated journal recovery, app boot/fault transition | Empty/partial/active/revoked OTP states, journal corruption/torn writes, boot-header validation | Production SD/header service; connector arbitration; display init/output; boot self-tests; watchdog/reset-reason policy | Real flash/XIP behaviour, TRNG health, brownout behaviour, display and connector pins |
| First-time setup UI | Four entry methods, independent confirmation, mismatch and no-recovery policy screens | State machine, input controller, framebuffer, terminal/GTK interaction | Production preflight, SD presence/capacity checks, format confirmation, entropy/KDF calibration, verification unlock | Display orientation/driver, buttons, SD detect, user-flow evaluation |
| Provisioning | Credential envelope creation; device-root and initial-journal primitives; host-oriented coordinator with verification and revocation on ambiguous failure | File roots/header/state; injected failures; envelope round trip; separate OTP+journal transaction | One authoritative production transaction; encrypted-volume initialization; redundant serialized header; final verification unlock | Irreversible OTP write/lock validation and power-cut campaign on sacrificial boards |
| Restart/recovery | Journal recovery and portable validated entry-method recovery boundary | Persistent host files and malformed-header cases | SD-backed redundant header selection/authentication and vault/journal identity reconciliation | SDIO backend and power-cut testing on target |
| Attempt reservation | State machine reserves before checking; authenticated append-only journal; RP2354 flash backend | Every journal program boundary, corruption, rotation, restart persistence | Explicit tenth-attempt policy proof across reset/failure; endurance budget and wear response | Real flash timing, XIP/cache, brownout, endurance |
| Unlock/authentication | Canonical encoding, bounded dual KDF/AEAD envelope open, VMK session ownership, reset-before-USB sequencing | Correct/wrong credentials, wrong roots, modified metadata, session clearing, full host coordinator flow | RP2354 command-loop service wiring; target KDF timing/UX; side-channel review | Timing, stack/SRAM use, constant-time leakage and fault-injection assessment |
| Encrypted storage | Generic 512-byte block-device interface only | None for encrypted blocks | On-disk format, SD driver, sector encryption/integrity, metadata, allocation/geometry, nonce/tweak scheme, rollback rules, power-loss recovery | SDIO PIO/electrical bring-up and performance |
| USB storage exposure | `USB_ATTACH_MSC`, detach/eject states and ordering | State-machine command assertions only | TinyUSB/other device stack, descriptors, SCSI callbacks, readiness/eject semantics, encrypted block adapter, connector mux policy | USB-A/USB-C mux, VBUS sensing, signal integrity and OS interoperability |
| Lock/eject/disconnect | State machine requests detach and erasure on Back, lock, eject, and fault | Session-object and state transition clearing | Physical disconnect detection, quiesce/sync protocol, USB detach completion barrier, plaintext cache inventory | Cable removal during writes and host-specific eject behaviour |
| Destructive lockout | One-way OTP revocation path and journal-authenticator deinit | OTP file lifecycle and app tenth-failure states | Precise commit/ack protocol, UI/power-failure semantics, validation that every future boot refuses all secret use | OTP revocation and permission enforcement on real locked silicon |
| FIDO mode | Menu/state and attach/detach commands | State transitions only | FIDO2/CTAP2 implementation, credential store, PIN/user-verification policy, user-presence UI, attestation/update policy, independent key hierarchy | USB HID interoperability and secure physical-presence validation |

The `FIDO_READY` transition does not authenticate the vault secret. That
is acceptable only if the eventual FIDO design deliberately defines its own user
presence/verification policy and separate keys. It must not be mistaken for an
implemented authenticator or inherit a vault-unlocked session implicitly.

## Security invariants

These are release gates, not aspirations:

1. No USB data interface is attached before the selected mode has reached its
   authorization boundary. Boot, setup, provisioning, fault, destroyed, and
   vault-secret states expose no MSC volume.
2. A vault credential is checked only after the incremented failed-attempt value
   is durably authenticated and recoverable after reset. An ambiguous reservation
   consumes an attempt or fails locked; it never grants a free retry.
3. MSC attachment occurs only after successful envelope authentication, a valid
   VMK session exists, and the zero-attempt success record is durable.
4. OTP roots never leave the device through USB, logs, removable media, crash
   output, or a non-secure execution domain. They are used only to derive
   domain-separated operational keys.
5. The human entry encoding is never stored and is never treated directly as a
   storage key. No independent offline password verifier is stored on the SD.
6. The SD card is entirely untrusted. Removing, cloning, corrupting, replacing,
   or rolling it back must reveal no plaintext and must not bypass attempt state.
7. Every persistent structure has a canonical byte serialization, magic,
   version, lengths, bounds, integrity/authentication coverage, and explicit
   compatibility/rejection rules. Native C structure layout is never an on-media
   format.
8. Every storage write either recovers the last authenticated state or a fully
   authenticated next state after arbitrary power loss. Ambiguity never selects
   unauthenticated or partially initialized data.
9. Nonces and tweaks cannot repeat under a key, including after rollback,
   interrupted writes, media replacement, sequence exhaustion, or reformat.
10. Lock, eject, cable loss, fatal error, mode exit, and destructive lockout stop
    new plaintext I/O, quiesce or fail outstanding I/O, detach USB, and clear all
    applicable transient secrets. Detach ordering must be specified and tested.
11. Revocation overrides every other OTP state. Once committed, all supported
    firmware and boot paths permanently refuse root reads and vault/FIDO use.
    “Destroyed” means cryptographic inaccessibility, not that programmed root bits
    have physically become zero.
12. Vault and FIDO keys, authorization state, persistent records, and USB modes
    are domain-separated. Authorization in one mode grants nothing in the other.
13. Missing, unknown, out-of-range, downgraded, inconsistent, or unauthenticated
    metadata fails locked without fallback defaults.
14. Ordinary builds cannot program fresh OTP roots. Provisioning builds and
    irreversible security configuration require explicit, auditable ceremony.

## Original sensitive-data ownership and zeroization audit

| Value | Owner and intended lifetime | Required destruction boundary | Current gap |
|---|---|---|---|
| Interactive entry state | `fv_app_t.secret_entry` or `setup_secret_entry`, only during entry/setup | Back/cancel, comparison, command completion, fault, reset | Portable clears exist; whole-app reset and compiler/toolchain guarantees need tests/review |
| Canonical entry encoding | Coordinator workspace, only during envelope create/open | Every return path | Volatile clearing is implemented and unit-tested in coordinators |
| Raw device roots | Provisioning/authentication/boot local workspace | Immediately after deriving or using operational keys | Clears exist; stack-copy audit, exception/reset behaviour, and secure/non-secure isolation unresolved |
| KDF/AEAD intermediate state | Credential-envelope implementation stack/workspace | Every success and failure return | Selected outputs are tested; full primitive/context copy audit and optimized-build disassembly review remain |
| VMK during provisioning | Provisioning workspace | After header initialization and verification | Coordinator clears it, but encrypted-volume creation does not exist yet |
| Unlocked VMK | `fv_authentication_session_t` owned by platform command loop | Lock, eject, disconnect, mode exit, fault, destruction, reset | Host display simulator owns/clears it; RP2354 has no session integration |
| Derived storage keys | Future encrypted-block session | Same as VMK, plus key-rotation/profile exit | Missing |
| Plaintext sectors/caches | Future encrypted adapter, USB transfer buffers, filesystem/MSC stack | After transfer; all buffers on detach/fault | Missing and must include DMA/PIO/USB buffers |
| Journal operational keys | Boot-lifetime dual journal authenticator | Revocation, fault, reset; may remain while locked for attempt accounting | Deinit exists on destructive command; broader fault-path ownership needs consolidation |
| FIDO private/working keys | Future independent FIDO service | Per-operation/session policy and destruction | Missing |

All sensitive clears should converge on one reviewed, compiler-resistant primitive.
Tests should assert clearing on every state transition and injected failure, while
target release builds should verify that optimization has not removed erasure.
Do not promise erasure of flash/OTP remnants that the hardware cannot erase.

## Persistence and power-loss contract

### OTP roots

The interpreted states `EMPTY`, `ACTIVE`, `REVOKED`, and `INVALID` are already
encoded in the portable lifecycle. Root rows must be fully programmed and read
back before the active marker. Revocation has precedence and must be read back.
Partial root or marker programming is permanently invalid and fails locked.

### Internal attempt journal

Two 4 KiB sectors contain append-only 256-byte records with sequence linkage,
vault binding, provisioning state, failed attempts, and two tags. A new record is
authoritative only after complete authentication. Rotation must retain the latest
valid old record until the new sector has a valid record. Sequence exhaustion,
full-media behaviour, unexpected erased/programmed patterns, and I/O errors fail
locked. The journal must be bound to the same vault ID as the accepted SD header.

### SD header and data

The production format is not yet specified. It must define at least two header
slots, canonical serialization, authenticated sequence/epoch selection, vault ID,
entry encoding version, crypto profile and parameters, wrapped VMK, data geometry,
integrity roots, and feature flags. A write protocol must define flush barriers
and which old copy remains authoritative at each cut point. The design must state
what SD rollback is detectable by internal state and what remains an accepted V1
limitation.

Encrypted data needs a sector or extent authentication scheme with explicit
freshness. In-place AEAD alone cannot distinguish an old valid sector. The chosen
tree/log/COW design must cover torn data and metadata writes, host write caching,
USB sync/eject, SD reordering, and recovery without nonce reuse.

### Provisioning transaction conflict to resolve

There are two partial transaction implementations:

- `device_provisioning.c` creates the initial authenticated journal before OTP
  activation and verifies it again using read-back roots, but does not create a
  credential envelope, SD header, or encrypted volume and is not UI-dispatched.
- `provisioning_coordinator.c` creates roots, the credential envelope and a host
  header, then publishes host security state; on later failure it revokes roots.
  Its service shape is exercised end to end by the host simulator, but it is not
  the production OTP+journal+SD transaction.

These must become one explicit state machine before production provisioning is
connected. A safe production order must prepare and verify all reversible SD
state, prepare an authenticated journal record, activate OTP only at the final
irreversible boundary, re-read every domain, perform a verification unlock, and
publish “provisioned” last. Every power cut must map to pristine/retryable,
complete, or permanently revoked/invalid—never an apparently usable half-vault.

## Board and production assumptions requiring closure

The EasyEDA Pro project is a binary SQLite project, so it is not review-friendly
in Git and was not sufficient by itself for a net-by-net audit in this environment.
Commit deterministic PDF schematic, netlist, BOM, PCB plots/renders, design-rule
reports, and revision identifiers before firmware pin claims are treated as
verified.

Open assumptions and checks:

- Confirm the RP2354A package, exact stacked-flash part/geometry, boot-stage read
  mode, JEDEC behaviour, and that the final 8 KiB reservation is correct.
- Verify all board-header GPIO aliases against exported nets and the assembled
  board. Revision 1 shorts each presence net to GPIO2/GPIO16 or GPIO3/GPIO17;
  GPIO16/17 must remain high-impedance and the electrical contention risk must be
  reviewed, not merely documented in firmware.
- Confirm the remaining USB presence assertion levels, power-path diode
  behaviour, back-power prevention, simultaneous A/C attachment policy, VBUS
  divider thresholds, ESD parts, differential routing, and which connector can
  source power. The FSUSB42 OE/SEL truth table and SEL-low USB-C mapping are now
  recorded.
- Confirm SD is four-bit SDIO (the top-level README still says SPI), voltage and
  pull-up requirements, card-detect polarity, PIO pin constraints, signal
  integrity, and hot-removal behaviour.
- Confirm display controller identity, reset/backlight levels, SPI limits,
  80x160-to-160x80 rotation, colour order, and framebuffer transfer strategy.
- Verify navigation switch polarity, external pull-ups, debounce assumptions,
  boot-strapping conflicts, and duplicate presence pins at reset.
- Define SWD/debug/test-point policy, secure/non-secure partition, OTP page locks
  and keys, secure boot signing, anti-rollback, firmware update/recovery, RMA, and
  manufacturing ownership. No current build programs final locks.
- Validate TRNG startup/continuous health handling, brownout detector settings,
  watchdog policy, reset causes, clocking, decoupling, and power-loss energy/time
  available to finish or abandon writes safely.

## Sequential implementation stages

Each stage is intentionally small. Do not start irreversible hardware security
configuration until the preceding portable formats and transactions are stable.

### Stage 0 — Freeze the lifecycle contract

Deliver a versioned lifecycle/state-transition specification and reconcile the
two provisioning coordinators. Define canonical persistent byte formats, commit
points, reset outcomes, error taxonomy, and ownership/zeroization tables.

Acceptance criteria:

- One production provisioning state machine covers SD preparation, journal, OTP,
  verification unlock, and final publication without contradictory ordering.
- A table enumerates every injected failure boundary and its permitted restart
  state.
- Header and journal identity/version rules reject all mismatches and unknowns.
- Security review explicitly approves destructive denial-of-service semantics.

Verification:

```sh
rg -n "provision|commit|power|sequence|vault_id|zero" docs firmware/include
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

### Stage 1 — Canonical redundant vault-header store — Implemented

Status (2026-09-05): implemented as a portable 256-byte canonical record in
two 512-byte block-device slots. The record uses explicit little-endian fields
and a domain-separated HMAC-SHA-256 derived from both device roots. Host file
storage now exercises this same store for provisioning, boot recovery, and
authentication across process-style restarts. Golden-format, malformed-field,
identity, invalid-tag, sequence, short-I/O, range, sync-failure, and exhaustive
byte cut-point recovery tests are in `fuse_vault_vault_header_store` and
`fuse_vault_persistent_vault`.

Implement a portable byte serializer/parser and two-slot header store over the
existing block-device interface, plus a host file/memory block device. Keep the
current credential envelope as a payload, but add explicit magic, format and
encoding versions, total length, slot sequence, vault ID, and authenticated
coverage. Do not implement SDIO or USB yet.

Acceptance criteria:

- No native struct is written directly; all integer encoding and bounds are
  explicit and round-trip across a fixed golden byte vector.
- Unknown versions/profiles/methods, duplicate or wrapped sequences, bad lengths,
  vault-ID mismatch, invalid tags, short I/O, and out-of-range blocks fail closed.
- Recovery chooses only the newest fully authenticated slot and retains the old
  valid slot throughout update.
- Exhaustive cut-point tests interrupt every write byte/block and recover either
  the old or new authenticated header, never a hybrid.
- Boot recovery and credential authentication use this store in a persistent
  host end-to-end restart test.
- All workspaces and caller outputs are zeroed after every failure.

Verification:

```sh
cmake -S firmware/host -B firmware/build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

This is the safest highest-value next stage: it closes the exact boundary that
currently forces provisioned hardware to fault, establishes the first real
on-media contract, and can be fully fault-injected on the host without claiming
anything about SDIO, USB, or irreversible OTP writes.

### Stage 2 — Encrypted data-format prototype — Implemented

Status (2026-09-05): implemented as the portable encrypted-block adapter and
canonical format specified in `docs/encrypted-data-format-v1.md`. V1 uses
ASCON-AEAD128 with vault/address/generation/session metadata as associated data,
domain-separated VMK-derived encryption and nonce keys, fresh random writer
epochs, and two 1 KiB copy-on-write slots per 512-byte logical block. The
dedicated host target covers golden derivation/media values, corruption,
reordering and cross-vault substitution, truncation, file persistence, teardown,
a 1,000-operation randomized model, and every byte cut of a replacement write.
The deliberate V1 limitation is recorded: wholesale rollback of both valid
sector copies is not detectable until a trusted freshness root is integrated.
Clean Debug host configuration/build passed all 16 CTest targets; the dedicated
target also passed AddressSanitizer and UndefinedBehaviorSanitizer (leak checking
is unavailable under the traced runner). A clean Release RP2354A configuration
and firmware link passed with Pico SDK 2.3.0. Whole-tree sanitizer compilation
currently stops on two pre-existing `-Wconversion` diagnostics in
`host/otp_file.c`; this does not affect the instrumented encrypted-block target.

Select and document the V1 sector/extent encryption, integrity, freshness, and
copy-on-write metadata design. Implement it as a portable block-device adapter
over an in-memory/file-backed untrusted device.

Acceptance criteria:

- Golden vectors cover key derivation, addressing/tweaks/nonces, ciphertext and
  tags; every purpose uses a unique domain label/key.
- Bit corruption, reordering, substitution across vault/block, truncation, and
  supported rollback cases return integrity errors without plaintext release.
- Arbitrary write cut points recover a defined old/new filesystem view without
  nonce reuse.
- Lock/fault clears keys and plaintext buffers; reads and writes then return not
  ready.
- Capacity/overhead and RP2354 SRAM/stack/performance budgets are recorded.

Verification uses the Stage 1 commands plus dedicated encrypted-block known-answer,
tamper, randomized-model, and cut-point CTest targets.

### Stage 3 — Complete host lifecycle and virtual MSC boundary — Implemented

Join the authoritative provisioning transaction, redundant header store,
encrypted block adapter, attempt journal, authentication session, sync/eject,
and a host-side SCSI/MSC request harness. No host directory may act as a plaintext
fallback volume.

Acceptance criteria:

- A fresh simulated device provisions, restarts, rejects wrong entries with a
  persistent count, unlocks, reads/writes encrypted media, syncs, locks, and
  restarts with intact data.
- Raw media searches reveal no supplied plaintext or key material.
- USB/MSC requests before unlock or after detach cannot reach plaintext blocks.
- Removal at every provisioning, authentication, write, sync, lock, and destroy
  boundary has a specified tested outcome.
- The tenth reserved attempt produces permanent simulated revocation across
  restart and replacement of the SD image.

Verification uses full CTest plus scripted lifecycle and raw-image inspection
tests committed to the host suite.

Implementation evidence (2026-09-05):

- `lifecycle_msc_tests.c` provisions and restarts a fresh host device for every
  entry method, persists a rejected attempt, unlocks, writes/reads/synchronizes
  encrypted blocks through the virtual SCSI/MSC boundary, ejects, locks, and
  reopens the same ciphertext after a further authentication.
- The removable image is one raw block device: two authenticated header slots
  followed by a bounded data slice. There is no plaintext directory or volume.
  The lifecycle test scans the image for the complete supplied plaintext block,
  both device roots, and VMK and rejects any match.
- Host security state now uses the same 8 KiB, two-sector, append-only,
  dual-authenticated journal as the portable design. The host service test
  corrupts the newest record and proves authenticated fallback. The lifecycle
  test reserves attempts 1 through 10 before checking, revokes at 10, replaces
  the complete removable image, restarts, and still refuses root access and
  authentication.
- `virtual_msc.c` is the host request boundary for READ(10), WRITE(10),
  SYNCHRONIZE CACHE, and START STOP/eject. It only accepts an explicitly attached
  ready encrypted-block interface. Before attach, once requests are blocked,
  after detach, after media removal, and after adapter lock/fault it returns not
  ready without invoking a plaintext block operation.

Host failure/removal outcomes are:

| Boundary | Injected condition | Required/tested outcome |
|---|---|---|
| Provisioning, before roots | random/status/write failure | pristine or fail-locked; no MSC |
| Provisioning, after roots | header/state/read-back failure or lost acknowledgement | simulated roots revoked; no MSC |
| Header slot update | every write cut, invalid newest slot | older authenticated slot or failure; never partial data |
| Journal append/rotation | every program boundary, corrupt newest record | prior authenticated count or fail-locked |
| Authentication | wrong method/secret, missing/replaced media, root read failure | rejected/fatal, empty session, no MSC |
| Encrypted write | every byte cut, media loss, sync failure, tamper | authenticated old/new block or error; no unauthenticated plaintext |
| Sync/eject | backing sync failure or removal | error/not-ready, new requests blocked before detach |
| Lock/fault | request blocking followed by detach and key clear | later requests not-ready and adapter/session zeroed |
| Destruction | tenth durable reservation, restart, SD replacement | root revocation has precedence permanently |

The reconciled lifecycle uses `fv_device_runtime` as its single target and host
coordinator. It composes publish-last setup, authentication, redundant media
metadata, the encrypted block adapter, MSC attachment and fail-closed teardown.
Factory OTP activation is kept distinct from user-vault setup, so removable
media failure cannot accidentally consume or revoke factory identity. Missing
media at boot is a recoverable locked wait state; inserted media is fully
authenticated before password entry becomes available.

Verification evidence (2026-09-05): the native suite passes 25/25 CTest targets,
including the actual TinyUSB MSC adapter behind a fake TinyUSB/device backend;
the same 25/25 pass under AddressSanitizer and UndefinedBehaviorSanitizer, and a
GCC `-fanalyzer` build completes without a finding. LeakSanitizer alone is
unsupported under the test runner's ptrace environment and was disabled. Both
the conservative RP2354A image and a fully enabled candidate peripheral image
compile and link. `git diff --check` passes. These checks are software evidence,
not substitutes for assembled-board, power-cut, or irreversible-OTP evidence.

### Stage 4 — Board artifacts and non-secret peripheral bring-up — Partial

Export reviewable hardware artifacts, resolve the pin/protocol discrepancies,
then implement display transport, connector detection/mux safety, SD card detect,
and raw SDIO block access without provisioning OTP.

Acceptance criteria:

- Schematic/netlist/BOM/PCB checks are peer-reviewed and board revision is tied
  to firmware configuration.
- At reset and fault, the USB mux is disabled and duplicate GPIOs are high-Z.
- Display test pattern, every button, both connector-presence inputs, card detect,
  and bounded raw block read/write/sync pass on a disposable card.
- SD removal and I/O faults propagate to the application fault/lock path.

Verification requires a documented bench checklist, logic-analyser traces where
relevant, and repeated read/write/verify tests on assembled hardware.

Implementation evidence (2026-09-05):

- `docs/hardware-bringup-stage4.md` records the immutable EasyEDA source hash,
  recoverable project/branch/board/schematic/sheet/PCB identifiers, exact export
  requirements, fail-closed decisions, and a disposable-media bench procedure.
  The current database stores the actual design in encrypted history blobs, so
  it cannot substantiate nets, part identities, polarities, or truth tables.
- Portable display initialization/presentation and change/rate scheduling are
  implemented in `display.c`; a guarded ST7735S target transport now exists.
- `peripheral_safety.c` forces mux-disable before presence-input setup, rejects
  simultaneous connector presence, routes only one observed connector, and
  disconnects/faults on a routed presence change. The RP2354 connector backend
  records the confirmed FSUSB42 control levels and USB-C ROM-flashing default
  while presence polarity remains gated. Its removable raw-block guard makes
  card loss and I/O/not-ready
  failures sticky and propagates them to application fault/lock handling.
- `rp2354_sd.c` provides a CRC-checked SPI-mode SD baseline on the routed
  CLK/CMD/DAT0/DAT3 signals, including capacity discovery and insertion,
  removal, transport-failure and sync lifecycle signals. Four-bit PIO SDIO is a
  throughput optimization, not a prerequisite for the V1 storage abstraction.
- The Stage 4 host target tests initialization failure, display failure and
  scheduling, mux-disable ordering, connector conflict, card removal, raw I/O
  failure, one-shot fault propagation, USB detach, and session-key erasure. A
  clean temporary host build passes all current CTest targets.

Hardware-blocked acceptance criteria: peer-reviewed schematic/netlist/BOM/PCB
and revision reconciliation; proof of reset-level mux disable and duplicate-pin
high impedance; target display test patterns and button/connector/card-detect
tests; bounded card read/write/sync; logic-analyser
captures; and removal/fault testing on an assembled board. None is claimed by
the host tests.

### Stage 5 — Production SD/header and encrypted-volume integration — Implemented, bench validation pending

Connect Stages 1–3 to the RP2354 SD backend while OTP provisioning remains
disabled. Use injected/development roots through an explicitly non-production
build path if target profiling needs keys.

Acceptance criteria:

- Provisioned header recovery no longer uses the deliberate `NULL` service and
  remains fail-closed for absent/corrupt/wrong cards.
- Target memory maps prove firmware, journal, stacks, DMA/PIO buffers, and crypto
  workspaces do not overlap or exceed budgets.
- Power cuts and card removal during redundant header/data updates recover only
  specified states over a statistically meaningful campaign.
- KDF latency, storage throughput, and UI responsiveness meet recorded limits.

The code path is integrated: initial setup validates/initializes media,
persists redundant authenticated metadata, reserves future-FIDO/recovery
domains, creates an encrypted logical disk, and recovers it after restart. The
remaining criteria require real SD cards, timed target measurements, and
power-interruption hardware tests.

### Stage 6 — USB MSC exposure and lock sequencing — Implemented, interoperability pending

Add a USB device implementation backed only by the unlocked encrypted adapter.
Specify connector selection and simultaneous-attachment behaviour.

Acceptance criteria:

- No descriptor/data interface is exposed while locked if that remains the final
  privacy policy; otherwise any minimal locked descriptor is explicitly reviewed.
- MSC attach requires durable successful authentication and a valid key session.
- SCSI bounds, readiness, write-protect, flush, eject, reset, suspend/resume, and
  malformed-request handling are tested by a host harness and multiple OSes.
- Lock first blocks new I/O, resolves outstanding writes per policy, syncs, detaches,
  clears all USB/plaintext/key buffers, and only then returns to mode selection.
- Surprise cable removal leaks no later plaintext and causes no nonce reuse.

TinyUSB descriptors and MSC callbacks now sit only above the unlocked encrypted
block device. Read, partial-write, sync, logical eject, readiness and failure
signaling have direct host tests. The target loop turns any backend failure into
USB detach and session erasure. Electrical connector routing and multi-OS USB
interoperability remain bench release gates.

### Stage 7 — Destructive lockout — Policy/tooling implemented, sacrificial validation pending

Finalize OTP page allocation, access keys/locks, secure/non-secure permissions,
manufacturing manifest, and destructive command protocol. Enable it only in a
controlled sacrificial build.

Acceptance criteria:

- Independent review approves the exact OTP map and generated manifest.
- First setup creates roots only in an empty OTP layout using the same firmware
  as normal operation. Once page 60 is locked read-only, revocation remains the
  only field-programmable lifecycle transition required by policy.
- Power interruption at every programmable row/marker yields only active,
  invalid/fail-locked, or revoked states as specified.
- Debug/non-secure/boot paths cannot read roots or bypass revocation after locks.
- Tenth-attempt acknowledgement and UI remain truthful under ambiguous failures.

The OTP allocation and permissions are machine-readable, checked against the
documented manifest, and consumed by a fail-closed signed-release packager.
The ordinary build provisions empty roots during first setup. Exact lock words and
power-cut behavior still require independent review and sacrificial silicon.

### Stage 8 — Secure boot, update, and release hardening — Partial

Define signing custody, anti-rollback versioning, recovery/update UX, debug
lockdown, reproducible release builds, dependency provenance, and security test
evidence before valuable-data use.

Acceptance criteria:

- Only authorized, non-rollback firmware boots; failed update/recovery never
  exposes a vault or bypasses revocation.
- Release artifacts are reproducible or have a documented variance/provenance
  process, with SBOM and pinned Pico SDK/crypto sources.
- Static analysis, sanitizers on host, fuzzing of parsers/state machines, stack
  analysis, optimized zeroization inspection, and independent crypto/security
  review have no unresolved release-critical findings.

Release CMake gates, version/VID/PID checks, two signing-key roles, rollback and
signed-recovery policy, staged picotool OTP inputs, artifact hashes and a receipt
are implemented. Real keys, reproducibility/SBOM work, parser fuzzing, target
measurements, sacrificial secure-boot trials and independent review remain.

### Stage 9 — FIDO2 as an independent product slice

Begin only after storage and device lifecycle are stable. Specify CTAP2/WebAuthn
scope, resident/non-resident credential storage, user presence and verification,
PIN retry policy, attestation, reset, backup/sync policy, and firmware-update
interaction. Use separate derivation domains and records from the vault.

Acceptance criteria:

- Relevant FIDO conformance/interoperability tests pass on supported operating
  systems and browsers.
- Vault unlock never unlocks FIDO and FIDO verification never unlocks storage.
- FIDO operations cannot access vault keys/blocks; vault operations cannot access
  FIDO private keys.
- Lockout, reset, provisioning, update, and destruction interactions are explicit,
  power-loss tested, and accurately presented to the user.

## Deferred and out-of-scope claims

Selective keyboard output is a post-V1 disclosure feature and should not share
the critical path with encrypted storage or FIDO. Invasive cloning of internal
flash, sophisticated side channels, fault injection, supply-chain compromise,
and destructive denial of service are not solved merely by the current journal
and OTP design. Any remaining exclusions must be stated in the threat model and
user documentation rather than implied to be protected.

## Immediate execution order

The portable V1 architecture and target composition are complete enough for
board bring-up. The next work is evidence-driven: first validate display, SD
and USB presence/routing on an assembled board; then run storage/USB/KDF and
power-cut campaigns; then exercise the frozen OTP/secure-boot flow on
sacrificial devices; finally obtain independent security review and release
credentials. FIDO2 remains an isolated post-V1 product slice and does not alter
the encrypted-storage format or key namespace.
