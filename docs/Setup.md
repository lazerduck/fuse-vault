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

The user also selects the failure policy before confirming creation. The UI allows 1–100 attempts, followed by enrollment-token destruction. The default is ten attempts. Permanent lockout is no longer selectable.

The current setup order is:

1. Choose the credential entry method.
2. Enter and confirm the credential.
3. Select the encryption stack.
4. Select the attempt limit; exhausting attempts destroys the enrollment token.
5. Review the choices and explicitly confirm creation, which replaces the existing SD layout.
6. Provision OTP enrollment material if needed. Each enrollment gets one fresh random token and a fresh authenticated journal. There is no permanent device root.
7. Generate a random VMK and protect it using the credential and device-held secrets, with the selected cipher families.
8. Initialize storage metadata and create the partition table and FAT32 filesystem through the encrypted storage path. Prepare the FIDO store when FIDO is enabled.
9. Write and verify the vault headers, then commit the active-vault state in the internal journal.

The VMK is generated independently at random. The user credential and OTP secrets derive the keys used to protect it. The details are in [Key handling](KeyHandling.md).

The vault becomes active after storage initialization and header writes succeed. On successful completion, the temporary unlocked session is cleared and the user returns to the locked home screen. They must unlock the new vault before using it.

Formatting initializes the encrypted filesystem and the metadata needed to treat unwritten content as zeros, as described in [File storage](FileStorage.md).

## Unlock

While locked, the VMK is stored encrypted in the SD card's vault headers. Internal flash holds the security-state journal. The user credential and OTP secrets are needed to recover the VMK into RAM and construct the unlocked session.

From the locked home screen, pressing Select opens the entry method chosen at setup. That choice is recorded in the unencrypted SD header. The firmware checks the header against the authoritative hash held in the internal journal before trusting it.

Before evaluating a submitted credential, the device durably increments the attempt counter and records that an attempt is pending. HMAC authenticates the journal records, protecting the saved attempt count and other security state against unauthorized modification. Its authentication key is derived from the enrollment token, with separate purpose and slot context. An interrupted attempt remains charged across power cycles.

If the credential is correct, the firmware recovers the VMK, resets the attempt count to zero and builds the working encryption pipeline. If it is incorrect, the user can try again while attempts remain. The final allowed attempt can still succeed. An interruption on the final charged attempt is handled conservatively by applying the limit policy during recovery.

## Reaching the attempt limit

With the default destruction policy, the device records destruction intent, marks the current enrollment token revoked, and programs its secret rows to all ones. Interrupted destruction is resumed during recovery before allowing further authentication. No permanent root or old journal key is retained.

The enrollment token is a required input to deriving the VMK's wrapping keys. Destroying it removes that input, preventing the device from recovering the VMK even with the correct user credential. The encrypted VMK and encrypted files and passkeys remain on the SD card, with access lost through destruction of the token.

After destruction completes, the user can set up a fresh vault using the next available token slot. A fresh token area has 228 slots, including the initial enrollment; interrupted provisioning and legacy occupied rows reduce available capacity. The fresh enrollment creates a new, empty vault.

Existing lockout-policy records that authenticate in the current format retain their policy and enforcement. A locked enrollment remains denied; credential changes do not silently change its policy. The UI offers destruction for new setup and explicit policy changes. Old root-based development formats are incompatible and are not migrated.

## Interrupted setup and destruction

Provisioning marks a slot started, writes and verifies its ECC token, erases and initializes its journal, then marks the slot ready. Interrupted started slots are revoked and fully burned before explicit setup advances. A ready token always requires its existing authenticated journal: a missing or corrupt journal denies access, never resets attempts.

After revocation, destruction resumes from OTP state even if the token is partly burned and its old journal can no longer authenticate. All revoked tokens must be fully burned before starting another enrollment. The detailed ordering, derivation labels and mask states are in [Key handling](KeyHandling.md#token-only-otp-enrollment).

Host interruption and rollback tests pass; physical power-interruption validation remains outstanding. Restoring an older image of internal flash within the same enrollment remains outside the agreed protection scope.
