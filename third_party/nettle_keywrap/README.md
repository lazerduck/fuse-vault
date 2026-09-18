# Generic key wrap from Nettle 3.10.2

Source: https://github.com/gnutls/nettle/tree/d9e983c664772f8cbacba9fa084bc079978e3c98
Tag: nettle_3.10.2_release_20250626. Unmodified source/header and licences are in
`upstream/`. Upstream offers LGPL-3.0-or-later or GPL-2.0-or-later; retain these
notices and account for the chosen licence when distributing linked firmware.

`keywrap.c` retains only the generic nist_keywrap16/nist_keyunwrap16 functions.
Their wrap/unwrap loops are unchanged. Integration changes: local compatibility
includes/types/endian conversion, fv_ namespace and scratch zeroization. AES-only
convenience wrappers and their dependency tree are omitted. The compatibility
comparison scans every byte. `src/security/key_wrap.c` restricts inputs to 32-byte
shares/40-byte wraps, supplies Mbed TLS AES/Camellia callbacks, propagates callback
failures, and clears failed output and key schedules. This is source reuse with
local adaptation, not a claim that our integration/envelope has been audited.

No crypto implementation is fetched at configure/build time. Tests compare AES
with OpenSSL's AES-KW and both algorithms with an independent RFC wrap reference
using OpenSSL ECB. Full-envelope fixed fixtures use an independent Python reference.
