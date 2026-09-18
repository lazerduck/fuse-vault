/* RP2354A XIP base + 2 MiB flash - two 4 KiB journal sectors. */
ASSERT(__flash_binary_end <= 0x101fe000,
       "Fuse Vault firmware overlaps reserved security journal flash")
