# New sample vault creation and verification

Board 66ED2A91873CF67F passed empty-enrollment preflight after normal reconnect.
CREATE_ERASE_SD succeeded: AES/Camellia test vault, 60,000 KDF iterations,
2,048 sectors / 1 MiB deterministic payload written, 4.771348 s for the full command.
Separate CHECK ORIGINAL succeeded: unlock, authenticate, decrypt and verify all
2,048 sectors, 3.228740 s for the full command. These are whole-command timings,
not isolated crypto or SD throughput benchmarks. Both commands finish locked.

Final state: ACTIVE (2), sequence 6, credential generation 1, token slot 0,
attempts 0, pending false; open/recovery/journal results all zero.
Raw responses: new-sample-vault-{create,check,state}-20260920.json.
No destruction, slot advancement or credential change performed.

Cold power-cycle and repeat CHECK ORIGINAL are pending. The repeat verification
reads the SD payload but its unlock also updates the persistent authentication
journal; it is not a globally write-free operation. Use the known original debug
credential only. The vault contains test data and uses a public debug credential.
