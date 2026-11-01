# Fuse Vault — security settings

[Firmware UF2](fuse-vault-security-settings.uf2) · [Validation and board checks](../../results/security-settings-20261101.md) · [Screen previews](../../results/security-settings-20261101/screens.png)

Built 1 November 2026 from `9b4037d49526255ddcee3a3b9e9c6c22d0112ba5`
plus the security-settings changes in this workspace. Nothing was flashed.

Fully unlock the vault, select Settings → Security, enter the current credential,
edit the options, then Review → Save. Saving returns the device to locked.

- Destroy keys: On after 1–100 consecutive failed attempts, or Off for unlimited
  attempts. This applies to local unlocking and FIDO verification.
- Auto lock: Off, or 1/2/5/10/15/30 minutes of inactivity, across all unlock modes.
- The dashboard includes a countdown ring and remaining minutes/seconds.

Existing vaults retain their attempt policy and start with auto-lock Off. New
vaults default to ten failures and five minutes. No re-enrollment is needed.

Physical buttons, successful file transfers and successful private passkey
operations reset the timer. Routine USB polling and FIDO discovery do not.
Continued file traffic can keep an inactivity timer alive. Eject mounted storage
before leaving it idle when applications may have cached writes.

Use the board's existing BOOTSEL procedure to copy this UF2. The image uses the
repository's `fuse_vault` RP2354 board configuration, TFT/UI, USB storage and FIDO,
32 KiB USB buffers, and all debug switches Off. It does not change production
secure-boot or OTP access-lock settings. Physical v3 testing remains required.

Older firmware cannot read a saved nonzero timer or disabled-destruction policy.
Keep this firmware or a compatible newer version after saving those options or
enrolling a new vault with this firmware.
Changing the settings back does not make downgrade safe, because incompatible
policy records can remain in journal history. Do not erase or re-enroll an
existing vault as part of this upgrade.

The final image checksum and local test results are recorded in the linked
validation report.
