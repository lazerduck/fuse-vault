# SM4 volume and VMK protection

Algorithm ID 3 means SM4-128-XTS for sectors and SM4-128-KW for the corresponding
VMK share. IDs 1 and 2, existing descriptor/envelope layouts, KDF labels and
AES/Camellia output remain unchanged. Old firmware rejects ID 3; volumes using
it require firmware with SM4 support. Selecting a stack applies to new volumes,
not an in-place conversion of an existing volume.

The volume and FIDO key derivations produce 32 bytes for SM4: the first 16 are
the data key and the next 16 the tweak key. Existing 64-byte pipeline slots
remain capacity buffers, with unused SM4 suffixes zeroed by derivation and
ignored by the pipeline. AES/Camellia still consume all 64 bytes.

The envelope derives a separate key per layer, bound to the authenticated
header, layer index and cipher ID. SM4 wrapping consumes the first 16 bytes of
that 32-byte HKDF output. Every layer wraps one XOR share; all shares are needed
to recover the 32-byte VMK. The envelope HMAC is verified before any unwrap;
unwrap failure clears output and never publishes a partial VMK. Sector HMAC
and envelope HMAC remain SHA-256. Adding SM4 does not change password KDF cost
or imply additive security strength across layers.

The device selector cycles AES-256, CAMELLIA-256, SM4-128. The board benchmark
accepts `--stacks sm4 aes,sm4,camellia,sm4` with its existing explicit destructive
benchmark opt-in. The portable upstream subset and its one C portability fix
are documented in `third_party/gmssl_sm4/README.md`.
