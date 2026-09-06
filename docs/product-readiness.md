# Fuse Vault product-readiness status

Status: active V1 release checklist, updated 2026-09-06.

## Current outcome

The V1 firmware skeleton is now assembled rather than being a collection of
separate prototypes. One runtime owns the complete security transition:

1. boot with no USB data interface;
2. validate factory OTP-root state, the internal authenticated attempt journal,
   the removable-media identity, redundant vault header, entry method, and
   encryption-stack descriptor;
3. on first setup, inspect the SD card and require an explicit destructive
   confirmation before invalidating its old discovery/partition sectors;
4. let the user select and confirm an on-device secret-entry method;
5. let the user select AES-256-XTS, ChaCha20, or either ordering of both;
6. generate a random VMK, wrap it under independently derived password/device
   branches, create the authenticated media layout, and publish the first
   journal state last;
7. reserve each unlock attempt durably before checking the entered secret;
8. after success, derive the selected layer keys, reset the attempt counter
   durably, and attach only the decrypted block interface to USB MSC; and
9. on lock, eject, or fault, stop USB requests and clear the VMK, layer keys,
   epochs, plaintext workspaces, and backend references.

Encryption is deliberately implemented at the 512-byte block boundary, not as
a file parser. The host creates and uses an ordinary filesystem on the unlocked
logical disk. Writes are encrypted before reaching the physical SD card and
reads are authenticated/decrypted before reaching the USB host.

The SD card is not flashed with a BIOS. It receives a versioned authenticated
Fuse Vault layout. That layout reserves independent, non-MSC-visible space and
key namespaces for a future FIDO2/CTAP2 authenticator. FIDO2 itself is not
advertised as usable in V1; the UI labels it planned until a real implementation
explicitly enables it.

## Implemented and host-verified

- Four password-entry modules: number wheels, direction sequence, keypad, and
  word list, including independent setup confirmation.
- A permanent algorithm registry and authenticated ordered stack descriptor.
  AES-256-XTS and ChaCha20 are real implementations. SM4-XTS has a reserved ID
  but is rejected as unavailable; no placeholder silently acts as encryption.
- A mandatory Ascon-AEAD128 record envelope around every selectable stack.
- Random VMK generation, dual password/device-root KDF branches, nested
  AES-GCM/Ascon VMK wrapping, domain-separated layer keys, and explicit clears.
- Redundant authenticated SD superblocks and vault headers, encrypted data,
  future-FIDO, and recovery domains, with bounds and sequence validation.
- Copy-on-write authenticated encrypted sectors with virgin-zero reads and
  interrupted-write recovery.
- Factory OTP identity provisioning separated from user-vault setup. A
  recoverable SD failure cannot burn the OTP revocation marker.
- An authenticated append-only internal attempt journal and ten-attempt
  destructive-root revocation path.
- One device-services adapter composing OTP, internal flash, and removable
  media, and one runtime composing setup, authentication, encrypted storage,
  USB attachment, lock, eject, and fault handling.
- TinyUSB MSC descriptors and read/write/sync/eject callbacks.
- A CRC-checked SD memory-card SPI backend using PIO timing and DMA transfers
  over the existing CLK/CMD/DAT0/DAT3 wiring: 400 kHz initialization, up to 8 MHz
  data, bulk sector transfers and bounded transport-failure handling. The
  protocol test exercises initialization, SDHC/SDSC addressing, read/write,
  CRC failure, transport failure, removal/reinsertion and timeout handling.
  Physical PIO/DMA timing still requires assembled-board testing.
- SD absence is not a fatal boot condition. A new device can enter setup and
  accept a later card; a configured device waits with USB locked, initializes
  an inserted card, and validates its authenticated header before password
  entry. Removal, transport failure, or an encrypted-backend error during an
  active session triggers detach and key clearing.

The native suite currently contains 27 checks with GTK available (26 without),
including a headless test of the graphical simulator's actual orchestration.
The simulator now uses the shared device runtime, persistent devices, clickable
controls and a sample-note panel through encrypted virtual MSC; setup, restart,
wrong/correct unlock, saved-note recovery and eject are exercised by that test.
This panel does not provide an OS-mounted filesystem.

The current suite passes in full, including a direct fake-TinyUSB test of target
MSC callbacks. The preceding 25-check baseline also passed a separate
AddressSanitizer/UndefinedBehaviorSanitizer run and GCC static-analyzer build;
those passes predate the graphical simulator integration. The
RP2354A image also compiles and links; following the 2026-09-06 USB presence
confirmation the debug ELF uses 90,664 bytes of text and 7,800 bytes of BSS.
A separate compile with the guarded ST7735S,
active-high connector-presence, candidate SD-card-detect, routing and TinyUSB
paths enabled also passes. Candidate builds are verification tools, not
electrical evidence. These results prove software composition, not electrical
or security-release readiness.

## Remaining release gates

These are concrete hardware/release tasks, not missing product architecture:

1. **Display bring-up.** The documented module is an 80x160
   N096-1608TBBIG09-C08 with an ST7735S controller, and a guarded SPI/RGB565
   target backend now renders the shared framebuffer. Verify that this is the
   populated part, then measure reset/backlight polarity, SPI rate, rotation,
   RAM offsets and colour order before marking the profile validated. The
   candidate display is enabled by default; its evidence flag remains unset.
2. **USB connector routing.** The FSUSB42 control truth table and revision-1
   USB-C default are now recorded: external pull-downs leave OE# asserted and
   SEL low, so the ROM USB path reaches USB-C for firmware flashing before the
   application starts. The application then drives OE# high before initializing
   other peripherals, and the target disable/select/enable backend is integrated
   with session teardown. The full schematic supplied 2026-09-06 confirms both
   connector-presence inputs assert high through 100 kOhm/120 kOhm dividers;
   firmware now uses those levels. Bench-test voltages and routing on arrival.
3. **SD bench validation.** Verify the pinout and electrical pulls, exercise
   the baseline SPI-mode driver on several cards, validate capacity parsing and
   CRC/error behavior, and test removal during reads/writes. Optimize to PIO
   four-bit SDIO only if measured throughput requires it.
4. **USB interoperability.** Obtain production VID/PID values and test format,
   mount, sustained read/write, sync, eject, reset, suspend/resume, and cable
   removal on supported Windows, macOS, and Linux hosts.
5. **OTP and boot policy.** The page allocation, picotool input, exact candidate
   page-lock words, two-key signing/recovery model, debug lockout, rollback
   rows/version and signed USB recovery policy are now machine-readable and
   checked by the host suite. A fail-closed packager creates signed images,
   four staged OTP inputs and a hash receipt without touching hardware. Create
   the real signing identities, record their public hashes, and validate every
   irreversible transition on sacrificial RP2354A devices. Page 59 necessarily
   remains Secure-read/write at page
   granularity; the field service constrains writes to its revocation row. Do
   not provision valuable boards before the release verifier and independent
   review pass.
6. **Target calibration.** Measure KDF delay, stack watermark, SD/crypto/USB
   throughput, TRNG health behavior, flash endurance, and brownout/power-cut
   outcomes. Replace the initial KDF constants with measured release values.
7. **Security/release review.** Run static analysis, fuzz persistent parsers,
   review cryptographic constructions and zeroization, perform the documented
   power-cut campaign, and commission independent review before valuable-data
   use.

The product is therefore no longer blocked on inventing its basic software
shape. It is blocked on validating and finishing the physical transports and
irreversible security configuration on the manufactured hardware.
