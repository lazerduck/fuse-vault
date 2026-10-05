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
The device root is stored at page 16 but there does not seem to be a reason for this. We should leave 0-1 reserved, page 2 for the secure boot and then put the device root into page 3. Page 4 would hold token status, and pages 5 to 60 would hold enrollment tokens. Existing devices already using page 16 for the root require an explicit compatibility or migration plan; their OTP cannot simply be relocated or cleared.

Currently each token uses one page, which is inefficient. Each page has 64 rows; each ECC enrollment token uses 16 rows, so four token secrets could fit in one page if their status is stored separately. The proposed page-4 raw status mask would hold three bits per token:

| Bits | Meaning |
| --- | --- |
| `000` | Unused |
| `100` | Selected; enrollment started but not completed |
| `110` | Token written, verified and ready for use |
| `111` | Revoked; finish overwriting its secret rows with ones if necessary |

This gives 224 token slots: 56 pages (5–60 inclusive), with four tokens per page. The mask requires 672 of the 1,536 raw bits in page 4. Two copies would fit, but duplication only detects disagreement; it does not by itself tell us which copy is correct. Whether to use redundancy, and how to handle interrupted updates to it, remains undecided.

The proposed rule is that a token must reach `110`, with that marker read back and verified, before it is used to create a vault. A slot left at `100` after interruption is abandoned even if its secret might have been fully written. We accept consuming that slot rather than trying to recover it.

When scanning for the current enrollment:

- `111`: never select it. Check that its secret rows are fully burned and finish destruction before allowing a new enrollment.
- `000`: a candidate for a fresh enrollment only after confirming there is no existing current enrollment or unfinished destruction. Verify the corresponding secret rows are blank before claiming it.
- `110`: a candidate for the current enrollment; reconcile it with the authenticated journal, which also distinguishes a prepared token from a completed vault.
- `100`: an interrupted enrollment. Mark it revoked, finish destroying its secret rows and then advance to a fresh slot.
- Unexpected bit patterns, unreadable status or conflicting current slots: report a fault rather than guess or reset attempts.

This proposal needs a defined ordering between OTP status updates and journal commits. The current implementation has separate completion and activation markers and commits an EMPTY enrollment record before setting activation. Replacing that sequence must handle any journal reference to an abandoned slot without resetting attempts or discarding an existing active vault. An interruption after `110` but before vault setup completes also needs defined recovery. The three-bit proposal and its recovery rules have not yet been implemented or tested.
