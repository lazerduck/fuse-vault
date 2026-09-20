# V2 production checklist

Development continues with diagnostics enabled. This checklist separates **code
implemented**, **hardware verified**, and **release configuration**. An unchecked
item does not mean it blocks the next development step; it means we must not claim
it is finished when preparing a production image. FIDO is outside this milestone.

## Agreed choices — stop reopening these without new evidence

- [x] Four fixed cipher/share slots; AES/Camellia KW shares, outer HMAC, XTS data
  pipeline and sector HMAC. Versioned formats allow deliberate future evolution.
- [x] **60,000 PBKDF2-HMAC-SHA-256 iterations** for enrollment/unlock/credential
  changes. Approximately 1.40 s measured at 150 MHz; persistent development builds
  accept exactly this count. Diagnostic timing commands remain configurable.
- [x] RP2354 dedicated TRNG feeding library AES-256 CTR-DRBG; hardware checks on,
  bounded collection, fresh entropy for each generation request, no PRNG fallback.
- [x] Device-local attempts/header authority; default ten charged attempts and
  enrollment destruction. Valid historical user-data rollback remains acceptable.

## OTP and persistent authority

- [x] Implement fixed development allocation: root page 16, token pages 17–24;
  guard all writes, refuse unexpected contents/permissions, never write boot or
  page-lock fuses through the development interface.
- [x] Implement revoked-first token destruction, all-ones fill of secret rows,
  verification, retry after interruption and advance to a fresh token slot.
- [x] Implement authenticated flash journal, separate body/commit programming,
  verified writes, stale-sequence rejection, two-bank rollover and boot recovery.
- [x] Reserve flash 0x1fe000–0x1fffff in V2 firmware linker layouts; both cores
  participate in SDK flash-safe execution. No default-state fallback on corruption.
- [x] Host failure-injection tests cover the above and the full vault lifecycle.
- [x] Explicit initialization of reserved flash on reused boards, guarded by every
  enrollment OTP row being blank/readable. No reset path after OTP enrollment.
  Physical verification pending; remove this debug command from production.
- [x] On sample 317741A1459A6F94: initialize reserved flash, provision root/token,
  verify expected OTP occupancy/unchanged locks, create/write/unlock/read 1 MiB
  using OTP/flash authority at 60,000 iterations. See
  [hardware record](../results/security-persistence-20260918.md).
- [x] Saved attempt count and all 1 MiB of payload survived a full power cycle and
  diagnostic reflash; delayed worker launch recovered/unlocked/read successfully.
- [ ] Resolve normal automatic startup: USB fails in the normal image but works
  immediately after flashing with explicit delayed worker launch. Cold startup
  of that diagnostic also fails before worker launch; inspect boot selection and
  earlier startup rather than treating delayed launch as a fix. Tagged cold-boot
  checkpoint now localizes the stall to SDK clock initialization before main.
- [ ] Wrong attempts persist across power cycles and SD swaps. Interrupted final
  attempt completes destruction before further authentication, including without SD.
- [x] Physical token destruction completed with raw all-ones readback verified by
  firmware; page 17 alone changed in snapshot fields, locks unchanged, old vault
  denied. Credential change and preserved-payload read also passed.
- [x] Destruction persisted through cold power removal: old vault denied after
  recovery. Advanced to slot 1/page 18 and created/read-verified a fresh 1 MiB
  vault. Only page 18 changed in snapshot fields; root remains usable.
- [ ] Provision next token slot; prove old slot never reused and exhaustion denies
  new enrollment. Cover partial initial root/token provisioning and repair policy.
- [ ] Actual flash interruption/rollover tests, timeout/readback failures, wear and
  lifecycle budget. Current ambiguous torn commits/erases deliberately deny access;
  define servicing/reprovisioning UX without a counter reset or revival bypass.
- [ ] Firmware update/BOOTSEL/erase policy preserves or safely invalidates enrolled
  authority. Restoring old internal flash is outside the SD-only rollback model;
  decide/document any stronger product requirement before claiming it.

## Production access protections

- [ ] Implement and review permanent OTP access policy. Root must not be exposed
  through Non-secure/debug/bootloader paths; token policy must still permit the
  intended Secure destruction operation. The current development adapter requires
  zero lock settings and is not the sealed production adapter.
- [ ] Signed/trusted firmware boot and update policy, debug restrictions, relevant
  silicon/ROM errata review. Storing a secret in OTP alone is not protection while
  arbitrary firmware/debug access remains possible.
- [ ] Secure RAM/access boundaries for VMK/derived keys/root intermediates; verify
  compiler/linker placement, stack/heap ownership and worst-case stack use.
- [ ] Real on-device credential UI and physical approval for policy changes;
  remove public test credentials and host-driven authentication controls.
- [ ] Session/key/plaintext erasure on lock, removal, reset paths and errors;
  check USB and application copies, not just the crypto object's buffers.

