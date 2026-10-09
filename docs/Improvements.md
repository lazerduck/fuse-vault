# Planned improvements

This collects the changes we intend to make following the review of the current implementation. The first four sections give the intended order of software work. Hardware, enclosure and recovery work follow as separate areas to develop. The linked pages explain the existing behaviour and the design details behind each change. These changes are pending implementation and validation.

## 1. Expand enrollment capacity and simplify provisioning

Replace the current permanent device root and eight enrollment slots with one independently random token per enrollment and a separate OTP status mask. This gives us more opportunities to destroy a vault and start fresh, while simplifying which secrets are needed to protect it.

The proposed allocation is:

| OTP pages | Purpose |
| --- | --- |
| 0–2 | Reserved chip and boot configuration |
| 3 | One raw status mask, with three bits per enrollment slot |
| 4–60 | Four 32-byte enrollment tokens per page, using 16 ECC rows each |
| 61–63 | Reserved hardware access keys and page-lock configuration |

This provides **228 enrollment slots on a fresh token area**. Use a single mask and verify each write by reading it back. Its states are `000` unused, `100` started, `110` token complete and `111` revoked.

Derive separate vault-binding and journal-authentication keys from the enrollment token, with distinct purpose labels and context. A fresh enrollment gets a fresh journal, attempt count and policy. Destroying the token removes access to its vault. The mask identifies the current token and irreversible revocation state.

Before implementing this, finish the ordering and recovery design for token provisioning, journal creation and vault activation. A token left at `100` is abandoned and destroyed. A revoked token must have its secret rows fully burned before advancing. A missing journal for an active vault must deny access and preserve attempt enforcement. The design still needs to resolve interruption after the token reaches `110` but before its journal is initialized.

Accept an incompatible format change for the two development units. Identify and exclude previously programmed OTP rows when reusing them; their remaining capacity will depend on what is already programmed.

As part of the setup changes, remove permanent lockout as a selectable failure action until a useful, secure recovery design exists. Keep the configurable attempt limit and enrollment-token destruction. Decide explicitly how existing lockout-policy records are handled.

