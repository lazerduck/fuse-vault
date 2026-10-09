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

## Why the store is in RAM

The imported FIDO engine works with a small filesystem-like image and accesses records through memory addresses. Our adapter provides that image as a contiguous 128 KiB RAM buffer. On the first FIDO operation that needs it after unlock, we read and authenticate the saved image from the SD card and open it in RAM. Later operations use this image until the FIDO session closes.

This simplifies adapting the engine and saving a consistent image, but reserves enough RAM for the whole store. FIDO2 itself does not require this arrangement. The 1 MiB reservation on the card includes snapshot copies and metadata; the engine's usable image is currently 128 KiB.

Capacity is approximately 100 discoverable credentials, often called resident passkeys. Each includes account information and storage overhead as well as key material. The [desktop capacity tests](../results/fido-128k-20260921.md) stored 103–112 credentials depending on the lengths of account names, user identifiers and site information. The exact capacity varies with the records stored.

The engine also supports nonresident credentials. For these, the host or service retains a protected credential identifier that the device can use later. They do not each consume a resident-passkey slot, but still depend on the device's FIDO secrets.

## Saving and protecting credentials

Separate keys derived from the VMK protect the FIDO store. It uses the selected encryption stack and authenticated snapshots, with two banks on the SD card. When the image changes, the device writes the alternate bank, synchronizes and verifies it, and then commits the authenticated metadata that selects it. A successful operation that changes the store is reported after its persistence step succeeds.

Opening a valid store is read-only. The current implementation reports a zero signature counter, so ordinary signing does not save the whole image just to increment a counter.

An interrupted save can leave the previous committed snapshot available. If the newest authenticated commit points to damaged data, opening fails instead of silently selecting older credentials. A corrupt store requires explicit action from the user; unlocking does not automatically replace it with an empty one.

As with file storage, authentication does not prevent restoration of an older, valid SD image. Such a restoration can undo passkey deletion or a FIDO reset within the same vault. Changing the vault credential preserves the VMK and therefore preserves the FIDO store. Destroying the enrollment token removes the device's ability to recover the VMK and use the passkeys.

## Viewing, deleting and resetting

While unlocked, the local management interface can list resident credentials and delete a selected credential. The list contains account and site information. Private keys are not exposed by this interface.

Reset FIDO explicitly reinitializes the FIDO store. It removes resident credentials and replaces the FIDO secrets needed to use existing nonresident credentials. The vault's files, VMK and enrollment token are preserved. Accounts registered with the old credentials will need another way to sign in and register replacement credentials.

There is also a host-requested FIDO reset operation. It requires authorization and a destructive confirmation on the device, and is accepted only in the first ten seconds after USB enumeration. Reopening the vault does not restart that window. The local Reset FIDO action provides the unlocked management route outside that window.

## Areas to review

If greater resident-passkey capacity is needed, review the engine's memory-backed storage interface and whether records can be retrieved on demand. This would require preserving authenticated storage and reliable interrupted-write handling. Simply allocating more SD space does not expand the current RAM image.

Hardware acceptance still needs to cover power interruption, memory use under combined USB/FIDO workloads and the full session-cleanup paths. The desktop tests provide useful evidence, but these device checks remain part of validating the implementation.
