# FIDO 64 KiB migration fixture

`fido64-image.bin` was generated with the pre-expansion engine and simulated
platform in `tests/fido_engine_tests.c`. It contains two resident credentials
and one nonresident registration for `migration.example`, using the test-only
root key `{1, 0, ...}` and device ID `{2, 0, ...}`. These are synthetic credentials,
not real accounts or device secrets. The associated public credentials are in
`fido64-credentials.json`.

The migration test checks their independent ES256 signatures, resident discovery,
and continued operation after a new registration and reopen. Store tests wrap
the image in genuine v1 encrypted snapshots and exercise interrupted upgrades.
