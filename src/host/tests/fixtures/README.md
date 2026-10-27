# Envelope vectors

`envelope_vectors.h` contains deterministic test-only vectors generated independently
of the firmware. Regenerate them from the repository root with
`python3 src/host/tests/envelope_reference.py`, or verify the checked-in bytes with
`python3 src/host/tests/envelope_reference.py --check`.

Generation requires Python `cryptography` with Camellia and SM4 support, plus the
OpenSSL CLI with `aria-256-ecb` support. ARIA uses OpenSSL directly and is never
silently skipped. The C tests consume the checked-in header, so they do not need
the generator's dependencies.

Vectors cover each cipher individually and mixed stacks, including ARIA in the
middle and last positions. The invalid-wrap vectors have a valid envelope MAC,
so they exercise share-unwrapping rejection rather than MAC rejection.

# FIDO 64 KiB migration fixture

`fido64-image.bin` was generated with the pre-expansion engine and simulated
platform in `src/host/tests/fido_engine_tests.c`. It contains two resident credentials
and one nonresident registration for `migration.example`, using the test-only
root key `{1, 0, ...}` and device ID `{2, 0, ...}`. These are synthetic credentials,
not real accounts or device secrets. The associated public credentials are in
`fido64-credentials.json`.

The migration test checks their independent ES256 signatures, resident discovery,
and continued operation after a new registration and reopen. Store tests wrap
the image in genuine v1 encrypted snapshots and exercise interrupted upgrades.
