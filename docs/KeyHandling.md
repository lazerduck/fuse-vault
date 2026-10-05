# Keys

The Fuse Vault data is encrypted with keys derived from the VMK or vault master key. From this key we are able to derive a number of subsequent keys that can be used individually for each encryption algorithm. The VMK is stored persistently only in protected form on the SD card. Its protection uses the same selected cipher families as the main vault, but a different construction: file data passes through successive XTS encryption layers, while VMK shares are individually protected using key wrapping.

In order to unlock the VMK we need the KEK or key encryption key. This is derived from a device secret (the device root), an enrollment token, and the user-entered credential. KEK is shorthand here for the key-encryption material: the implementation derives a separate wrapping key for each share. This allows the user to alter their key, and we can just re-encrypt the VMK with the new KEK to allow a new key to unlock the same vault.

## Ensuring the VMK is secure.
As mentioned the VMK is encrypted and only ever exists plain in RAM. When at rest we need to ensure that the VMK is as secure as the rest of the vault to ensure it does not represent a weakness. The user data on Fuse Vault is secured via a user defined stack of encryption algorithms and so a similar approach is required here.

The VMK is first split into n parts with n representing the number of encryption algorithms the user selected. This is done through generating 32 random bytes (A) and XORing them with the VMK. This leaves us with a random A and B such that A XOR B = the VMK. We can then repeat this process until we have enough parts. If only one encryption layer is selected, the sole share is the VMK itself and no random splitting is needed. For example we can then take B, generate a random number C and then XOR C with B to get B'. At this point B' XOR C XOR A gives us the VMK.

With these parts A, B', and C we can then encrypt each one with one of the encryption algorithms the user has selected. With independently random shares, sound key derivation and a correctly functioning implementation, the VMK remains hidden while at least one share remains confidential. A weakness or trapdoor in one selected cipher need not expose the other shares. With only one selected cipher, there is no second cipher protecting a remaining share if that cipher fails; repeating the same cipher does not provide diversity against a failure of that cipher family. We can apply HKDF to the KEK to create derived keys for each encryption algorithm, so recovering one wrapping key should not reveal the other wrapping keys. This addresses a weakness in a cipher, rather than malicious firmware that can directly read secrets in RAM. The wrapping keys still share the SHA-256-based derivation described below.

### Deriving the wrapping keys

The gist is: the device root, current enrollment token and user credential are all needed to unlock the VMK. Changing the credential changes its protection without changing the VMK or re-encrypting the files.

The current implementation does this in stages:

1. **Bind the vault to the device and enrollment.** HMAC-SHA-256, keyed by the device root, combines the token, token-slot number and vault identifier with a purpose label to produce a binding secret.
2. **Bind the credential to that context.** A second HMAC uses the binding secret to combine the user credential and header context. This produces the secret input for the next step.
3. **Make each credential guess expensive.** PBKDF2-HMAC-SHA-256 performs 60,000 iterations using that input and a salt built from a purpose label and the header's random salt. PBKDF2 is a deliberately slow password-based key derivation function: it makes each evaluable guess require repeated computation. The device's persistent attempt limit is a separate protection.
4. **Derive keys for separate purposes.** HKDF-SHA-256 derives a wrapping key for each share and a separate authentication key for the VMK envelope. HKDF is a fast key derivation function, using distinct labels and context to separate the outputs; it is not the expensive password-hardening step.

The envelope includes an HMAC-SHA-256 authentication tag covering its header and wrapped shares. HMAC is a keyed check for modification, not another encryption layer: it does not conceal a share if its wrapping cipher is broken. Sector data also has HMAC authentication, using a separate key derived from the VMK.

The permanent device root has a second role: deriving the key that authenticates the internal flash journal containing attempt counts, policy and enrollment state. That journal is authenticated, not encrypted. The root remains across vault destruction; the independently random enrollment token is destroyed and cannot be regenerated from the root. These are the current implementation choices, separate from the proposed allocation changes below.

## Handling the device secret and enrollment token
Both the device secret and enrollment token are stored in OTP. OTP is one time programmable storage, starting as all 0s and allowing the system to update bits to be 1s. Unlike ordinary flash, OTP cannot be erased and rewritten: programmed bits cannot be unset. That is why it holds the enrollment token and irreversible revocation markers. After its secret rows have been programmed to all ones, the old token cannot be restored by rewriting those rows. Additional zero-to-one changes are still possible while programming access is allowed, so OTP access controls are needed to prevent unauthorized changes. This does not revoke a copy of a token extracted before destruction. Additionally the OTP can also have access controls and debug restrictions. The device root is stored in OTP page 16 with ECC. Pages 17 to 24 are used for enrollment tokens (8 slots).

