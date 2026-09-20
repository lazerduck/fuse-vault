# Read-only BOOTSEL comparison — 2026-09-20

Working sample: 66ED2A91873CF67F.
Original failing sample: 317741A1459A6F94.

Sources: working-board-boot-state-complete-20260920.json and
original-board-boot-state-20260920.json. All four reads returned success on both.
The initial working-board snapshot had an unsupported wildcard selector; the
complete snapshot uses explicit row numbers and contains all 128 rows.

## Findings

- Raw OTP rows 0x0040–0x00bf (pages 1 and 2) are all zero and identical,
  including redundant copies, boot flags, critical configuration and signing hashes.
- Selected permanent locks match exactly: pages 0, 1, 2, 16–24, 59 and 60.
- Neither board has a flash partition table.
- Both report A4, QFN60, ROM gitrev 0xa8bfe860, 2 MiB flash, ARM default,
  secure boot disabled, and normal/secure debugging enabled.
- Installed images differ as expected: working sample has V2 startup-only;
  original has ALIVE built for pico2. Both report SDK 2.3.0 and valid ARM Secure
  image metadata without extra image security. Metadata is not a bytewise image
  verification. Chip IDs and per-boot random values naturally differ.

## Interpretation and limits

No evidence supports accidental programming of boot settings/signing keys or
changes to the selected OTP locks. The current measurements agree with earlier
application-level blank-page summaries. Application OTP contents, all other OTP
pages, flash-chip status/configuration, full flash contents and electrical state
were not compared. These results do not prove a physical defect or exclude every
persistent-state mechanism. Manual BOOTSEL entry does not preserve a diagnostic
record of the preceding failed cold start; zero current boot diagnostics are not
proof that the cold boot succeeded.

All commands were info, partition info, or otp get. No reset, reflash, OTP write,
SD command, provisioning or recovery was performed during collection.

## Normal restart after read-only comparison

User unplugged/reconnected the original sample without BOOTSEL. Elevated lsusb
showed neither ALIVE nor BOOTSEL; no serial/by-id port existed. The ALIVE probe
stopped at identity validation before sending any ping. Missing-USB behaviour
therefore persists after the read-only inspection. CPU execution state remains
unknown; this does not independently identify a hardware failure.
