# Fuse Vault — three unlock modes

Firmware: [fuse-vault-unlock-modes.uf2](fuse-vault-unlock-modes.uf2)

Built 31 October 2026 from base commit
`196cfe503c22b49f2de90c04af51de596cd71a1f` plus the unlock-mode changes in the
workspace. No device was flashed during this task.

| Locked-screen button | Access after entering your usual credential |
| --- | --- |
| Select | Files read/write and passkeys |
| Up | Passkeys only; no file volume |
| Down | Read-only files; passkeys disabled |

Lock and unlock again to change mode. Eject mounted storage before locking.
Left/Right still flip the screen. A website-triggered unlock opens passkeys only.
The FIDO interface remains discoverable in read-only mode, but private FIDO
operations are disabled.

Flash using the board's existing BOOTSEL procedure and copy the UF2. Use your
existing current-format vault and credential; no erase, setup or new enrollment
is needed for this feature. No new migration for older incompatible development
formats is included.

The firmware uses the existing `fuse_vault` custom-board pin configuration:
RP2354 / RP2350 ARM Secure, physical TFT/UI, USB storage and FIDO, 32 KiB USB
buffers, all `FV_DEBUG_*` switches OFF. This image does not enable production
secure boot or OTP access locks. Board-specific deployment checks remain with
the physical v3 board.

SHA-256:

```text
5dbbceedf20a90992596fbee55fea5e4bcfab9e487b8c3b9461271eb67908c40
```

Picotool verified the RP2350 ARM Secure UF2 and image range
`0x10000000–0x1007ced0`, below the reserved authority journal at `0x101fe000`.

See the [implementation, local validation and board-test checklist](../../results/unlock-modes-20261031.md).
