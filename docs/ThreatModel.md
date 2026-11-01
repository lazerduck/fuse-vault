# Threat model

This is the current agreed security scope for Fuse Vault, consolidated on 2026-11-01. It takes precedence over scope statements in the historical [design notes](AI/README.md). It describes the protections we intend to provide; pre-alpha implementation and test results do not yet establish every guarantee below.

## The core idea

The computer you plug Fuse Vault into does not need to be trusted with the secrets that unlock the device or protect its contents. You enter your credential on the device. The device uses its keys internally and gives the computer only the access you have authorized.

If you unlock the files, the computer can read them. Full access also lets it change or delete them. That access must not give it the keys, a copy of your unlock credential, or another way to come back after the session ends and gain access again without fresh authorization. Full access is permission to use the file volume and request approved passkey operations; it is not permission to extract secrets, replace trusted firmware or establish persistent access.

We do not guarantee that your data will remain accessible under every circumstance. When the device cannot establish that access is authorized, it must refuse access, even if that means losing data or requiring a new enrollment.

## What must stay protected

Device-managed secrets include the unlock credential, enrollment token, vault master key (VMK), derived encryption and authentication keys, passkey private keys, and the internal secrets that protect FIDO credentials. Usable copies must not be exported to the host through normal operations, error responses, diagnostics or firmware-update paths. Their persistent storage must also remain protected.

This does not cover secrets you choose to put in ordinary files: opening the file volume exposes those files to the computer. Public keys, signatures and protected FIDO credential identifiers are expected protocol outputs. FIDO also uses temporary protocol secrets and authorization tokens that the host can hold. These must be limited to their permitted operations and lifetime; they must not reveal device-managed secrets or restore device authorization after locking.

Internal authentication state is also protected: the accepted credential and vault header, security settings, attempt accounting, enrollment revocation and current session permissions. An attacker must not change that state to gain access.

## What the attacker can control

We assume the computer's operating system, applications and USB software can all be malicious. The attacker can record traffic, issue arbitrary storage and FIDO requests, send malformed input, replay requests, overlap operations, cancel work and trigger USB resets or disconnections. They can use every operation permitted during an authorized session and retain everything the device releases to them.

We also assume an attacker can possess the device, try credentials, interrupt its power, and read, copy, replace, corrupt or restore the entire SD card. Firmware, storage formats and algorithms are public. The attacker may know both the plaintext of files and their encrypted representation.

The USB file volume does not expose raw vault headers, the private FIDO area or storage metadata. Keeping those regions private reduces exposure, but confidentiality must not depend on ciphertext being unavailable. A copied SD card, including wrapped keys and metadata, must not be sufficient to decrypt the vault or evaluate credential guesses without the protected enrollment secret.

## Authorization and session boundaries

- **Unlock happens on the device.** Host requests cannot submit or capture the local unlock credential, impersonate physical approval, or grant themselves an unlocked session. Credential and security-policy changes require the authorized local flow.
- **The selected mode limits access.** Full access permits file reads/writes and FIDO; Passkeys only permits FIDO; Read-only files permits file reads without writes or FIDO. Selecting a mode does not authorize it. Host requests cannot broaden permissions.
- **Passkey operations require approval.** Registration and sign-in require physical approval, as well as the applicable verification policy. Previous button presses, stale requests and cancellation must not approve a later operation.
- **A session ends completely.** Locking, disconnecting, restarting or another session-invalidation event must revoke its permissions and temporary authorization. Pending work, cached verification, protocol tokens and unsent responses must not carry access into a later session. Working secrets and sensitive buffers must be cleared when no longer needed.
- **A later session requires fresh authorization.** Captured traffic, signatures, protocol tokens, encrypted storage and files copied during an earlier session must not unlock the device or authorize new passkey use.

The current automatic lock is an inactivity timer. Successful host file traffic can keep a session open, so it is not a guaranteed maximum connection time. The later-session guarantee starts when the session actually ends. See [Session](Session.md) for the events and cleanup behaviour.

Locking cannot erase plaintext already copied to the computer. It also cannot revoke a website login session or cookie obtained through an approved passkey operation. The device displays the site identifier supplied by the host; a compromised browser cannot be relied upon to report the genuine website origin. We protect the device's keys and authorization boundary, not the trustworthiness of the computer or a website session it controls.

## Attempts, destruction and interrupted operations

When destruction after a configured number of failures is enabled, power cycling, changing SD cards or restoring an old SD image must not grant extra attempts. Disabling destruction removes that attempt limit; it does not bypass credential verification or grant any host access while locked.

Credential changes and security-policy changes must not be undone by restoring an old SD header. Enrollment destruction must prevent the device from recovering the old VMK, including after interruption or restoration of old SD contents. Destruction cannot revoke plaintext or a usable secret already extracted beforehand.

Missing, corrupt, inconsistent or partially written security state must fail closed. Recovery must not silently reset attempts, accept an older credential, recreate an enrolled vault or continue with default authorization. Interruption may leave the device unavailable; it must not turn failure into permission to access secrets. See [Key handling](KeyHandling.md) and [Setup](Setup.md).

## Availability and data history

Availability is not guaranteed. Device failure, media damage, deliberate corruption, lost credentials and key destruction can cause permanent loss of access. No supported key-export or recovery mechanism currently restores that access.

Encrypted data is authenticated, but we do not promise that it is the newest version. An attacker may restore valid historical file or FIDO data, undo deletion or a FIDO reset, or cause corruption by mixing snapshots. Unauthenticated unset-sector flags can also turn affected file content into zeros. These are accepted data-loss and replay limitations; they must not reset authentication, restore attempts or undo enrollment destruction. Restored passkeys still require a valid device session and approval before use.

An authorized writable session can supply arbitrary file content, including malicious files. Fuse Vault does not make those files safe to open on another computer. See [File storage](FileStorage.md) and [FIDO2](FIDO2.md) for the integrity and replay boundaries.

## Trusted components and remaining protections

The device hardware, its local input/display, cryptographic implementation, randomness and trusted firmware form the security boundary. Production protection requires appropriate OTP access restrictions, trusted boot and update policy, and debug restrictions. Physical possession must not provide a supported debug, bootloader or update route to read secrets or install arbitrary firmware. Those restrictions are not all established by the current development configuration.

Hostile USB/FIDO input is in scope. A host-triggered firmware bug that exposes keys or bypasses authorization violates this model; calling the firmware trusted does not excuse it. The current firmware has no hardware isolation between its components, so a successful firmware compromise can expose its secrets. A full TrustZone split remains deferred as additional containment; it does not replace safe parsing, authorization checks or the production access restrictions.

We currently make no claim of resistance to invasive chip extraction, fault injection or side-channel key recovery. Restoring an older internal-flash image is outside the existing SD rollback protection; boot/update policy must prevent a host from using supported interfaces to bypass the authorization boundary. Internal-flash rollback resistance needs separate design and evidence before it can be claimed.

Outstanding work includes production boot/debug/OTP policy, hostile-input and authorization testing, hardware interruption tests, and wider validation of secret cleanup across both cores, libraries and transfer buffers. The [production checklist](AI/production-checklist.md) and linked validation records track evidence, including historical results; they are not a blanket claim that this threat model has been satisfied.
