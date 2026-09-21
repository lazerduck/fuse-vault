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
| P1 | UI consistency: review home, unlock, settings, passkey approval and management; make focus, Back/cancel, long labels, busy states and error recovery clear and consistent on the physical screen dimensions. Verify shared UI behavior with desktop tooling. | NEXT |
| P2 | Everyday controls: review storage/FIDO status and verification-setting explanations; improve visibility of current state and consequences of lock, credential changes and destructive actions. Preserve deliberate defaults and confirmation behavior. | PLANNED |
| P3 | Passkey management polish: review browsing with many credentials, account/site disambiguation, long labels, deletion feedback and empty/full-store errors. Keep deletion targeted to the selected credential. | PLANNED |
| P4 | Software readiness: reconcile stale documentation, document normal setup/use/update behavior and limitations, review development versus release build controls, and run relevant regression/build checks. Track any security work that cannot be completed without hardware separately. | PLANNED |

Larger new features should be scoped individually when requested; this plan does
not add credential export, new authentication protocols or change the storage
threat model. Production boot/debug protections and security review remain real
work, even after UI polish is complete.

## Current handoff

- Priority moved from immediate F5 hardware testing to software completion/polish.
- Next action: inspect P1 flows and identify concrete UI changes against current
  screen rendering and input behavior before editing firmware.
- No firmware changes or new tests were performed when creating this plan.
