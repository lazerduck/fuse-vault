# FIDO2 and passkeys

Fuse Vault can act as a USB FIDO2 authenticator. This lets you register it with a website or service and then use it to sign in. FIDO uses a public and private key pair for each credential. The service holds the public key, and the device uses the private key to sign a challenge during authentication. The service can check that signature without receiving the private key. The browser connects this exchange to the website being visited. See the [WebAuthn specification](https://www.w3.org/TR/webauthn-2/) for the protocol model.

The FIDO store lives in the private SD card region described in [File storage](FileStorage.md). Its protection depends on the VMK, so unlocking the vault also makes FIDO available. This page describes the current firmware with FIDO enabled.

## Registering and signing in

When a website asks to register a credential or sign in, the device requires an unlocked vault and approval on the device. A request arriving while locked can open the credential-entry screen. Basic discovery, such as asking which FIDO features the device supports, is available while locked.

There are two separate decisions involved:

- **User verification:** entering the vault credential establishes that you are authorized to use the vault.
- **User presence:** approving the particular registration or sign-in request establishes that you want it to proceed.

The screen shows the operation and the site's identifier supplied by the host. This gives you context for approval; the browser is responsible for checking the website's origin. A prompt expires after 60 seconds if left unanswered.

Successful verification can be reused according to the selected FIDO policy:

| Policy | When the credential needs entering again |
| --- | --- |
| Strict | Verification must first be used within 30 seconds. Reuse is tied to the same site and expires ten minutes after verification. Changing sites requires fresh verification. |
| Session | Verification can be reused across sites for the rest of the unlocked session. |

Physical approval of a registration or sign-in request still applies when verification is reused. Changing the policy clears the saved verification. Expiration of FIDO verification causes another credential prompt; the storage session can remain unlocked.

## Attempts and lockouts

FIDO uses the same on-device credential and persistent attempt counter as vault unlocking. An incorrect credential submitted through FIDO consumes a vault attempt and closes the unlocked session. Successful verification resets the attempt count in the normal way.

Reaching the limit applies the vault's configured destruction or permanent-lockout policy, as described in [Setup](Setup.md). This means failed FIDO verification can ultimately destroy access to both files and passkeys. Removing permanent lockout remains a proposed change.

The FIDO protocol also has verification errors, short-lived authorization tokens and retry reporting. In this implementation, the reported verification retries come from the vault's remaining attempts. The host cannot set up a separate FIDO PIN with its own retry allowance. Prompt timeouts, rejected requests and expired authorization are separate from submitting an incorrect vault credential.

## SD-backed credential storage

The firmware reads the FIDO filesystem directly from the private SD region. It
keeps an eight-sector plaintext cache, bounded record buffers and file/RP indexes
in RAM, rather than a complete filesystem image. Buffers and derived keys are
cleared when the FIDO session closes.

The existing 1 MiB reservation contains an 824 KiB filesystem, 55 KiB of sector
authentication metadata, a 128 KiB redo journal and 17 KiB for control records and
migration workspace. USB geometry is unchanged. The engine supports 512 resident
slots; available bytes and fragmentation can limit unusually large records.
The desktop journal test fills all 512 slots using distinct sites, 64-byte user
IDs and 100-character account/display names, then checks reopen, signatures,
full-store rejection, deletion/reuse and reset.

Nonresident credentials remain supported. Their protected identifiers are held
by the host or service, but still depend on the device's FIDO secrets.

## Saving and protecting credentials

Separate VMK-derived keys protect the filesystem and journal. Data uses the
selected encryption stack; sectors are authenticated before decryption. Updates
first go to a bounded journal, including the replacement authentication metadata.
The device synchronizes and verifies the journal before publishing its commit
record. It then applies, synchronizes and verifies those sectors in the main
filesystem before marking the transaction complete and reporting success.

An interrupted uncommitted update leaves the main filesystem unchanged. A
committed update is replayed before normal FIDO access, including after another
interruption during recovery. Missing or corrupt committed journal data fails
closed. The journal can hold 128 destination sectors; overflow faults the session
without publishing a partial transaction. Ordinary signing uses zero signature
counters and does not write merely to increment a counter.

There is no per-sector fingerprint table or internal anti-rollback anchor.
Restoring old valid sectors/tags at their original positions, journal state, or
an entire SD image is outside the freshness guarantee and can undo deletion or
reset. The device assumes exclusive media ownership; card replacement requires
closing and reopening the session. Soldered storage does not provide cryptographic
freshness. Changing the vault credential preserves the VMK and passkeys.
Destroying the enrollment token removes the device's ability to recover the VMK.

On first FIDO access after unlock, firmware automatically converts authenticated
64 KiB or 128 KiB snapshots. It stages the original encrypted snapshot in unused
space, commits a migration marker, then relocates filesystem links into the larger
image. Credential payloads and engine wrapping keys are preserved. Interrupted
migration resumes from the staged source. **Do not downgrade firmware after this
conversion.** A failed open never automatically creates a fresh store.

## Viewing, deleting and resetting

While unlocked, the local management interface can list resident credentials and delete a selected credential. The list contains account and site information. Private keys are not exposed by this interface.

Reset FIDO explicitly reinitializes the FIDO store. It removes resident credentials and replaces the FIDO secrets needed to use existing nonresident credentials. The vault's files, VMK and enrollment token are preserved. Accounts registered with the old credentials will need another way to sign in and register replacement credentials.

There is also a host-requested FIDO reset operation. It requires authorization and a destructive confirmation on the device, and is accepted only in the first ten seconds after USB enumeration. Reopening the vault does not restart that window. The local Reset FIDO action provides the unlocked management route outside that window.

Reset publishes an authenticated destructive-intent marker before directly
initializing the filesystem. It does not journal the credentials being erased.
A restart completes that initialization before allowing FIDO access. Host reset
preserves the local UV policy; local Reset FIDO clears it, matching the previous
behaviour. Reset is logical removal, not a promise of forensic SD erasure.

## Areas to review

Hardware acceptance still needs to cover power interruption, memory use under combined USB/FIDO workloads and the full session-cleanup paths. The desktop tests provide useful evidence, but these device checks remain part of validating the implementation.
