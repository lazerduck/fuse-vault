# FIDO2 reuse assessment and integration plan

Assessment date: 2026-09-06. Status: proposed implementation plan; FIDO2 remains disabled.

## Recommendation

Use **CanoKey Core as the provisional protocol implementation**, behind Fuse Vault
adapters. Make the first implementation milestone prove a combined RP2354A link,
crypto correctness, and responsive user-presence handling before committing to it.
Keep **Pico FIDO as the alternative** if its licensing is selected and its platform
framework proves cheaper to adapt. Neither is a drop-in dependency.

CanoKey's Apache-2.0 core and explicit platform interfaces favour maintaining Fuse
Vault's existing runtime and lifecycle. Pico FIDO has the stronger demonstrated
hardware fit, but brings its own application entry point, USB ownership, flash
filesystem and OTP initialization. Fuse Vault has not selected a project licence;
Pico FIDO's current top-level licence is AGPLv3, and its README offers a commercial
licence. Some source headers still say GPLv3. Resolve the exact selected revision's
licences and dependency notices before importing it; do not assume old licence
summaries apply. This assessment does not select Fuse Vault's project licence.

## Source revisions inspected

Sources were cloned with recursive submodules into disposable `/tmp` checkouts.
No upstream firmware was flashed, no OTP was programmed, and no dependency was
added to the production build.

