# Fuse Vault

Fuse Vault is a physically separate trust boundary for storing and using digital
secrets. It protects data at rest, keeps authentication and cryptographic keys
away from the host computer, and releases plaintext only through deliberate,
user-authorized disclosure. Its security model combines physical possession
with a knowledge-based secret, without biometric recovery, key escrow, or a
vendor-accessible back door.

The project is an experimental hardware-encrypted USB storage device built
around the Raspberry Pi RP2354A. It is designed for material such as recovery
codes, passwords, private documents, encryption keys, and passkey credentials:
secrets that must remain protected while stored but must also be usable when
their owner chooses to reveal them.

The device is a custom PCB with USB-A and USB-C plugs, an 80×160 colour display,
physical directional controls, and removable SD-card storage. All authentication,
key derivation, encryption, and decryption are intended to happen on the device.

Fuse Vault is currently under development. The hardware, firmware, and
cryptographic design are evolving together as the first prototype is built.

## Mission

Fuse Vault is designed around two primary goals: **security at rest** and
**controlled disclosure**.

A secret is useful only if it can eventually be used. Fuse Vault therefore does
not attempt to prevent plaintext from ever reaching another computer. Instead,
it keeps the device locked and its contents encrypted until the user explicitly
authorizes access using the device's own display and physical controls.

The intended disclosure boundaries are:

- **Locked:** The host receives no storage volume, unlock secret, plaintext, or
  usable key material.
- **Unlocked storage:** The user deliberately exposes the decrypted filesystem
  as a USB mass-storage volume. The connected host can then access that volume.
- **Selective disclosure:** Where supported, the user releases one chosen
  credential or performs one authentication operation without mounting or
  exposing the wider vault.

This makes Fuse Vault more than an encrypted USB drive. It is a user-controlled
boundary between stored secrets and computers that are not inherently trusted.

## The idea

Connecting Fuse Vault supplies power and starts its user interface, but does not
immediately expose a storage volume. The user enters their unlock secret using
the onboard display and controls, without typing it on the host computer.

After successful authentication, the device presents itself as a conventional
USB mass-storage volume. The host performs ordinary block reads and writes while
the RP2354A transparently decrypts data read from the SD card and encrypts data
before writing it. The SD card should never contain the plaintext filesystem.

Disconnecting or locking the device removes access to the decrypted volume and
clears transient secrets from RAM.

```text
                    Fuse Vault
             ┌───────────────────────┐
USB-A / USB-C│  RP2354A              │      SD card
host ◀───────│  USB mass storage     │◀────▶ encrypted blocks
             │  key derivation       │
             │  cipher pipeline      │
             │  display + controls   │
             ├───────────────────────┤
             │ Password entry occurs │
             │ entirely on-device.   │
             └───────────────────────┘
```

## Design principles

- **Security at rest.** Capturing the locked device or removing and imaging its
  SD card does not reveal the stored contents.
- **Hardware-isolated authentication.** Password entry, key derivation, and
  storage cryptography are performed by the RP2354A rather than the host CPU.
- **Deliberate disclosure.** Nothing is mounted before authentication. Plaintext
  is released only after an explicit action using the physical device.
- **Minimum necessary disclosure.** Modes that operate on individual
  credentials reveal only the selected secret or authentication result rather
  than exposing the complete storage volume.
- **Integrity as well as confidentiality.** Encrypted storage must detect
  unauthorized modification and, where the storage design permits, rollback of
  protected data.
- **Encrypted media is untrusted media.** Removing and imaging the SD card
  should disclose ciphertext, not the filesystem or its contents.
- **No recovery by design.** The user secret is combined with a unique,
  device-bound secret. If that secret is destroyed or the device becomes
  unrecoverable, the encrypted data is also permanently lost.
- **Do not depend on one cipher.** The planned cryptographic pipeline can apply
  multiple independently keyed algorithms, reducing reliance on any single
  primitive or standards authority.
- **Minimise host trust.** The host receives only data that the user explicitly
  makes available. It should never receive the vault password or master keys.

Fuse Vault necessarily trusts a deliberately limited set of components: the
RP2354A silicon and immutable boot process, the configured device-security
facilities, the Fuse Vault firmware and provisioning process, and the onboard
display and controls used to authorize operations. The host computer, removable
storage, and undocumented host security processors remain outside that trusted
boundary.

## Hardware

The current design includes:

- Raspberry Pi RP2354A microcontroller with 2 MB of stacked flash
- WiseVision N096-1608TBBIG09-C08 80×160 RGB display
- Four-way directional control and a back button
- SD card connected over SPI
- Male USB-A and USB-C connectors
- USB ESD protection and power-path diodes
- Voltage-divider sensing for connector detection
- A multiplexer for selecting the active USB data connection

The RP2354A was chosen for its integrated flash and security-oriented facilities,
including OTP storage, a hardware true-random-number generator, SHA-256
acceleration, Arm TrustZone support, signed boot, and encrypted code storage.

