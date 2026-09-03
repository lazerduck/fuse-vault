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

/* --- SD card: four-bit SDIO, initially implemented using PIO --- */
#define FUSE_VAULT_SD_CLK_PIN 4
#define FUSE_VAULT_SD_CMD_PIN 5
#define FUSE_VAULT_SD_DAT0_PIN 6
#define FUSE_VAULT_SD_DAT1_PIN 7
#define FUSE_VAULT_SD_DAT2_PIN 8
#define FUSE_VAULT_SD_DAT3_PIN 9
#define FUSE_VAULT_SD_CARD_DETECT_PIN 24

/* --- Active-low navigation controls with external pull-ups --- */
#define FUSE_VAULT_NAV_UP_PIN 10
#define FUSE_VAULT_NAV_LEFT_PIN 11
#define FUSE_VAULT_NAV_DOWN_PIN 12
#define FUSE_VAULT_NAV_RIGHT_PIN 13
#define FUSE_VAULT_NAV_SELECT_PIN 14
#define FUSE_VAULT_NAV_BACK_PIN 15
#define FUSE_VAULT_NAV_ACTIVE_LOW 1

/* --- TFT display --- */
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

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
