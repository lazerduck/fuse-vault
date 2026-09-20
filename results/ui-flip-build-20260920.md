# UI flip build

Artifact: `build-pico-device-ui/v2_device_ui_flip.uf2`
SHA-256: `187936be35e6c14647bd0a6a0d36a0f5239a1a0d0e18505686024451aca6cb04`
Highest payload end: `0x10022b00`, below the persistent journal.

Left/Right on the locked/unlocked home screen flips pixels and directional
controls 180 degrees. Select/Back retain their roles. The preference is volatile
and survives UI/session transitions while power remains applied. No storage
format, authority or key changes; existing vault is preserved.

ARM build and desktop UI tests pass, including ASan/UBSan. Tests compare every
pixel against the 180-degree transform, flip twice back to the original frame,
verify equal credential bytes with inverse physical directions, and exercise
unlocked menu navigation and preference retention/reset. Physical orientation
acceptance awaits user flashing. Firmware has not been flashed by this task.

User reports prior bitmap firmware successfully copied files and worked through
USB-A; this is user-reported functional validation, not a measured benchmark.