Completion should include interruption tests through provisioning, activation and destruction, plus checks that resets and SD replacement or rollback cannot restore attempts. The detailed proposal is in [Key handling](KeyHandling.md#proposed-improvements-not-implemented) and [Setup](Setup.md#improvements-not-yet-implemented).

## 2. Access FIDO credentials from the SD card on demand

Replace the complete 128 KiB in-memory FIDO image with storage that reads and updates records on the SD card as needed. Credentials already persist encrypted on the card; the change is to keep only the working records and a bounded cache in RAM during use.

The goals are to support substantially more resident passkeys and release RAM for other work. Capacity should primarily depend on the allocated SD space and record format. We should choose a capacity target after measuring record overhead and the indexing needed to find, list and delete credentials. The existing 1 MiB reservation may also need to grow.

This requires changes to the engine's memory-based storage interface and the current whole-image snapshot format. Define how records are indexed, authenticated, updated and recovered after interruption. Keep indexes and caches bounded so their memory usage remains manageable as the credential count grows.

Preserve VMK-derived protection, authentication before records are used, durable updates before reporting success, on-device verification and approval, and the existing delete and reset behaviour. Define the format transition for existing stores and retain the documented SD rollback protection scope.

Validate capacity, registration, signing, listing, deletion and reset with a large store. Include corruption and interrupted-write cases, and measure both peak RAM use and response time on hardware. See [FIDO2](FIDO2.md).

## 3. Review larger USB read and write buffers

After the FIDO storage change, measure how much RAM is available and consider increasing the two USB storage buffers from their current 32 KiB each. Retain enough headroom for stacks, cryptographic contexts, FIDO operations and error handling under combined workloads.

Larger batches may reduce per-batch overhead and improve the overlap between USB transfers and storage work. Compare several sizes on the device before choosing one. Measure read and write throughput, RAM use and responsiveness during FIDO requests, cancellation and locking.

The batch limit appears in the storage and command paths as well as the buffer allocations. Review these together, including bounds checks and cleanup of the full buffers. Select a larger size only where the measurements show a useful benefit. The current pipeline is described in [File storage](FileStorage.md#buffering).

## 4. Review encryption and decryption performance

Make a focused performance pass over the storage path. Measure time spent in SD access, encryption, decryption, HMAC calculation and memory copies across representative cipher stacks. Use this to identify where an optimization would make a practical difference.

Potential work includes removing unnecessary copies, reusing prepared cryptographic contexts where safe, improving batching and reviewing compiler or backend choices. These are candidates to investigate; the measurements should determine which changes we make.

Preserve the selected cipher stack, key sizes, key separation, sector authentication and verification before plaintext release. Review any backend change for timing or other side-channel implications. Keep the credential derivation's deliberate cost separate from this storage-throughput work.

Check optimized output against the existing implementation and cryptographic test vectors, exercise corruption and error paths, and benchmark on the actual device. Record gains separately for reads, writes and each tested stack.

## 5. Improve the physical input

Replace the large joystick with a flatter control that gives more definite directional input and a clear confirmation action. Credential entry and approval should feel predictable, with fewer ambiguous presses or accidental direction changes.

Try candidate controls on actual hardware before committing to the board layout. Check direction patterns, number and word selection, menu navigation and confirmation, including how the control feels through the case. Choose the control together with its mounting and enclosure clearance.

## 6. Create a smaller board revision

Design a new board revision around the chosen input and review the other components and their placement for opportunities to reduce the device's size and thickness. One of the current inductors is particularly large and should be included in that review.

Check replacement components against their electrical requirements, including current capacity, temperature and power-supply behaviour. Establish the component heights and connector positions early enough to guide the case design, then validate the revised board under realistic storage and FIDO workloads.

## 7. Continue the case redesign

Keep developing the current case, which is a useful starting point. Improve the appearance and fit of the end caps and reduce the overall thickness.

In particular, reduce how far the case extends below the USB connectors. The current shape can interfere with plugging the device into laptops where there is little clearance beneath the port. Check the fit with representative laptops and other hosts, including nearby ports and the surface the computer rests on.

Coordinate the case with the flatter input and revised board, while keeping enclosure work as its own design task. Prototype the connector clearance, input access and end-cap fit before settling on the final shape.

## 8. Design recovery options

Decide how users could recover access when the device holds important files and passkeys. Define which situations we intend to support: a lost or broken device, a damaged or lost SD card, a forgotten credential, or enrollment-token destruction after exhausting attempts. These cases may need different provisions made while the vault is still accessible.

Compare possible approaches, such as a separately protected backup, recovery material held by the user, or a second device. Establish what each approach would recover, what the user must retain, and who could gain access if that material were obtained. For passkeys, include the account information and supporting FIDO secrets needed to use the credentials on a replacement device. Restoring content also requires a surviving copy of that content.

Resolve how recovery interacts with the attempt limit and token destruction. A recovery route that survives destruction would change its meaning: destruction would end access through the original enrollment, while separately held recovery material could retain access. That choice needs to be explicit in both the design and the user-facing explanation.

Define authorization for creating and using recovery material, how backups stay current, and what happens to deleted passkeys when an older backup is restored. Test recovery on replacement hardware before presenting it as a supported feature.

This is a design investigation. No recovery mechanism has been selected, and the current destruction behaviour remains as described in [Setup](Setup.md#reaching-the-attempt-limit).

## Other work already identified

The documentation review also identified work to complete alongside these changes before treating the device as ready for production:

- **Protect key RAM:** define and implement the hardware-enforced boundary around key handling, including access from both cores, DMA and debugging. See [Session](Session.md#protecting-ram-while-unlocked).
- **Finish cleanup validation:** audit temporary copies, library contexts, cancellation and error exits, and test locking while USB and FIDO work is active.
- **Complete firmware and debug hardening:** finalize trusted-boot, firmware-update and OTP access policies, along with production debug restrictions. The existing [production checklist](AI/production-checklist.md) tracks the detailed work.
- **Qualify randomness:** assess the source and sampling settings under the intended temperature, clock and supply conditions, and validate failure recovery. See [Randomness](Randomness.md#remaining-validation).
- **Complete hardware interruption tests:** verify setup, destruction, FIDO updates and storage/session behaviour during power loss and USB events. Repeat relevant tests as the storage and memory changes land.

Update this document as decisions are resolved and work is completed, and update the corresponding behaviour pages when the implementation changes.
