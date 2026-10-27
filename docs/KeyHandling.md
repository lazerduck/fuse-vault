# Keys

The Fuse Vault data is encrypted with keys derived from the VMK or vault master key. From this key we are able to derive a number of subsequent keys that can be used individually for each encryption algorithm. The VMK is stored persistently only in protected form on the SD card. Its protection uses the same selected cipher families as the main vault, but a different construction: file data passes through successive XTS encryption layers, while VMK shares are individually protected using key wrapping.

In order to unlock the VMK we need the KEK or key encryption key. This is derived from an independently random enrollment token and the user-entered credential. KEK is shorthand here for the key-encryption material: the implementation derives a separate wrapping key for each share. This allows the user to alter their key, and we can just re-encrypt the VMK with the new KEK to allow a new key to unlock the same vault.

## Ensuring the VMK is secure.
As mentioned the VMK is encrypted and only ever exists plain in RAM. When at rest we need to ensure that the VMK is as secure as the rest of the vault to ensure it does not represent a weakness. The user data on Fuse Vault is secured via a user defined stack of encryption algorithms and so a similar approach is required here.

The VMK is first split into n parts with n representing the number of encryption algorithms the user selected. This is done through generating 32 random bytes (A) and XORing them with the VMK. This leaves us with a random A and B such that A XOR B = the VMK. We can then repeat this process until we have enough parts. If only one encryption layer is selected, the sole share is the VMK itself and no random splitting is needed. For example we can then take B, generate a random number C and then XOR C with B to get B'. At this point B' XOR C XOR A gives us the VMK.

With these parts A, B', and C we can then encrypt each one with one of the encryption algorithms the user has selected. With independently random shares, sound key derivation and a correctly functioning implementation, the VMK remains hidden while at least one share remains confidential. A weakness or trapdoor in one selected cipher need not expose the other shares. With only one selected cipher, there is no second cipher protecting a remaining share if that cipher fails; repeating the same cipher does not provide diversity against a failure of that cipher family. We can apply HKDF to the KEK to create derived keys for each encryption algorithm, so recovering one wrapping key should not reveal the other wrapping keys. This addresses a weakness in a cipher, rather than malicious firmware that can directly read secrets in RAM. The wrapping keys still share the SHA-256-based derivation described below.

### Deriving the wrapping keys

The gist is: the current enrollment token and user credential are both needed to unlock the VMK. Changing the credential changes its protection without changing the VMK or re-encrypting the files.

The current implementation does this in stages:

1. **Bind the vault to the device and enrollment.** HMAC-SHA-256, keyed by the enrollment token, combines the token, token-slot number and vault identifier with a purpose label to produce a binding secret.
2. **Bind the credential to that context.** A second HMAC uses the binding secret to combine the user credential and header context. This produces the secret input for the next step.
3. **Make each credential guess expensive.** PBKDF2-HMAC-SHA-256 performs 60,000 iterations using that input and a salt built from a purpose label and the header's random salt. PBKDF2 is a deliberately slow password-based key derivation function: it makes each evaluable guess require repeated computation. The device's persistent attempt limit is a separate protection.
4. **Derive keys for separate purposes.** HKDF-SHA-256 derives a wrapping key for each share and a separate authentication key for the VMK envelope. HKDF is a fast key derivation function, using distinct labels and context to separate the outputs; it is not the expensive password-hardening step.

The envelope includes an HMAC-SHA-256 authentication tag covering its header and wrapped shares. HMAC is a keyed check for modification, not another encryption layer: it does not conceal a share if its wrapping cipher is broken. Sector data also has HMAC authentication, using a separate key derived from the VMK.

The enrollment token also derives a separate key that authenticates the internal flash journal containing attempt counts, policy and enrollment state. The journal is authenticated, not encrypted. There is no permanent device root or fallback MAC key derived from the public device ID.

## Token-only OTP enrollment

