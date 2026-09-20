# Initial provisioning check — prepared 2026-09-20

Target new sample 66ED2A91873CF67F only; preserve original failing board.
User supplied two passing worker-check outputs for the prior non-provisioning
build and reported all good after the requested post-flash/cold-start checks.
Both outputs showed blank root/tokens/journal, open_result=boot_recovery=1.

New artifact: build-pico-v2-provision-check/v2_provision_check.uf2
SHA256: 3177125a8d80a9a46d23880df5afa14bfabac5440921c70ecc1b55eba6956316
SDK 2.3.0 Release; enrollment and OTP inspection ON; deferred startup and trace OFF.
Build passed, UF2 bounds below journal verified. Host Python compilation and
shell syntax checks passed. No new board commands or provisioning performed yet.

Procedure:
1. Flash only the new working sample. Run worker_check --expect-unprovisioned.
2. Fully unplug/replug and repeat that check before programming OTP.
3. Run bash tools/provision_new_sample.sh. It checks virgin state, captures a
   redacted OTP occupancy/lock summary, sends one PROVISION for the exact serial,
   records state and a second snapshot/diff. Logs go to a unique results directory.
   It never sends PREPARE_FLASH, CREATE, DESTROY, WRONG, or any SD test.
4. Fully unplug/replug; run worker_check --device 66ED2A91873CF67F --expect-empty.
   Requires recovery/open/journal success, EMPTY status, slot 0, no attempts/pending,
   occupied root/token/journal. Boot recovery remains the pre-provisioning value
   until restart, so --expect-empty is specifically for the post-restart check.
5. Stop for review before creating a vault or further enrollment operations.

Expected OTP changes are pages 16 and 17 only, with unchanged access locks.
Provisioning writes device-root/token OTP and the initial flash journal. OTP
writes are permanent. Baseline and follow-up snapshots contain counts/locks,
not key contents. On any failure, stop and preserve logs; do not retry blindly.
