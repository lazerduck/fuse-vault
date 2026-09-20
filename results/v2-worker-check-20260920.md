# V2 worker initialization test

Target: new working sample 66ED2A91873CF67F only. Preserve the original sample.

Artifact: build-pico-v2-worker-check/v2_worker_check.uf2
SDK 2.3.0, Release, fuse_vault / rp2350-arm-s.
STARTUP=OFF, BOOT_TRACE=OFF, ENROLLMENT=OFF, OTP_INSPECT=OFF.
Normal worker launches automatically. On virgin enrollment it reads OTP and
reserved flash, reports unprovisioned, then waits for commands. No provisioning
is automatic. Existing enrolled devices may perform recovery writes on startup;
this artifact is intended for the new, unprovisioned sample in this test.
Provisioning/destruction debug commands are disabled. This is not a globally
read-only firmware: the legacy explicit SD test remains present but is not sent.

Build passed. UF2 payload bounds verified below the reserved flash journal.
SHA256: aed066c46bf932e915cf611a74728767f647e985c33bff4aa6100a718e668b91
Host checker compiled and CLI help checked; physical testing pending.

## User check

After flashing, run:

```sh
python3 /home/adam/projects/fuse-vault/tools/worker_check.py --device 66ED2A91873CF67F --expect-unprovisioned
```

This uses 5-second transaction timeouts and only INFO and STATE. Both are handled
by the worker, so a response proves more than USB enumeration. The expected blank
state is open_result=1, boot_recovery=1, root_blank=tokens_blank=flash_blank=1.
The command prints JSON and PASS, exits 0 on success, or FAIL/nonzero otherwise.
No automatic retry or provisioning is performed. The prior deferred-worker build
cannot pass this check while its worker remains stopped.

Repeat the same command after completely unplugging for five seconds and
reconnecting without BOOTSEL. Record post-flash and cold-start results separately.
A pass establishes worker responsiveness and blank enrollment, not crypto/SD
throughput or reliability of future provisioning operations.
