# The unlocked session

Unlocking recovers the VMK into RAM and derives the working keys needed for storage. These remain available while the device is unlocked, allowing files and FIDO credentials to be used without entering the vault credential for every operation.

The persistent copy of the VMK remains encrypted in the SD card headers. The unlocked session is the temporary state that lets the device use it. [Key handling](KeyHandling.md) explains how the credential and enrollment secrets protect that persistent copy.

## What stays in RAM

The open vault holds the VMK, the derived encryption and authentication keys, and storage state and caches. File transfers also use RAM buffers containing the batches currently being processed.

When FIDO is used, its 128 KiB store image and working secrets are loaded into RAM. FIDO also keeps temporary verification and authorization state. Its lifetime is tied to the same unlocked vault, as described in [FIDO2](FIDO2.md).

The user credential is needed to recover or reverify the VMK. Ongoing file access uses the working keys already in the session.

## Locking

Locking closes FIDO and clears its image, cached secrets and authorization state. The firmware then explicitly zeroes the vault session structure, including the VMK and working keys. USB transfer cleanup drains outstanding worker activity and clears transfer buffers so an old batch cannot be returned after the session has been invalidated.

Locking leaves the encrypted data on the SD card ready for the next successful unlock. Destroying access to that data is the separate enrollment-token operation described in [Setup](Setup.md).

Session invalidation also occurs on USB disconnect, bus reset, suspend and switching the active USB port. Ejecting the storage through the supported USB eject command locks it. Serious storage errors, including integrity failures, close the vault; removal of the SD card is handled when the firmware detects it.

These events can arrive while work is in progress. The firmware marks the session invalid and coordinates cleanup with the worker. This prevents new access while allowing outstanding work to finish before its state is cleared. A write already committed to the card remains committed.

There is currently no general inactivity timeout that locks the vault. Display sleep and the FIDO verification timeout have their own purposes; the vault remains unlocked until a locking event occurs.

## Power loss and restart

RAM is volatile, so a full power-off loses the live session state. The device starts locked and needs the credential again to recover the VMK.

USB events can happen while the chip remains powered, particularly with two connectors. The firmware therefore explicitly invalidates and clears the session on those events. A reset is also a reason to start locked rather than resume access from whatever bytes happen to remain in RAM.

## Protecting RAM while unlocked

The current firmware gives the storage worker ownership of the vault and FIDO secrets. The UI and USB code send it requests. This helps coordinate access, but the hardware memory boundary needed to protect those secrets from other firmware components is still an improvement to implement.

The RP2350 family supports Arm TrustZone and access controls for memory and peripherals. We could place key material and the code that uses it in a protected Secure region, with the rest of the firmware making narrowly defined requests to that code. Access restrictions would need to cover both cores, DMA and debugging as well as ordinary CPU reads. Raspberry Pi describes these facilities in its [security features guide](https://pip-assets.raspberrypi.com/categories/1260-security/documents/RP-009377-WP-1-Understanding%20RP2350_s%20security%20features.pdf).

In that design, a fault in a less-privileged USB or UI component would have a restricted route to the keys. The trusted cryptographic code would still need access to them, and would still need to clear temporary state when finished. A protected region uses hardware access rules around RAM; the protection depends on how we configure and divide the firmware.

## Improvements not yet completed

Define and implement the hardware-enforced boundary around key handling and its permitted operations. This includes deciding which components are trusted and configuring every relevant memory-access path.

Audit and test cleanup beyond the main session objects: temporary stack copies, library contexts, error exits, cancellation and work interrupted by USB events. The current explicit wipes establish the intended lifecycle, but a complete RAM-cleanup claim needs that wider evidence.

The internal security-state journal also has an authentication key available while locked so the device can enforce its saved state. Its lifetime is separate from the unlocked VMK session. The proposed enrollment redesign in [Key handling](KeyHandling.md#proposed-improvements-not-implemented) changes how that key is derived.
