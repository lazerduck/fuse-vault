# V2 software completion and polish

## Direction agreed 2026-09-21

The user will obtain a V2 board with a working screen, print a case and covers,
and use it day to day. Defer physical endurance testing to that board; keep the
unverified hardware gates visible without making them a prerequisite for software
development. Daily use supplies practical evidence; controlled interrupted-write
and other fault tests remain separate acceptance work.

FIDO core functionality is delivered, with user-reported GitHub and Twitter
success. Preserve existing credentials, encrypted formats and configurable
verification reuse. Do not initialize storage or enroll/flash hardware as part of
software polish. See the [FIDO ledger](v2-fido-progress.md) for detailed evidence
and the [production checklist](production-checklist.md) for release gates.

## Ordered software work

These are planned work packages, not claims of missing implementation. Begin each
by inspecting existing behavior and only implement the gaps. Record changes,
relevant automated validation and the next action here as work proceeds.

| Step | Scope and completion criterion | Status |
| --- | --- | --- |
| P1 | UI consistency: review home, unlock, settings, passkey approval and management; make focus, Back/cancel, long labels, busy states and error recovery clear and consistent on the physical screen dimensions. Verify shared UI behavior with desktop tooling. | FIRST PASS COMPLETE — 2026-09-21 |
| P2 | Everyday controls: review storage/FIDO status and verification-setting explanations; improve visibility of current state and consequences of lock, credential changes and destructive actions. Preserve deliberate defaults and confirmation behavior. | PLANNED |
| P3 | Passkey management polish: review browsing with many credentials, account/site disambiguation, long labels, deletion feedback and empty/full-store errors. Keep deletion targeted to the selected credential. | PLANNED |
| P4 | Software readiness: reconcile stale documentation, document normal setup/use/update behavior and limitations, review development versus release build controls, and run relevant regression/build checks. Track any security work that cannot be completed without hardware separately. | PLANNED |

Larger new features should be scoped individually when requested; this plan does
not add credential export, new authentication protocols or change the storage
threat model. Production boot/debug protections and security review remain real
work, even after UI polish is complete.

## Current handoff

- Visual follow-up (2026-09-21): FIDO approval, passkey management, verification,
  destructive confirmations, setup menus and error screens now use shared headers,
  separators, highlight bars and compact icon footers. The first pass primarily
  improved behavior; this follow-up addresses the visual inconsistency the user
  identified. Latest preview/evidence: [unified UI](../../results/ui-style-20260921.md).
- Button convention: Up/Down moves through choices; Left/Right changes the
  selected value or pages long text; Select enters/accepts; Back exits a menu and
  locks at unlocked home. FIDO Back still rejects/cancels. Credential entry keeps
  its method-specific direction mapping; normal pattern Back deletes an input.
  Locked home retains Left/Right flip (also available at unlocked home).
- Failure policy now has selectable Attempts / Action / Review rows. Encryption
  layers use Left/Right to change the selected cipher and an explicit Remove last
  layer row; navigation alone never applies a policy or confirms destruction.
- Priority moved from immediate F5 hardware testing to software completion/polish.
- P1 first pass implemented and reviewed across 30 synthetic screen states using
  the actual C renderer: nested Back/cancel behavior, bounded passkey label paging,
  settings position, operation-specific busy text, readable confirmations and
  accurate FIDO cancellation hints. Credentials and persistent formats unchanged.
- Evidence and preview: [UI polish results](../../results/ui-polish-20260921.md).
- Next action: P2 status/feedback review. Generic error codes still need a deliberate
  worker-to-UI error classification before offering more specific recovery advice;
  do not infer a failure cause from overlapping subsystem integer codes.
- Physical readability/button feel remain for the V2 board. Synthetic previews
  and host tests do not establish those hardware properties.

## Offline UI review

Run `python3 src/host/tools/ui_preview.py` from the repository root. Requires a host C
compiler and Pillow. The tool compiles the actual UI renderer with synthetic
fixtures, without firmware, USB or vault access. It writes a 30-screen contact
sheet and an HTML gallery with native-size and enlarged views to
`/tmp/fuse-vault-ui-preview`. Use `--output PATH` to select another output folder.
The fixture source is `src/host/tools/ui_preview.c`; behavioral navigation and authorization
checks remain in `src/host/tests/device_ui_tests.c` and the FIDO adapter tests.
