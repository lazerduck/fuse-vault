# First RP2354 security bring-up — 2026-09-17

Board ID: `317741A1459A6F94`. Reported chip revision 3, ROM revision 4,
Pico SDK 2.3.0, CPU 150 MHz, requested SD clock 25 MHz, Pico SHA backend.
Flashed image SHA-256:
`72c125a1cee787c53cb1acbb9618e4b573675674a5f3dd5dea32f032fc05c1fe`.

## Outcome

All measured runs passed: five repeated RNG diagnostics, three full-envelope KDF
timing runs at 60,000 iterations, and three full 1 MiB SD lifecycle tests. Earlier
single RNG and 1,000-iteration pilot commands also passed. The repeated measurements
are preserved in [the raw JSON](security-317741A1459A6F94-20260917.json).

| Measurement | Median |
|---|---:|
| Envelope unlock, 60,000 PBKDF2 iterations | 1.401510 s |
| Reject wrong credential, 60,000 iterations | 1.400780 s |
| Envelope seal, 60,000 iterations | 1.415534 s |
| SD vault create, test count of 1,000 iterations | 55.018 ms |
| SD vault unlock, test count of 1,000 iterations | 26.686 ms |
| Credential change, test count of 1,000 iterations | 57.869 ms |
| Device-only AES → Camellia write | 557.0 KiB/s |
| Device-only AES → Camellia read/verify | 562.9 KiB/s |

60,000 is a measured candidate for the agreed 1–2 second unlock target, not yet a
frozen production KDF policy. Successful and failed opens have similar measured
cost. The 1,000-iteration count remains a deliberately short integration-test value.

Storage tests use 32 KiB batches over 1 MiB. Every run creates the header and
metadata, unlocks, writes, locks/reopens, reads/verifies, changes credential, rejects
the old credential, unlocks with the new one and verifies the complete payload
again. A write completes only after the SD sync. Timing includes local pattern
preparation/verification, but **excludes USB bulk payload transport**. Consequently
these figures must not be presented as an improvement over the earlier USB pipeline
benchmark or as mounted-drive throughput.

## Randomness observations

No terminal hardware health error or timeout occurred in the recorded runs.
Each repeated RNG command collected six 192-bit source blocks, taking
2.833–3.060 ms in total. All generated samples stayed private to firmware.
The early 1,000-iteration pilot reported autocorrelation statistics 16395 (one
internal rejected check and eleven checks), while its operation completed without
a terminal error. Thus “no reported operation failure” does not mean every internal
check passed first time. This is a smoke test of the configured source and failure
reporting, not an entropy assessment or qualification across supply/temperature.

## OTP baseline

- [Before](otp-317741A1459A6F94-20260917-before.json)
- [After](otp-317741A1459A6F94-20260917-after.json)
- [Comparison](otp-317741A1459A6F94-20260917-diff.json): no changes in reported fields.

All 4,096 rows were readable through the inspection path. Programmed data rows
were reported on pages 0, 60, 62 and 63; the other 60 pages had all 64 rows blank.
Page 0 reports permanent read-only Secure/Non-secure/bootloader access. Pages 1,
2, 62 and 63 also have non-default permanent access settings. Public lock-row
redundant copies agree for every page. Existing occupancy is not attributed to
prior application provisioning or factory data solely from these aggregate readings.
Blank/readable pages are not automatically approved application allocations.

No OTP programming or software-lock command exists in this image; none was issued.
Snapshot equality covers occupancy/access/public-lock fields, not bit-for-bit
identity of secret data rows, which the tool intentionally does not export.

## Remaining boundary

The test enrollment and authority snapshots were in RAM, erased at command end.
These runs do not establish restart persistence, flash journal durability,
irreversible enrollment destruction, Secure RAM isolation or production entropy
quality. Next prepare a specific scratch-page allocation and guarded OTP experiment,
using this board's baseline before any irreversible transition. Then integrate
permanent enrollment with the durable flash authority adapter.