The EasyEDA Pro hardware project is stored in
[`circuit board/fuse-vault.eprj2`](circuit%20board/fuse-vault.eprj2).

## Key hierarchy

The intended key flow is:

```text
user-entered secret ─┐
                     ├─▶ password KDF ─▶ master key
device-bound secret ─┘                       │
                                            ├─▶ key for cipher layer 1
                                            ├─▶ key for cipher layer 2
                                            └─▶ keys for authentication,
                                                metadata, and other purposes
```

The user-entered secret exists only in RAM while needed. It is never stored on
the SD card or sent to the host. A salt and an appropriate password-based key
derivation function will be used alongside the device-bound secret. Derived keys
must be domain-separated so that no key is reused across algorithms or purposes.

The device-bound secret deliberately makes the vault inseparable from its
hardware. Fuse Vault will not provide a back door, recovery key, or escrow path.

The exact KDF, secret-provisioning procedure, memory-clearing strategy, and OTP
layout remain security-critical design work and are not yet specified.

## Encryption pipeline

Fuse Vault is intended to support a configurable stack of block-preserving
encryption layers. V1 may use one stack selected when the whole vault is
formatted. Encryption passes each storage block through the configured pipeline;
decryption processes the same pipeline in reverse.

```text
write: plaintext block  ─▶ cipher A ─▶ cipher B ─▶ encrypted SD block
read:  encrypted block ─▶ cipher B⁻¹ ─▶ cipher A⁻¹ ─▶ plaintext block
```

Candidate primitives include AES, SM4, and others that can be implemented safely
within the RP2354A's performance and memory constraints. Every layer must use an
independently derived key.

A block cipher alone is not a complete storage format. The design must also
define suitable modes of operation, block positions or tweaks, integrity and
authentication, metadata protection, replay behaviour, and safe handling of
interrupted writes. These details will form part of the on-disk format
specification.

## Operating modes

The device may offer a mode selector when it powers on. Planned modes include:

### Encrypted storage

After local authentication, expose the decrypted vault as USB mass storage.
While it is unlocked, the connected host can read, copy, modify, or delete all
data made available through that volume. Fuse Vault protects data at rest; it
does not attempt to isolate data that the user has made available to an unlocked
host.

### FIDO2 authenticator

Act as a FIDO2 roaming authenticator, with user interaction and verification on
the device. FIDO credentials and storage-encryption keys must use separate key
domains and permissions. Unlocking one function must not implicitly authorize
the other.

### Selective keyboard output (possible future feature)

Allow the user to select one stored credential on the device and deliberately
send only that value to the host as USB keyboard input at the current cursor.
This would avoid mounting the storage vault or exposing unrelated secrets.

The selected value would necessarily become visible to the receiving host and
application, so this mode is selective disclosure rather than secret isolation.
It is a post-V1 possibility, not a current requirement.

## Initial threat model

Fuse Vault aims to protect against:

- Theft or loss while the device is locked
- Offline imaging or removal of the SD card
- Password capture by host keyloggers during normal on-device entry
- Direct access to plaintext or storage keys by the host before unlocking
- Unauthorized or downgraded firmware, once secure boot and provisioning are
  correctly implemented
- Repeated guessing, using a persistent attempt counter and a possible
  destructive lockout policy

Data exposed to a host after unlocking falls outside this boundary. Side-channel
analysis, fault injection, debug-port policy, firmware rollback, supply-chain
attacks, and invasive physical attacks will be addressed in a separate threat
model.

A proposed limit of approximately ten failed unlock attempts would erase or
irreversibly invalidate the device-bound secret. This behaviour must be designed
to survive power interruption and state rollback. It also intentionally permits
denial of service: an attacker with the device could destroy access to its data
even without learning the data.

## Project status

Fuse Vault is currently at the hardware-design and architecture stage. The
repository contains the EasyEDA Pro circuit-board project and a barebones
[RP2354A firmware project](firmware/README.md) ready for initial board bring-up.
The storage format, cryptographic construction, provisioning tools, application
firmware, and enclosure work are still to be developed.

Near-term work includes:

1. Review and validate the schematic and PCB design.
2. Define a detailed threat model and explicit security boundaries.
3. Specify the encrypted block format and power-loss behaviour.
4. Select and review the password KDF and cipher construction.
5. Define OTP provisioning, secure boot, debug restrictions, and firmware update
   policy.
6. Implement the display and physical-input interface.
7. Implement locked and unlocked USB device states.
8. Build and benchmark the storage-encryption pipeline.
9. Add FIDO2 support after the core storage design is stable.
10. Review and test the complete hardware and cryptographic implementation.

## Security status

The project is at the design and prototyping stage and has not yet undergone an
independent security audit. A dedicated security policy and private disclosure
process will be added before public hardware or firmware releases.

## Documentation

- [Raspberry Pi microcontroller documentation](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html)
- [FIDO2 specifications](https://fidoalliance.org/specifications/download/)

## License

No project licence has been selected yet. Until a licence is added, all rights
remain with the project copyright holder.
