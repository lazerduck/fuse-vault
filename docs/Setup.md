# Setup unlock and enrollment

During initial setup the user selects how they want to enter their credential. Currently we support three methods: a direction pattern, four number wheels, and four word selectors. Each has a tradeoff between the number of possible values, remembering the credential and entering it on the device.

The number of possible values depends on the method and, for direction patterns, the length:

| Method | Choices | Possible values if chosen uniformly at random |
| --- | --- | --- |
| Direction pattern | 8–64 entries, each one of four directions | 4 to the power of the length; 2 bits per direction |
| Four number wheels | Each wheel selects 00–99 | 100 million combinations, about 26.6 bits |
| Four words | Each position selects from 64 words | About 16.8 million combinations, 24 bits |

The wheels have more possible values than the four-word method, but a direction pattern of 14 entries already has more possible values than the wheels. These figures describe random choices. Familiar sequences, dates or predictable word combinations can be much easier to guess.

## Creating the vault

The user first enters and confirms their credential, then selects the encryption stack. They can select one to four layers from AES-256, Camellia-256 and SM4-128. These layers build the pipeline through which content is encrypted and decrypted. Additional layers can reduce transfer speeds. Changing the stack currently requires creating a fresh vault. Credential changes preserve the existing vault.

The user also selects the failure policy before confirming creation. The current UI allows 1–100 attempts and offers either key destruction or permanent lockout. The default is ten attempts followed by key destruction. Removing the lockout option is an intended change described below.

The current setup order is:

1. Choose the credential entry method.
2. Enter and confirm the credential.
3. Select the encryption stack.
4. Select the attempt limit and failure action.
5. Review the choices and explicitly confirm creation, which replaces the existing SD layout.
6. Provision OTP enrollment material if needed. On a new device this includes both a permanent device root and an enrollment token; after destruction, the existing root is retained and a fresh token is provisioned.
7. Generate a random VMK and protect it using the credential and device-held secrets, with the selected cipher families.
8. Initialize storage metadata and create the partition table and FAT32 filesystem through the encrypted storage path. Prepare the FIDO store when FIDO is enabled.
9. Write and verify the vault headers, then commit the active-vault state in the internal journal.

The VMK is generated independently at random. The user credential and OTP secrets derive the keys used to protect it. The details are in [Key handling](KeyHandling.md).

The vault becomes active after storage initialization and header writes succeed. On successful completion, the temporary unlocked session is cleared and the user returns to the locked home screen. They must unlock the new vault before using it.

Formatting initializes the encrypted filesystem and the metadata needed to treat unwritten content as zeros, as described in [File storage](FileStorage.md).

## Unlock

While locked, the VMK is stored encrypted in the SD card's vault headers. Internal flash holds the security-state journal. The user credential and OTP secrets are needed to recover the VMK into RAM and construct the unlocked session.

From the locked home screen, pressing Select opens the entry method chosen at setup. That choice is recorded in the unencrypted SD header. The firmware checks the header against the authoritative hash held in the internal journal before trusting it.

Before evaluating a submitted credential, the device durably increments the attempt counter and records that an attempt is pending. HMAC authenticates the journal records, protecting the saved attempt count and other security state against unauthorized modification. Its authentication key is currently derived from the permanent device root. An interrupted attempt remains charged across power cycles.

If the credential is correct, the firmware recovers the VMK, resets the attempt count to zero and builds the working encryption pipeline. If it is incorrect, the user can try again while attempts remain. The final allowed attempt can still succeed. An interruption on the final charged attempt is handled conservatively by applying the limit policy during recovery.

## Reaching the attempt limit

With the default destruction policy, the device records destruction intent, marks the current enrollment token revoked, and programs its secret rows to all ones. Interrupted destruction is resumed during recovery before allowing further authentication. The permanent root is retained in the current implementation.

The enrollment token is a required input to deriving the VMK's wrapping keys. Destroying it removes that input, preventing the device from recovering the VMK even with the correct user credential. The encrypted VMK and encrypted files and passkeys remain on the SD card, with access lost through destruction of the token.

After destruction completes, the user can set up a fresh vault using the next available token slot. The current implementation has eight slots, including the initial enrollment, and partially programmed slots can also be consumed. The fresh enrollment creates a new, empty vault.

The alternative currently offered is permanent lockout. This refuses further authentication, including the correct credential, but retains the token. This is a terminal state with no supported recovery or fresh-setup route.

## Improvements not yet implemented

### Remove permanent lockout

Remove permanent lockout as a selectable failure action until we have designed an alternative with a clear purpose and a secure recovery path. Keep the configurable attempt limit, with destruction as the supported action when the limit is exhausted.

The new UI should make the consequence explicit: exhausting attempts destroys access to the current vault, and setting up again creates an empty vault. Existing lockout-policy records need an explicit compatibility decision that preserves enforcement of their stored state and policy.

### Simplify enrollment secrets and expand capacity

Remove the permanent device root. Use one independently random enrollment token to derive separate keys for vault protection and journal authentication. A new enrollment starts with a fresh journal, policy and zero attempts.

Use a single raw OTP status mask on page 3 and pack four token secrets per page across pages 4–60. This provides 228 slots on a fresh token area. The mask identifies unused, started, completed and revoked tokens. The allocation and derivation proposal is described in [Key handling](KeyHandling.md#proposed-improvements-not-implemented).

A token left in the started state after interruption is abandoned and destroyed before advancing. A completed token must be verified before being used to create a vault. A revoked token is never selected again, and any unfinished burning of its secret rows must be completed.

### Define interrupted setup and fresh journal creation

Specify and test the ordering between token completion, journal initialization, filesystem/FIDO initialization and the final active-vault commit. Track token completion and vault activation as separate stages.

Only explicit new-enrollment setup may initialize a fresh journal. A missing or corrupt journal for an existing active vault must deny access. Recovery after interruption between token completion and journal initialization remains a design detail to resolve.

Accept an incompatible format change for the two development units. Previously programmed OTP rows must be identified and excluded from fresh slot allocation.

Implementation of these improvements is pending. Attempt accounting must remain effective across normal resets and SD replacement or rollback. Restoring an older image of internal flash remains outside the agreed protection scope.
