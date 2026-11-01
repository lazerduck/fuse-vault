# The unlocked session

Unlocking recovers the VMK into RAM and derives the working keys needed for storage. These remain available while the device is unlocked, allowing files and FIDO credentials to be used without entering the vault credential for every operation.

The persistent copy of the VMK remains encrypted in the SD card headers. The unlocked session is the temporary state that lets the device use it. [Key handling](KeyHandling.md) explains how the credential and enrollment secrets protect that persistent copy. The [threat model](ThreatModel.md) defines the boundary between authorized use by an untrusted computer and access after the session ends.

## What stays in RAM

The open vault holds the VMK, the derived encryption and authentication keys, and storage state and caches. File transfers also use RAM buffers containing the batches currently being processed.

When FIDO is used, its 128 KiB store image and working secrets are loaded into RAM. FIDO also keeps temporary verification and authorization state. Its lifetime is tied to the same unlocked vault, as described in [FIDO2](FIDO2.md).

The user credential is needed to recover or reverify the VMK. Ongoing file access uses the working keys already in the session.

## Session permissions

Keys being present in RAM does not by itself authorize host access. A separate,
worker-owned volatile mode grants Full access (files read/write and FIDO),
Passkeys only, or Read-only files (no FIDO). Invalid/default modes deny access.
Permissions are granted after successful authentication, never just by selecting
a screen option. FIDO reverification cannot broaden them. Vault-wide credential,
security-settings and erase actions require Full access; FIDO management remains
available in either FIDO-capable mode.

Changing mode requires locking and unlocking again. Commands and transfer batches
are tied to a session generation so queued work cannot cross into a new session.
Mode and cached permissions are cleared on invalidation together with the session
and transport cleanup. This is firmware authorization around the existing shared
VMK, not cryptographic separation into independently keyed vault areas.

USB and FIDO responses also check the worker's current generation before release.
Locking discards an old completed USB read and any unsent fragments of a FIDO
response. An explicit USB eject still requests a lock when FIDO is busy; a FIDO
cancel by itself does not close an unrelated read-only file session.

## Locking

Locking closes FIDO and clears its image, cached secrets and authorization state. The firmware then explicitly zeroes the vault session structure, including the VMK and working keys. USB transfer cleanup drains outstanding worker activity and clears transfer buffers so an old batch cannot be returned after the session has been invalidated.

Locking leaves the encrypted data on the SD card ready for the next successful unlock. Destroying access to that data is the separate enrollment-token operation described in [Setup](Setup.md).

Session invalidation also occurs on USB disconnect, bus reset, suspend and switching the active USB port. Ejecting the storage through the supported USB eject command locks it. Serious storage errors, including integrity failures, close the vault; removal of the SD card is handled when the firmware detects it.

These events can arrive while work is in progress. The firmware marks the session invalid and coordinates cleanup with the worker. This prevents new access while allowing outstanding work to finish before its state is cleared. A write already committed to the card remains committed.

## Automatic locking

Security settings can lock the whole session after 1, 2, 5, 10, 15, or 30 minutes of inactivity, or disable the timer. New vaults default to five minutes. Existing vaults retain Off until the user changes their settings. This is separate from display sleep and the FIDO verification timeout.

The countdown starts after successful unlock in any mode. Physical button presses (including waking the display), successful nonempty file reads/writes, passkey creation/sign-in and successful credential reverification renew it. Routine USB status/sync polling, FIDO discovery/retry/key-agreement queries, failed requests and malformed traffic do not. Local activity is bound to its session generation, so a stale button event cannot renew a later session. As an inactivity timer, continued host file traffic can keep it open; it is not a maximum session lifetime.

The dashboard shows remaining minutes/seconds and a small circular indicator. The timer keeps running in Settings and while a FIDO prompt is open. At expiry the worker revokes the session generation, clears its keys and cancels pending FIDO work. A quiet worker polls the deadline without waiting for a USB request. No activity at or after the deadline can revive the old session; authentication is required again.