| Source | Inspected commit |
| --- | --- |
| [CanoKey Core](https://github.com/canokeys/canokey-core/tree/ad71b0f506d40c7502300ac57239ccb0833c2504) | `ad71b0f506d40c7502300ac57239ccb0833c2504` |
| CanoKey crypto | `b9518de9959772bb92a720f2eb56b2dc7544b80c` |
| CanoKey TF-PSA-Crypto | `0ef26791ceb1445b0563c40b8e2c073000245a7d` |
| [Pico FIDO](https://github.com/polhenarejos/pico-fido/tree/09d95a469b3ca1142bb04b505b49ab77eba6e964) | `09d95a469b3ca1142bb04b505b49ab77eba6e964` |
| Pico Keys SDK | `263b2a9839acb3a16936199ca5dbb814e8bf16e1` |
| Pico FIDO fetched Mbed TLS | `068ff080b369adfac81509f9b57b2afabaf82dc5` (requested tag `v3.6.7`) |
| Pico FIDO fetched TinyCBOR | `c0aad2fb2137a31b9845fbaae3653540c410f215` (requested tag `v0.6.1`) |

These are assessment snapshots, not endorsed release versions. Before adoption,
select a maintained release or audited commit, inventory all transitive licences,
review relevant security fixes and pin the entire dependency graph. Current heads
advertise a broader CTAP feature set than the initial Fuse Vault scope needs.

## Findings from source inspection

| Boundary | CanoKey Core | Pico FIDO | Fuse Vault integration |
| --- | --- | --- | --- |
| Build | C11 static library, but CMake globs multiple applets even with extra USB interfaces disabled | Standalone executable importing Pico Keys SDK | Maintain an explicit source/feature list; do not import either application's full startup path |
| USB | Reusable `CTAPHID_Init`, `CTAPHID_OutEvent`, `CTAPHID_Loop`; framing still calls `USBD_CTAPHID_*` helpers | Already uses TinyUSB; SDK owns descriptors, callbacks and USB processing | One USB owner, mode-specific descriptors and a bounded report queue |
| Persistence | `include/fs.h`: files, offsets, attributes, rename and removal over LittleFS | `file_t`, `file_search_by_fid`, resident containers and SDK flash management | A private FIDO filesystem/store over authenticated encryption; a raw block slice alone is insufficient |
| Crypto | Current backend uses TF-PSA-Crypto and private Mbed TLS headers; CMake applies an Ed25519 patch | SDK fetches Mbed TLS 3.6.7; optional EdDSA fork | Fuse Vault SDK currently supplies Mbed TLS 3.6.6 with a small symmetric-only config; choose one compatible backend |
| Presence | `wait_for_user_presence` loops for up to 30 seconds, services CTAPHID and consumes touch events | Physical presence hooks and SDK scheduling | Poll USB, buttons, display and connector/media safety throughout waits; prevent nested command execution |
| Verification | ClientPIN/token implementation; direct `options.uv=true` currently rejected in makeCredential/getAssertion | GetInfo advertises ClientPIN; UVM code reports external passcode | Neither supplies Fuse Vault's desired local verification flow unchanged |
| Lifecycle | Applet initialization/reset and persistent secrets belong to the core's storage model | SDK startup calls OTP initialization and scans its flash store | Fuse Vault remains owner of boot, provisioning, roots, revocation and session clearing |

CanoKey source anchors: `CMakeLists.txt`, `include/device.h`, `include/fs.h`,
`src/device.c`, `interfaces/USB/class/ctaphid/ctaphid.c`,
`applets/ctap/ctap.c`, `scripts/gen_ctap_get_info.py`, and
`canokey-crypto/CMakeLists.txt` / `src/ecc.c`.

Pico FIDO source anchors: `CMakeLists.txt`, `src/fido/cbor_get_info.c`,
`src/fido/cbor_make_credential.c`, and SDK `src/main.c`, `src/usb/usb.c`,
`src/fs`, `src/otp/otp_rp2350.c`, `cmake/deps.cmake`.

In particular, do not treat a local button press as user verification, reuse the
vault's authenticated flag, or simply change GetInfo to advertise `uv`. Local
verification needs its own credential, durable retry state, token permissions,
expiry and protocol error handling.

## Build evidence and limits

| Check | Result | What it establishes |
| --- | --- | --- |
| Fresh Fuse Vault host configure/build/CTest | **26/26 passed** | Storage/application regression baseline |
| CanoKey native static-library build, extra interfaces disabled | **Passed** | Current source and default crypto dependencies compile on this host |
| CanoKey generic Cortex-M33 cross-build with default crypto backend | **Failed** | Embedded backend needs time and entropy configuration; not a proven RP2354 link |
| Pico FIDO standalone `pico2` build with installed Pico SDK, OATH/OTP/PQC disabled | **Compiled and linked** | RP2350-family toolchain compatibility; not Fuse Vault board integration |

CanoKey's cross-build stopped in TF-PSA-Crypto `platform/platform_util.c` with
`No mbedtls_ms_time available` and a request to replace built-in host entropy with
`MBEDTLS_PSA_DRIVER_GET_ENTROPY` / `mbedtls_platform_get_entropy()`. This was an
unadapted generic target probe, not evidence that CanoKey cannot run on RP2354.
Further errors may appear after supplying those platform hooks.

The Pico FIDO ELF reported 436,896 bytes of text and 81,192 bytes of BSS with
`arm-none-eabi-size`. These are standalone build figures, not an integrated RAM
budget; heap, stack peaks, feature pruning and shared crypto affect the result.

No combined Fuse Vault/FIDO ELF, USB enumeration, authenticator conformance test,
physical authentication or power-cut test was performed. The CanoKey native
result is a library build, not a run of its upstream unit tests.

Concurrent SD-driver/build-file edits appeared later in the workspace. They were
not modified or assessed here; the 26-test result is the earlier assessment
baseline and does not validate those subsequent changes.

Reproduction commands (after checking out the revisions and submodules above):

```sh
cmake -S /tmp/fv-canokey-assessment -B /tmp/fv-canokey-build \
  -DENABLE_NFC=OFF -DENABLE_APPLET_NDEF=OFF \
  -DENABLE_IFACE_WEBUSB=OFF -DENABLE_IFACE_CCID=OFF \
  -DENABLE_IFACE_KBDHID=OFF -DENABLE_PASS=OFF -DENABLE_DEBUG_OUTPUT=OFF
cmake --build /tmp/fv-canokey-build -j4

# Repeat those feature flags with a separate build directory and:
# -DCMAKE_SYSTEM_NAME=Generic
# -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc
# -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
# '-DCMAKE_C_FLAGS=-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections'
# to reproduce the unadapted CanoKey cross-build failure.

cmake -S /tmp/fv-pico-fido-assessment -B /tmp/fv-pico-fido-build \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_BOARD=pico2 \
  -DENABLE_OATH_APP=OFF -DENABLE_OTP_APP=OFF -DENABLE_PQC=OFF
cmake --build /tmp/fv-pico-fido-build -j4
arm-none-eabi-size /tmp/fv-pico-fido-build/pico_fido.elf

cmake -S firmware/host -B /tmp/fv-fido-baseline
cmake --build /tmp/fv-fido-baseline -j4
ctest --test-dir /tmp/fv-fido-baseline --output-on-failure
```

Temporary logs use `/tmp/fv-canokey-{configure,build}.log`,
`/tmp/fv-canokey-arm-{configure,build}.log`,
`/tmp/fv-pico-fido-{configure,build}.log`, and
`/tmp/fv-fido-baseline-{configure,build,tests}.log`. They are disposable; the table
above is the retained assessment record.

## Proposed initial product policy

These are implementation defaults for review, not changes to the V1 contract:

- Keep exclusive Vault/FIDO modes. USB is detached while switching. FIDO mode
  exposes HID only; storage mode exposes MSC only after vault authentication.
- Initially require the configured device and its valid SD card. Missing or
  removed media disables FIDO as well as storage. Card-independent FIDO would
  need a different persistence and boot design and is deferred.
- Target USB CTAP2 registration/assertion, ES256, discoverable credentials and
  credential management. Choose the advertised CTAP version only after checking
  its mandatory feature requirements. Disable unimplemented extensions/options
  in both dispatch and GetInfo; do not just hide UI entries.
- First transport prototype may use ClientPIN and physical presence. The final
  product milestone requires independent on-device verification. That prototype
  must not be presented as completion of the local-verification requirement.
- FIDO reset invalidates all FIDO credentials and verification state, including
  non-discoverable credential handles. It never erases the vault. Ordinary FIDO
  retry exhaustion blocks FIDO under its own recovery policy; it does not invoke
  the vault's destructive ten-attempt path.
- Preserve current whole-device root revocation: ten vault failures also disable
  any FIDO keys derived from those roots. Explain this in setup/destruction UI.
  If FIDO must survive vault destruction, redesign the root/revocation scheme
  before freezing the FIDO format; separate derivation labels cannot provide it.
- Defer credential export/sync, NFC, composite MSC+HID, enterprise attestation,
  unrelated smartcard/OTP applets and optional PQ algorithms.

## Architecture to implement

Introduce `fido_service` and a narrow platform adapter. The CTAP core receives
FIDO-only storage/crypto operations, time, randomness, presence/verification and
transport functions. It must not receive the vault master key, plaintext block
device or unrestricted platform-services object.

Refactor TinyUSB initialization/task/detach from `rp2354_usb_msc.c` into a shared
USB owner. Preserve MSC callbacks; add FIDO HID callbacks and descriptor selection
while disconnected. Test re-enumeration and descriptor caching on each supported
OS. Route attach/detach through the existing connector-safety code.

Open `fv_media_open_fido()` independently of vault unlock. Derive private FIDO
storage/wrapping keys using versioned domains and a reset generation. Never use
the vault VMK. Use the existing authenticated block machinery only behind an
explicit FIDO key/domain adapter with a fixed FIDO format. Its current 4:1 physical
block overhead would reduce the 1 MiB reservation to 256 KiB before LittleFS
overhead; measure credential capacity rather than promising a count.

Adapt LittleFS read/program/erase/sync to that private encrypted block device,
including defined erase contents, interrupted writes and disk-full behaviour.
Its file attributes and rename semantics are required by CanoKey. Do not silently
format a corrupt credential store. Version FIDO metadata within the reservation
without changing existing vault data offsets.

Keep retry counters and the reset generation in authenticated internal storage.
An authenticated SD snapshot can still be replayed: encryption alone does not
prevent rollback. Design a versioned extension/migration for the existing small
internal journal, including wear and space budgets. Anchor reset generation so
restoring old SD media cannot resurrect reset credentials or reset retry counts.
Decide whether deletion rollback also needs an internal commit anchor; absent
such a mechanism, do not claim deletion resists restored SD snapshots. Keep all
records needed for a transaction consistent across SD and internal-flash commits.

Session cleanup must clear FIDO keys, PIN/UV tokens, pending assertions, buffered
reports and upstream globals on exit, reset and fault. Enforce a fresh physical
action for each approval; a held mode-selection button must not approve a request.
Logical APIs/key domains reduce accidental cross-access but do not enforce a
hardware security boundary within one privileged address space. Resolve that
limitation alongside the existing secure/non-secure partition work.

## Delivery sequence and acceptance gates

### 1. Prove the reusable core and crypto port

- Pin CanoKey and dependencies; create an explicit CTAP-only target behind an
  off-by-default `FUSE_VAULT_ENABLE_FIDO2` option.
- Separate its transport helpers from the bundled USB device stack and remove
  unrelated reachable applets, test hooks, debug output and presence bypasses.
- Resolve the crypto mismatch: either configure/port one shared backend, or
  implement the needed CanoKey crypto entry points using a compatible backend.
  Do not link overlapping Mbed TLS versions or allow weak crypto stubs to count
  as implemented algorithms. Omit unsupported algorithms from GetInfo.
- Provide embedded time/entropy and a cooperative wait adapter. Verify that
  safety polling and cancellation continue while waiting and during crypto.

**Gate:** combined Fuse Vault RP2354A ELF links; crypto known-answer and signature
verification tests pass; link map has one intended crypto backend, no unwanted
USB stack or applets, and adequate measured/estimated memory margins. Existing
host suite passes with the feature both off and on. If this needs broad changes
to vault crypto or upstream CTAP internals, reassess Pico FIDO before proceeding.

### 2. Establish transport and a disposable authenticator

- Add HID descriptors, runtime selection, report queue and CTAPHID adapter.
- Use a host/disposable credential backend and prototype ClientPIN/presence.
- Integrate GetInfo, registration and assertion into the host test harness.

**Gate:** host tools enumerate and inspect the authenticator; signatures verify;
fragmentation, malformed lengths, channel contention, timeout, CANCEL, held
buttons, lock and detach are tested. Board tests prove HID-only/MSC-only modes
and responsive UI/safety checks. No persistent user credentials yet.

### 3. Add independent persistent FIDO state

- Implement encrypted FIDO storage, internal retry/reset records, migration and
  reset transactions. Connect upstream filesystem and secret initialization.
- Assign Fuse Vault identity/attestation policy; remove upstream default identity,
  test certificates/private keys and manufacturing/vendor administration paths.

**Gate:** credentials survive reboot and work with the vault locked; the host
cannot read them through MSC. Card absence/corruption fails closed. Power cuts
at each commit boundary, restored SD snapshots, disk-full, reset and existing
root revocation have tested outcomes. Firmware updates preserve the format or
perform an explicit recoverable migration.

### 4. Deliver on-device verification and credential UI

- Add separate FIDO setup/secret entry, approval, account selection and reset UI.
- Implement the chosen CTAP built-in verification flows, including permission-
  scoped tokens, expiry, retry persistence and truthful GetInfo capabilities.
- Display relying-party/account information as untrusted host-provided text;
  bound and sanitize it, and avoid ambiguous truncation on the small display.

**Gate:** verification-required browser requests succeed only after valid FIDO
verification; vault unlock never authorizes FIDO and FIDO never unlocks storage.
Wrong secrets, cancellation, reset, mode changes, reboot and token expiry are
covered. ClientPIN and local verification have an explicit coexistence policy.

### 5. Validate and enable

- Run the selected CTAP version's conformance/interoperability checks, parser
  fuzzing and Windows/macOS/Linux browser matrix. Include credential management,
  multiple accounts, signature-counter policy and supported extensions.
- Measure stack/heap peaks, response/keepalive timing, flash wear and SD latency.
- Complete hardware security/release gates and independent review of adapters,
  key separation, verification, reset and failure paths.

**Gate:** only then set `fido_available` for a production configuration. Existing
storage release gates remain in force. Upstream certification or a successful
standalone build does not certify the combined device.

## Planning implications

Reuse removes the need to design a new CTAP implementation. The critical path
is now the crypto/USB port, durable FIDO state, and local verification. Milestone
1 should be a bounded feasibility spike; revise delivery estimates from its
combined build, patch size and timing evidence. No firm calendar estimate follows
from the standalone build results alone.

This plan elaborates [roadmap stage 9](implementation-roadmap.md#stage-9--fido2-as-an-independent-product-slice)
and preserves the [V1 storage contract](v1-product-contract.md). Hardware deployment
follows storage/device-lifecycle stabilization; host-side integration can proceed
behind the disabled feature flag.
