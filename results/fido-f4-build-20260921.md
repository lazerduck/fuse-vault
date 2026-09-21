# F4 software validation — 2026-09-21

Implemented:

- On-device resident credential list, long-label scrolling, confirmed deletion and
  empty-list state. Stable IDs prevent stale-index deletion. Worker closes the
  local engine management scope after each operation; failed deletion faults/clears
  the engine, and success follows durable encrypted commit.
- Loopback-only Python WebAuthn server with SQLite public records, exact Host/Origin
  checks, session-bound single-use expiring challenges, ES256/UV/UP enforcement and
  user-handle binding. Browser page supports register, account login, discoverable
  account selection, cancel and readable results.
- Preserved existing credentials, verification setting, composite USB and event
  budget. No flashing, physical credential creation/deletion or SD changes performed
  by the agent.

Validation:

- Full desktop CTest suite: **35/35 passed**.
- FIDO/UI ASan+UBSan focused suite: **11/11 passed**, `detect_leaks=0` due to the
  environment's tracing limitation. Final discoverable-account and allow-list
  deletion assertions were rerun normally and under sanitizers and passed.
- WebAuthn verifier tests use the actual C engine and encrypted file-backed store;
  no virtual browser authenticator. Both Alice/Bob assertions verify, including
  getNextAssertion/account selection and server/engine reopen. Negative origin,
  RP, challenge, replay, expiry, session, signature, UV and user-handle cases reject.
- Actual HTTP handler tested without sockets for Host/Origin, content type, static
  routing, cookie attributes and consumption of failed challenges.
- Firmware adapter tests cover local list/delete, unknown/stale IDs, locked denial,
  persistence after reopening, deleted allow-list credential rejection and continued
  signing by the surviving credential. UI tests cover selection, cancel-by-default
  confirmation, stable ID handoff, fresh generation and empty-list behavior.
- Composite FIDO and FIDO-disabled device UI firmware compile/link successfully.
- In-app browser UI inspected using the Browser skill at localhost:8000: page loads,
  controls render and login before registration shows the expected error. Temporary
  server/tab stopped after checking. No physical WebAuthn ceremony claimed.

Artifact: `build-pico-fido/fuse_vault_security.uf2`
SHA-256: `9495546184e2dc179af8721af947fbb6d3c175c03f4f9cb71afb43e3943e9a36`
ELF size: text 415,548 bytes; BSS 347,068 bytes; separate data 0.

User hardware/browser follow-up (2026-09-21): registered Alice and Bob, deleted Bob,
and reported everything working as expected. Supplied screenshot confirms Alice
discoverable login verified by the local WebAuthn server, with UV/UP true and counter
zero. These are user-run physical tests, separate from the agent checks above.
Browser identity and coverage of both Chromium/Firefox, plus explicit rejection of
the deleted credential after reconnect, have not yet been confirmed.

Subsequent real-site user report (2026-09-21): Twitter worked; GitHub showed
"Authentication failed". Exact ceremonies, browser and device prompt behavior
remain unspecified. GitHub failure is an open compatibility investigation; the
screenshot does not establish its cause. Remaining acceptance runbook:
`docs/v2-fido-browser-testing.md`.
