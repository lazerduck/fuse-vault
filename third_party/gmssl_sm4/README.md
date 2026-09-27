# Portable SM4 from GmSSL 3.1.1

Source: https://github.com/guanzhi/GmSSL/tree/d655c06b3a6b0fe8cff900f293bf0e5aac6eb0a2
Tag: v3.1.1. Apache-2.0; the licence and pristine source subset are in `upstream/`.
Only the portable block cipher, key schedule, tables and required headers are
included. No TLS, random generator, allocation or OS dependency is linked.
No source is downloaded at configure/build time.

The compiled copies of sm4_common.c, sm4_enc.c and sm4_setkey.c are unchanged.
The local sm4_lcl.h casts S-box bytes to uint32_t before shifts in S32, avoiding
undefined signed left shifts after integer promotion. This is the only source
adaptation. The block path uses a 1 KiB lookup table; key setup uses a 256-byte
S-box. It is not a constant-time or physical side-channel-resistant backend.
FV_CIPHERS_RAM places the block routine and 1 KiB table in Pico SRAM via forced
forward declarations; no Pico dependency is introduced in desktop builds.

SM4 uses 16-byte keys and 16-byte blocks. Our XTS adapter prepares encryption,
decryption and tweak schedules (384 bytes total) and uses 32 effective key
bytes. Our generic Nettle-derived KW adapter uses a 16-byte KEK to wrap each
32-byte VMK share. Neither mode comes from GmSSL. Camellia/SM4 KW and XTS here
are project compositions, not claims of AES-mode certification.

Validation includes published SM4 single-block/million-iteration answers,
OpenSSL SM4 block comparisons composed with independent XTS arithmetic,
Python cryptography/OpenSSL envelope and KW fixtures, mixed stacks, invalid
keys, tampering, clearing, vault lifecycle and FIDO storage tests.
