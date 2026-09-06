# Password and entry-method changes

Implemented 2026-09-06; hardware validation remains pending.

## User flow

Choose Settings from the mode menu. Settings is available in both storage-only
and FIDO-enabled builds. Selecting it requires a fresh normal device unlock:
attempt reservation, verification and durable success accounting all run before
the settings menu opens. No storage or FIDO USB interface is attached in settings.

- **Change password** keeps the current entry method.
- **Change entry method** chooses number wheels, directions, keypad or word list,
  then enrolls a new secret using that method. It does not translate a secret
  between different encodings.
- Enter the new secret twice independently. A mismatch changes nothing and does
  not consume an unlock attempt.
- Review the method and confirm Save. Back cancels before saving. Within a
  secret picker, Back first edits/removes input; Back on an empty picker cancels.
- Saving shows a busy screen and accepts no further button actions. Success
  clears the session, displays “Password changed”, and requires another unlock.
  Files, encryption stack and FIDO credentials remain unchanged.
- An ambiguous I/O failure faults closed. Restart selects the credential that
  actually committed; it never guesses from the newest SD header.

This is a change of a known secret, not forgotten-password recovery. Password
practice, resets, deletion and read-only storage are separate future features.

## Ownership and extension points

- app.c owns top-level mode selection, authentication, attempt accounting,
  settings admission, locking and faults. State transitions emit commands.
- settings.c owns the settings item registry, navigation and temporary
  enrollment state. Stable action IDs do not depend on menu order. The active
  entry method remains unchanged until durable success.
- app_view.c renders ordinary application states; settings_view.c renders
  settings, including a scrolling three-item menu window. Both produce
  fv_ui_view_t; neither performs persistence or USB operations.
- input_bindings.c maps physical controls by state and draft entry method.
  Direction/word choices and confirmation presses do not auto-repeat.
- ui.c draws the view without understanding settings workflow or cryptography.
- device_runtime.c executes commands and owns session keys. Its optional
  synchronous presentation callback lets the hardware/simulator draw the busy
  screen before blocking work. Presentation failure aborts the change; callbacks
  must not dispatch input or re-enter the runtime.
- credential_change.c validates the authorized operation, rewraps the existing
  VMK, verifies it, and commits metadata. It never creates a replacement VMK.
- Device and host services share the header staging/selection implementation.

Add future settings through the item registry and explicit states/handlers.
Keep security admission/teardown in the application, platform effects in runtime
coordinators, and text/layout in views. Do not add host-controlled settings
commands or attach MSC while editing credentials.

## Persistent transaction

The existing authenticated 256-byte journal has a new v3 encoding, readable
alongside v1 and v2. It preserves the attempt state and FIDO anchor, and adds:

| Bytes | Value |
|---|---|
| 83–90 | Little-endian committed vault-header sequence |
| 91–122 | The committed canonical header's 32-byte authentication tag |
| 123–127 | Reserved, zero on write |

A zero in-memory header sequence denotes a legacy unanchored journal; v3 records
require a nonzero sequence. Both journal authentication tags protect the anchor.
An ordinary journal append must preserve it or advance its header sequence by
exactly one. Clearing it, decreasing it or changing the tag at the same sequence
is rejected. The tag binds the complete canonical header, so an abandoned stage
at the same sequence cannot substitute for a later committed change.

The change transaction is:

1. Load the authenticated current header and journal after normal unlock.
2. For legacy vaults, commit and read back an anchor to the current header
   **before changing either SD copy**.
3. Rewrap the existing VMK using fresh salts/nonces and the new secret/method.
   Retain the vault ID and encryption stack. Never reduce existing KDF costs.
4. Open the candidate envelope and verify it yields the same VMK.
5. Write, sync and read back the header in the slot opposite the committed one.
   It may replace an abandoned stage. It must not overwrite the committed copy.
6. Append and read back the new journal anchor. This is the commit point.
7. Load the header through the normal committed-header path and verify its anchor
   before reporting success and clearing keys.

Before commit, boot uses the old header even if a newer complete stage exists.
After commit, boot requires the exact new sequence/tag. An old SD image cannot
restore the old password. Corruption or absence of the committed copy fails
closed; an older password is never used as a fallback. A lost acknowledgement
after commit may leave the new password active despite a fault screen.

The protection covers SD replay, not invasive rollback of both internal flash
and firmware. Firmware predating journal v3 must not be installed after migration:
the signed-update/firmware anti-rollback release policy must enforce this.
Keeping old software unable to bypass the new anchor is a hardware release gate.

## Verification

The host regression suite includes:

- Real state-machine and runtime changes across all four methods and back again,
  including failed-unlock retries and unchanged file data/VMK/FIDO snapshot keys.
- The graphical simulator's actual debounced input mappings through method
  selection, enrollment, confirmation, reboot and recovery of an existing note.
- Independent python-fido2 assertion verification for resident and non-resident
  credentials after password changes, under both PIN/UV protocol versions.
- Cancellation, mismatch, sensitive-draft clearing, failed display presentation,
  random-source failure, SD read/sync failure and USB-detach failure.
- Every byte cut from 0 through 512 across a staged SD sector and across the two
  journal appends in the first change (1,026 cases), followed by boot and unlock.
- Retry after an abandoned stage, replay of the original card and replay of an
  abandoned header at the same sequence, plus anchor-clear rejection.

Physical display/buttons, KDF latency, real SD/internal-flash brownouts and
secure-boot/firmware rollback validation remain hardware acceptance work.

Verification on 2026-09-06: all 32 FIDO-enabled host checks passed, including the
new credential-change test and expanded simulator/reference-client tests. The
same 32 checks passed with AddressSanitizer and UndefinedBehaviorSanitizer
(leak detection disabled in this environment; pre-existing SD CRC conversion
warnings were demoted from errors for that build). Storage-only and FIDO-enabled
RP2354 firmware both compiled and linked. The nine workflow screens were rendered
with the actual framebuffer renderer and inspected at the device resolution.