Merged on 2026-10-18 in [PR #1](https://github.com/lazerduck/fuse-vault/pull/1). This implements the allocation proposed in [Improvements](Improvements.md#1-expand-enrollment-capacity-and-simplify-provisioning). Each enrollment has an independently random 32-byte token. Destroying it abandons its vault, attempt count, failure policy and header reference. Journal continuity between enrollments is neither required nor accepted.

### Key separation and formats

- Vault binding uses `HMAC-SHA-256(token, "FV2/vault-binding/v1\0" || slot_le32 || volume_id_16 || token)` through the existing binding primitive. The user credential and header context then enter the unchanged credential-hardening and share-wrapping derivations.
- Journal authentication uses HKDF-SHA-256 with the token as input key material and the 16-byte device ID as salt. Expand information is `"FV2/enrollment-state-mac/v2\0" || slot_le32`; output is 32 bytes. The public device ID supplies context, not secrecy.
- Journal records retain the 512-byte body/commit layout, with format version 2 and domains `FV2/device-state/body/v2` and `FV2/device-state/commit/v2` (including their terminating NUL bytes). Each new enrollment starts at sequence 1, zero attempts and the default policy. Old records are erased before the new enrollment becomes ready, and cannot authenticate with its key.

### OTP allocation

| Pages | Purpose |
| --- | --- |
| 0–2 | Reserved chip and boot configuration |
| 3 | One raw enrollment-status mask |
| 4–60 | Four enrollment tokens per page, 16 ECC rows per token |
| 61–63 | Reserved hardware access keys and page-lock configuration |

Pages 4–60 provide **228 enrollment slots on a fresh token area**. Raw OTP rows have 24 programmable bits. The mask packs eight three-bit slots into each row and occupies 29 rows. Every write is read back and verified. Secret writes and reads use the ROM ECC interface; occupancy and destruction checks use raw rows, including ECC bits.

| Bits | Meaning |
| --- | --- |
| `000` | Unused; eligible only if all 16 raw secret rows are blank |
| `100` | Started; abandon and destroy after interruption |
| `101` | Interrupted revocation of a started slot; finish revocation and burning |
| `110` | Token and initial journal verified; ready for vault creation |
| `111` | Revoked; finish burning all 16 secret rows before advancing |

Abandonment sets the revocation bit first (`100` → `101` → `111`), one bit at a time, so it cannot transiently expose the token as `110`. Normal destruction changes `110` → `111`. Other bit patterns, unreadable rows, multiple ready slots, or a ready slot alongside interrupted provisioning deny access and provisioning.

### Provisioning and interrupted setup

Only an explicit new-enrollment setup can erase and initialize the journal, and only when no ready token exists:

1. Revoke all started slots and finish every revoked slot's burn, verifying all secret rows are `0xffffff`.
2. Exclude any unused-mask slots with occupied raw rows, including legacy ECC data. Select a blank slot and verify programming permissions before marking it started.
3. Generate its token, write via ECC, and verify the secret.
4. Erase both journal banks, verify erasure, and commit the initial authenticated empty-enrollment record with the new key.
5. Mark the token ready and verify the mask. Only now may vault creation use it.

An interruption before the ready bit consumes the started slot on the next explicit setup. An interruption after the ready bit resumes the already committed journal without resetting it. A missing or corrupt journal for any ready token always denies access; provisioning never turns it into zero attempts. SD replacement and rollback cannot restore attempts.

Vault creation writes and verifies its headers before committing the active-vault journal anchor. The existing persistence tests cover pending attempts across resets and SD rollback. A record with a nonblank but invalid commit causes corruption even when older valid records exist; it is never skipped to recover a lower attempt count. An uncommitted body can be ignored beside an existing valid record, but a nonblank journal with no valid record is corrupt, not empty.

### Destruction and recovery

Destruction first commits intent while the token and journal are still intact. It then revokes the token in OTP, clears the cached journal key and burns every secret row to all ones. If power fails before revocation, the intact journal's pending state resumes destruction. After revocation, recovery uses OTP alone; it never needs to authenticate with a partly destroyed token or a public fallback key.

For the vault/UI authority interface, a revoked slot is exposed as a terminal pending/destroyed state derived from raw OTP. This is not a journal record and contains no usable vault reference. Completing destruction requires no final flash commit. Explicit setup may then erase the obsolete journal and provision a fresh token.

### Compatibility and remaining validation

This is an incompatible development format change. Root-based and earlier experimental journal formats are not migrated. Previously programmed rows remain excluded; the two development units therefore have less than the fresh 228-slot capacity. A ready token with an older incompatible journal denies access rather than silently resetting attempts.

Lockout-policy records that authenticate in the current format keep their stored policy and locked state. New UI setup and policy changes offer destruction only. Credential changes preserve the existing policy; the firmware does not silently convert an existing lockout into destruction.

Host tests exercise all 228 enrollments and exhaustion, actual journal-key separation, malformed masks, legacy occupied rows, byte-level journal write cuts, provisioning/cleanup/destruction interruption with RAM reset, and stale or missing journals. Validation of the merged PR head passed 30/30 standard host tests and 44/44 full host/FIDO tests under AddressSanitizer and UndefinedBehaviorSanitizer, and built the full Pico firmware with UI, USB MSC and FIDO. Hardware power-interruption and OTP ECC qualification remain required. See the [validation record](../results/otp-enrollment-20261018.md) for scope and limitations.

Restoring an older internal-flash image within the same enrollment remains outside the agreed protection scope. OTP access restrictions, trusted firmware and debug protections are still necessary. Burning a token cannot revoke a copy extracted beforehand.
