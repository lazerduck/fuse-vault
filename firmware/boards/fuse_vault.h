/*
 * Fuse Vault custom-board definition for the Raspberry Pi Pico SDK.
 *
 * This file is intentionally limited to properties confirmed independently of
 * the application schematic. Peripheral pin aliases belong here once they have
 * been checked against an exported schematic or assembled hardware.
 */

#ifndef _BOARDS_FUSE_VAULT_H
#define _BOARDS_FUSE_VAULT_H

pico_board_cmake_set(PICO_PLATFORM, rp2350-arm-s)

#define FUSE_VAULT_BOARD
#define FUSE_VAULT_BOARD_REVISION 1

/* Evidence gates record which electrical facts have been established. Code
 * must not infer unrelated behavior merely from the pin-number aliases below. */
#ifndef FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED
#define FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED 1
#endif
#ifndef FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
#define FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED 1
#endif
#ifndef FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
#define FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED 0
#endif
#ifndef FUSE_VAULT_DISPLAY_CONTROLLER_CONFIRMED
#define FUSE_VAULT_DISPLAY_CONTROLLER_CONFIRMED 0
#endif

/* A production build cannot use the bench-only CMake overrides. These checks
 * become satisfiable only when measured values have been reviewed into this
 * board definition. */
#if defined(FUSE_VAULT_RELEASE_BUILD) && FUSE_VAULT_RELEASE_BUILD
#if !FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED
#error "Release requires a confirmed USB mux truth table"
#endif
#if !FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED
#error "Release requires confirmed USB connector-presence polarity"
#endif
#if !FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED
#error "Release requires confirmed SD card-detect polarity"
#endif
#if !FUSE_VAULT_DISPLAY_CONTROLLER_CONFIRMED
#error "Release requires a confirmed display controller/profile"
#endif
#endif

/* RP2354A uses the 60-pin, 30-GPIO RP2350 A package. */
#define PICO_RP2350A 1

/* --- USB connector sensing and data mux --- */
#define FUSE_VAULT_USB_SELECT_PIN 0
#define FUSE_VAULT_USB_OUTPUT_ENABLE_PIN 1
#define FUSE_VAULT_USB_A_PRESENT_PIN 2
#define FUSE_VAULT_USB_C_PRESENT_PIN 3

/*
 * PCB revision 1 connects the USB presence nets to a second GPIO pair as a
 * schematic error. These pins must remain high-impedance inputs. Reading
 * either pair produces the same result; firmware uses GPIO2/3 canonically.
 */
#define FUSE_VAULT_USB_A_PRESENT_DUPLICATE_PIN 16
#define FUSE_VAULT_USB_C_PRESENT_DUPLICATE_PIN 17

/* FSUSB42MUX OE# and SEL each have 10 kOhm pull-downs. The manufacturer truth
 * table makes OE low enabled; PCB revision 1 routes SEL-low HSD1 to USB-C. */
#define FUSE_VAULT_USB_MUX_ENABLE_LEVEL 0
#define FUSE_VAULT_USB_MUX_SELECT_USB_C_LEVEL 0

/* Full schematic supplied 2026-09-06: VBUS through 100 kOhm to sense,
 * with 120 kOhm from sense to GND (about 2.73 V at 5 V VBUS).
 * Keep optional bench overrides available for diagnostic builds. */
#ifndef FUSE_VAULT_USB_A_PRESENT_LEVEL
#define FUSE_VAULT_USB_A_PRESENT_LEVEL 1
#endif
#ifndef FUSE_VAULT_USB_C_PRESENT_LEVEL
#define FUSE_VAULT_USB_C_PRESENT_LEVEL 1
#endif

/* --- SD card: four-bit wiring, initial driver uses SPI mode --- */
#define FUSE_VAULT_SD_CLK_PIN 4
#define FUSE_VAULT_SD_CMD_PIN 5
#define FUSE_VAULT_SD_DAT0_PIN 6
#define FUSE_VAULT_SD_DAT1_PIN 7
#define FUSE_VAULT_SD_DAT2_PIN 8
#define FUSE_VAULT_SD_DAT3_PIN 9
#define FUSE_VAULT_SD_CARD_DETECT_PIN 24
/* R2 pulls SD_CD high. The socket symbol does not specify switch state;
 * the reported low-when-empty behavior remains pending a continuity test. */
/* Define FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL as 0 or 1 in the same reviewed
 * change that enables FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED. */

/* --- Active-low navigation controls with external pull-ups --- */
#define FUSE_VAULT_NAV_UP_PIN 10
#define FUSE_VAULT_NAV_LEFT_PIN 11
#define FUSE_VAULT_NAV_DOWN_PIN 12
#define FUSE_VAULT_NAV_RIGHT_PIN 13
#define FUSE_VAULT_NAV_SELECT_PIN 14
#define FUSE_VAULT_NAV_BACK_PIN 15
#define FUSE_VAULT_NAV_ACTIVE_LOW 1

/* --- TFT display --- */
/* Horizontal 160x80 mounting, flex through PCB. Q1 AO3401A is a
 * high-side P-channel backlight switch: gate low enables LEDA. */
#define FUSE_VAULT_TFT_BACKLIGHT_ENABLE_LEVEL 0
#define FUSE_VAULT_TFT_BACKLIGHT_PIN 18
#define FUSE_VAULT_TFT_RESET_PIN 19
#define FUSE_VAULT_TFT_DATA_COMMAND_PIN 20
#define FUSE_VAULT_TFT_CHIP_SELECT_PIN 21
#define FUSE_VAULT_TFT_CLOCK_PIN 22
#define FUSE_VAULT_TFT_MOSI_PIN 23

/*
 * The RP2354A contains 2 MiB of stacked flash on the normal XIP/QSPI
 * interface. Start with the conservative serial-read stage 2 until the exact
 * flash characteristics have been validated on production silicon.
 */
#define PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H 1

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

/*
 * Mutable security state occupies the final two 4 KiB sectors. Application
 * code must access this region only through the authenticated journal backend.
 */
#define FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES (2u * 4096u)
#define FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES \
    (PICO_FLASH_SIZE_BYTES - FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES)

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
