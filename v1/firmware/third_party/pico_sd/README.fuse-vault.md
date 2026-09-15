# Pinned native SD transport dependency

Source: https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
Revision: d5e453404cdbfaa55ab30d285b6ab0b730e84a05
Imported `src/` and root LICENSE; upstream source files unmodified.
Upstream Apache-2.0 and bundled component notices/licenses are preserved,
including SdFat, ZuluSCSI and FatFs notices in their source directories.

Fuse Vault uses the raw `sd_card_t` block operations only. No FatFs filesystem
is mounted by this integration. CMake builds the upstream library; unused
filesystem code is removed by section garbage collection. The native backend
is an optional bench build and is rejected by the production build gate.

Configuration: one card, PIO1 dedicated to SD, shared DMA IRQ1, CMD=5,
DAT0..3=6..9, CLK=4 (D0-2), 25 MHz data clock / 400 kHz initialization.
The upstream driver negotiates four-bit mode and checks command/data CRCs.
The adapter additionally completes writes and checks card status, bounds
requests to the upstream DMA array capacity, and halts DMA on failures.
SDHC/SDXC sector-addressed cards only in this backend. Tests cover the adapter
contract with mocked transport; native electrical behavior still needs testing.

When moving from SPI to native SD, fully remove power after flashing: an SD
card latched into SPI mode needs a power cycle to return to native mode.