### Proposed improvements (not implemented)

Remove the permanent device root. Each enrollment will have one independently random 32-byte OTP token. Destroying an enrollment abandons its vault, attempt count, failure policy and header reference; no journal continuity is required between enrollments. The descriptions above reflect the existing implementation, which still uses a permanent root.

The enrollment token will support two separate purposes:

- **Vault protection:** combine token-derived binding material with the user credential to derive the VMK wrapping keys. Preserve the existing intent of credential hardening, separate wrapping keys and envelope authentication, without a permanent root as an additional input.
- **Journal authentication:** derive an HMAC key from the token using a distinct purpose label and enrollment context. This key authenticates the internal flash journal before the user unlocks. It must be separate from vault-binding and wrapping keys.

The OTP mask, rather than the journal, identifies the current token and irreversible revocation state. The journal stores changing state for that enrollment, including attempts, policy and the accepted vault-header reference. A new token gets a fresh journal; an old enrollment's records must not authenticate under the new token. Exact derivation labels, context encoding and format identifiers must be defined and tested before implementation.

#### OTP allocation and status mask

With no root page required, the proposed allocation is:

| Pages | Purpose |
| --- | --- |
| 0–2 | Reserved for chip and boot configuration |
| 3 | One raw enrollment-status mask |
| 4–60 | Enrollment-token secrets |
| 61–63 | Reserved for hardware access keys and page-lock configuration |

Each page has 64 rows. A 32-byte token uses 16 ECC rows, so four token secrets fit per page when status is stored separately. Pages 4–60 provide 57 × 4 = **228 token slots**, including the initial enrollment. The mask uses three bits per token, or **684 of the 1,536 raw bits** in page 3.

Use a single mask, with writes read back and verified. Two copies introduce conflicting sources and interrupted-copy updates without identifying which copy is correct. Redundant copies are not part of this proposal.

| Bits | Meaning |
| --- | --- |
| `000` | Unused |
| `100` | Selected; enrollment started but not completed |
| `110` | Token written, verified and ready for use |
| `111` | Revoked; finish overwriting its secret rows with ones if necessary |

A token must reach `110`, with that marker read back and verified, before it is used to create a vault. A slot left at `100` after interruption is abandoned even if its secret might have been fully written. Consuming that slot is preferable to trying to recover an uncertain token.

When scanning for the current enrollment:

- `111`: never select it. Check that its secret rows are fully burned and finish destruction before allowing a new enrollment. Cleanup must not require the revoked token's journal to authenticate, because the token may already be partly destroyed.
- `000`: a candidate for a fresh enrollment only after confirming there is no existing current enrollment or unfinished destruction. Verify the corresponding secret rows are blank before claiming it.
- `110`: identifies a completed token, not necessarily a completed vault. Authenticate its journal with the token-derived journal key and determine the vault's setup state.
- `100`: an interrupted enrollment. Mark it revoked, finish destroying its secret rows and then advance to a fresh slot.
- Unexpected bit patterns, unreadable status or conflicting current slots: report a fault rather than guess or reset attempts.

#### Journal initialization and interrupted setup

Only explicit new-enrollment setup may initialize a fresh journal. A missing or damaged journal for an existing active vault must deny access; it must never silently become a new record with zero attempts.

The ordering between mask updates, journal initialization and vault creation still needs to be specified. In particular, interruption after `110` but before journal initialization must be distinguishable from loss of an active vault's journal, or handled conservatively without resetting attempts. The three-bit mask alone does not distinguish those cases. This is a remaining recovery-design question, not a reason to retain a permanent root.

Destruction makes the OTP revocation marker authoritative before burning the token. After burning starts, the old journal may no longer be verifiable. Complete destruction using OTP state, then initialize a new journal only as part of explicitly setting up the next enrollment.

#### Compatibility and protection scope

Accept an incompatible format change for the two existing development units; preserving their vault contents is not a requirement for this proposal. A general user migration system is not required. Previously programmed OTP rows cannot be cleared or treated as unused: reusing those boards will require identifying and excluding occupied slots, or using fresh hardware for the clean allocation. The 228-slot capacity assumes a fresh token area.

Attempt limits must survive normal resets and SD replacement or rollback. Protection against an attacker restoring the device's internal flash is outside scope. HMAC detects forged journal contents but does not independently prevent replay of an older valid journal from the same enrollment. OTP access restrictions, trusted firmware and debug protections remain necessary to protect the token.

These changes are documentation of the intended design only. Root removal, new derivations, the mask layout and recovery behavior have not yet been implemented or tested.
