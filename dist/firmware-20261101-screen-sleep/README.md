# Fuse Vault — screen sleep

[Firmware UF2](fuse-vault-screen-sleep.uf2) · [Screen preview](screen-setting.png)
· [Local test output](tests.txt)

Built 1 November 2026 from `cfa7b7d1da8da807fe664457948c1d0787e710c9`
plus the screen-sleep changes in this workspace. Includes the tested lock-screen
layout and security settings. No board was flashed by this work.

## Using the setting

Fully unlock, open Settings → Security, enter the current credential, and move
down to Screen. Left/Right changes the choice; continue to Review → Save.
Saving preserves the vault contents and finishes locked. The choice survives
unplugging and rebooting.

| Choice | Behaviour |
| --- | --- |
| Until lock (default) | On throughout the unlocked session, including the countdown; sleeps 60 seconds after locking. |
| 30 seconds, 1/2/5/10 minutes | Sleeps after that period without display activity. |
| Never | On while powered. |

Existing vaults without a saved display preference now use Until lock. With
auto-lock disabled, this keeps the screen on until the vault is locked another
way. Display refreshes do not renew the auto-lock timer. A button pressed while
the screen is dark or still refreshing only wakes it; press again once visible
to navigate or approve.

This extends the authenticated policy with a display preference. After saving
a timed or Never choice, keep this firmware or a compatible newer version;
older firmware rejects those records, even if the setting is changed back later.
Current-format vaults can be upgraded without re-enrollment.

## Local validation

All 18 targeted tests passed under ASan/UBSan with leak checking disabled.
Coverage includes policy encoding, approval and header matching, saved settings
after OTP-journal reopening, preservation of VMK/data, UI navigation/cancel in
both orientations, TFT sleep/wake deadlines, Never/Until lock modes, button
consumption during refresh, session timeouts, USB invalidation and the FIDO
adapter. The actual renderer's setting and review screens were inspected.

Release build: existing `fuse_vault` RP2354 configuration, SDK 2.3.0, TFT/UI,
USB MSC and FIDO enabled, 32 KiB USB buffers, all debug switches Off. ARM Secure
image ends at `0x1007e518`, below the authority journal at `0x101fe000`.
ELF: 523,540 bytes text, 0 data, 368,152 BSS; +1,432 text and +24 BSS compared
with the lock-screen image. `git diff --check` passed.

UF2 SHA-256:

```text
bacc84fcee353f6abbec95056b55e5aeb023f9510c7d1a5ff09fae58970c4436
```

On the v3 board, check Until lock through a full idle countdown and the following
minute, a timed choice with wake-only input, and Never while automatic locking
still operates. Save a non-default choice and unplug/reconnect to confirm it is
retained. Eject writable storage before waiting for an idle lock.