Storage writes are committed in batches. An executing batch can finish before the worker checks expiry; no later batch or stale result is authorized after revocation. Host-side cached data has not necessarily reached the device, so eject before leaving a writable volume idle if applications have unsaved or cached writes. Locking cannot erase data already delivered to the host.

## Display sleep

Settings → Security includes a Screen setting saved across power cycles. It is
separate from automatic locking:

| Choice | Screen behaviour |
| --- | --- |
| Until lock (default) | Stays on while unlocked, including the countdown; sleeps 60 seconds after locking. |
| 30 seconds, 1/2/5/10 minutes | Sleeps after that period without display activity, even while unlocked. |
| Never | Stays on while the device is powered. |

Existing vaults without a screen preference use Until lock. If automatic locking
is Off, that choice keeps the screen on until the session is locked another way.
Changing it uses the same current-credential and Review → Save flow as other
Security settings.

Physical buttons and accepted FIDO operations/prompts wake the display and renew
its display timer. File traffic, countdown redraws and discovery polling do not.
A lock/unlock transition starts a fresh display interval. A physical press while
the display is dark or being refreshed only wakes it; press again once visible
to navigate or approve. The current frame is refreshed before the backlight turns
on.

Display activity alone never renews the vault's inactivity timer. Keeping the
screen on cannot keep the vault unlocked, and turning it off does not lock the
vault. Physical button activity still renews the session as described above.

## Power loss and restart

RAM is volatile, so a full power-off loses the live session state. The device starts locked and needs the credential again to recover the VMK.

USB events can happen while the chip remains powered, particularly with two connectors. The firmware therefore explicitly invalidates and clears the session on those events. A reset is also a reason to start locked rather than resume access from whatever bytes happen to remain in RAM.

## Protecting RAM while unlocked

The current firmware gives the storage worker ownership of the vault and FIDO secrets. The UI and USB code send it requests. This helps coordinate access, but does not establish a hardware memory boundary protecting those secrets from other firmware components.

The RP2350 family supports Arm TrustZone and access controls for memory and peripherals. We could place key material and the code that uses it in a protected Secure region, with the rest of the firmware making narrowly defined requests to that code. Access restrictions would need to cover both cores, DMA and debugging as well as ordinary CPU reads. Raspberry Pi describes these facilities in its [security features guide](https://pip-assets.raspberrypi.com/categories/1260-security/documents/RP-009377-WP-1-Understanding%20RP2350_s%20security%20features.pdf).

In that design, a fault in a less-privileged USB or UI component would have a restricted route to the keys. The trusted cryptographic code would still need access to them, and would still need to clear temporary state when finished. A protected region uses hardware access rules around RAM; the protection depends on how we configure and divide the firmware.

## Improvements not yet completed

A full TrustZone split is deferred during pre-alpha under the [current threat model](ThreatModel.md#trusted-components-and-remaining-protections). Hostile USB/FIDO input remains in scope: a firmware exploit that exposes secrets or bypasses authorization would violate that model. Isolation could contain the damage from a compromised component, but the current firmware does not provide it. Prioritize trusted boot and update policies, debug and OTP access restrictions, hostile-input validation, and hardware testing of key cleanup and interrupted operations. Revisit component isolation as additional containment, including the trusted components and every relevant memory-access path.

Audit and test cleanup beyond the main session objects: temporary stack copies, library contexts, error exits, cancellation and work interrupted by USB events. The current explicit wipes establish the intended lifecycle, but a complete RAM-cleanup claim needs that wider evidence.

The internal security-state journal also has an authentication key available while locked so the device can enforce its saved state. Its lifetime is separate from the unlocked VMK session. The [token-only enrollment design](KeyHandling.md#token-only-otp-enrollment) derives that key from the current enrollment token and clears it on revocation.