## RNG — concrete acceptance work, not endless smoke testing

We rely on the documented RP2350 TRNG design; we are not trying to re-prove Arm's
entropy source by generating many apparently random bytes. More successful smoke
tests establish availability/repeatability, not entropy quality or resistance to
a compromised source. Statistical suites are not a release requirement merely
because they exist. No certification claim is being made.

- [x] Dedicated TRNG driver uses normal checking/conditioning; no `pico_rand` use.
- [x] Library DRBG known-answer tests, source-error/constant/repeated-block tests,
  fresh reseeding and output clearing. Hardware smoke test passed on one board.
- [ ] Verify platform driver handling of error status, timeout and unexpected
  configuration changes: no usable output and no silent fallback/retry-to-success.
- [ ] Confirm reliable startup/collection on representative sample boards and
  supported clock/supply/temperature conditions. This is an integration check with
  a defined product operating range, not evidence of new cryptographic strength.
- [ ] Record final source parameters and reliance on vendor documentation; ensure
  another library/core cannot reconfigure/consume the TRNG unexpectedly.
- [ ] Set production entropy-failure reporting/retry policy. Current debug commands
  may explicitly start a fresh diagnostic after failure; key generation aborts.

Reference: [RP2350 datasheet §12.12](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).
The source design and its configuration guidance support this implementation;
they do not certify the complete assembled device or all firmware integrations.

## Storage and USB release integration

- [ ] USB MSC/SCSI integration, locked/unlocked behavior, flush/eject, disconnect,
  host error reporting and ownership of buffers/requests across cores.
- [ ] Power-loss outcomes for data + metadata writes documented and verified;
  corruption detected without releasing unauthenticated plaintext. No claim of
  atomic multi-sector writes unless explicitly implemented and tested.
- [ ] Run final throughput, latency and memory measurements with production
  firmware configuration and USB path; local device-only rates are not USB rates.
- [ ] Final review of envelope composition, parsers/derivations and lifecycle
  transitions; freeze format/version identifiers and document migration rules.

## Remove development surfaces and package

- [ ] Build production application without `firmware/security`/benchmark command
  dispatchers, `FV_DEBUG_OTP_INSPECT`, `FV_DEBUG_ENROLLMENT`, `FV_DEBUG_STARTUP`, `FV_DEBUG_BOOT_TRACE`, public test passwords,
  test RNGs, raw memory/secret export or factory reset shortcuts.
- [x] Build variants demonstrate inspector and enrollment-command handlers can be
  compiled out. Normal internal final-attempt destruction is a required behavior,
  not a debug command to disable.
- [ ] Inspect final binary/capabilities and test that removed commands are rejected.
- [ ] Pin dependencies, retain notices and meet distribution/relinking obligations
  of the selected Nettle LGPL/GPL licence path; review other bundled licences.
- [ ] Archive per-board enrollment/manufacturing records without secret contents,
  final firmware hash, test results and reviewed release configuration.

### USB mass-storage development follow-up

- [ ] Replace public-fixture CDC unlock/lock with trusted UI/session policy before release.
- [ ] Validate real-host enumeration, lock/unlock, eject, suspend/reset and reconnect.
- [ ] Validate filesystem durability and USB interruption during authenticated writes;
      current writes sync per batch but do not promise atomic multi-sector updates.
- [x] Implement two-buffer USB/worker overlap with 32 KiB buffers; host tests verify
      command completion, read prefetch, error propagation and buffer ownership.
      See [pipeline design](v2-usb-pipeline.md).
- [ ] Measure pipelined MSC latency/throughput on hardware and define worker-fault recovery.
- [ ] Validate host-visible capacity and metadata isolation; no raw SD access.
- [ ] Integrate FIDO HID and physical approvals without allowing storage I/O to approve FIDO.

### Device UI and full-card setup

- [ ] Disable `FV_DEBUG_SCREEN` (framebuffer extraction and remote button presses)
      and `FV_DEBUG_SESSION` (public fixture unlocks); verify rejection in the
      final binary. Physical controls must remain functional.
- [ ] Validate full-capacity create, filesystem format, encrypted file round-trip,
      settings persistence, wrong-sequence accounting and cold reconnect on board.
- [ ] Validate C-only, A-only, both-powered C preference, route changes and loss of
      power; confirm no route change preserves an unlocked storage session.
- [ ] Integrate/test physical TFT output from the shared UI framebuffer; current
      development viewer works with the disconnected display.
- [ ] Finalize credential UX, terminal lockout/recovery and authenticated reset UX;
      current reset consumes one of eight OTP token slots. Document unmounting.

- [ ] Hardware-test lazy bitmap first-write power loss, including a batch crossing
      bitmap sectors; verify SD sync semantics, logical-zero reads before publish,
      HMAC failure on corruption, and existing-volume compatibility.
