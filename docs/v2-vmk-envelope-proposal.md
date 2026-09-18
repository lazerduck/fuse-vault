# Stack-aware VMK envelope — four-slot development format

**Share-based direction and four fixed slots accepted by the user on 2026-09-17.
Portable serialization, binding/derivation, wrapping and lifecycle are implemented
and tested. Production entropy/device-state integration and independent security
review remain outstanding; the persistent format is not frozen.** This is an
application protocol built from standard primitives, not a claim that the complete
multi-cipher construction is standardized or audited.

## Recommendation: wrap independent shares, require every share

For a volume with AES → Camellia, generate two 32-byte shares whose XOR equals
the VMK. Wrap one share with AES key wrap, the other with Camellia key wrap,
using independent derived keys. Authenticate the entire envelope/header with a
separate HMAC key. Recover the VMK only after authentication and both unwraps.

For n layers, choose n−1 independent random 32-byte shares; the last is VMK XOR
all previous shares. With n=1 the sole share is the VMK. Every rewrap resamples
the shares (while keeping the same VMK) and password salt. A wrapped share alone
does not reveal the VMK for n>1. Do **not** independently wrap the whole VMK under
each cipher: that would let the weakest wrapper reveal it.

This differs from a literal encryption cascade in the earlier conceptual sketch.
It achieves the intended need for all configured cipher protections, while each
wrapper handles exactly 32 bytes. Stack order is explicit and authenticated, and
selects key/share indices; XOR reconstruction itself is order-independent.
The user accepted this distinction and the share-based construction.

## Existing primitives and implementation constraints

- AES-256-KW: [RFC 3394](https://www.rfc-editor.org/rfc/rfc3394.html), default
  eight-byte `A6` initial value, 32-byte input → 40-byte output, no padding.
- Camellia-256 key wrap: [RFC 3657 §3](https://www.rfc-editor.org/rfc/rfc3657.html),
  its specified key-wrap algorithm and default initial value, same input/output sizes.
- HMAC-SHA-256, full 32-byte outer tag; domain-separated HKDF-SHA-256 keys.
- n-of-n XOR splitting uses independent random shares; the split-knowledge
  principle is described by [NIST](https://csrc.nist.gov/glossary/term/split_knowledge).
  That source does not specify or validate this complete envelope protocol.

KW uses its defined fixed initial value. It has no externally supplied random
nonce and does not use XTS. Deterministic wrapping is appropriate to these
high-entropy shares; resampling shares/salt on rewrap changes the envelope.

Why not simply nest KW outputs? The Camellia CMS profile restricts supported
CEK/KEK size relationships; an inner 40-byte envelope exceeds a 32-byte KEK.
The underlying wrap loop may process it, but we should not pretend that this is
an unqualified use of that profile. Fixed 32-byte shares avoid that issue.

The checked-in SDK's Mbed TLS `nist_kw.c` explicitly accepts only AES. Camellia
KW is **not** supplied by that API merely because Camellia ECB is available.
The implementation uses the generic C wrap/unwrap functions from
[Nettle 3.10.2](https://github.com/gnutls/nettle/tree/d9e983c664772f8cbacba9fa084bc079978e3c98),
pinned and retained with upstream source/licences in `third_party/nettle_keywrap`.
A narrow adapter supplies Mbed TLS AES/Camellia ECB callbacks, propagates failures
and wipes scratch/key schedules. Its licence choices and exact changes are in that
folder's README. No AES-only checks were removed from Mbed TLS. This adaptation
and our complete protocol still need independent review before production use.

Fixed full-envelope vectors are generated independently using Python hashlib/hmac
and cryptography/OpenSSL: AES-KW from that library, Camellia-KW from an independent
RFC loop over its Camellia ECB primitive. The vectors include valid outer HMACs
over deliberately broken wrapped shares, checking failure after authentication.

## Implemented development construction

All integer encodings/labels use the rules in the main design. Candidate
construction ID `0x0101` means `SHARES_KW_HMAC_SHA256_V1`. IDs are provisional.
Wrapper IDs 1/2 mean AES-256-KW/Camellia-256-KW, distinct from XTS mode IDs.
Wrapper count equals storage layer count; each wrapper's cipher family must match
its corresponding storage layer. Duplicate families still get independent keys.
New algorithms require a registered wrapper ID and reviewed compatibility; do not
substitute an unsupported family. Different size/mode requirements can use a new
construction/version without changing the VMK hierarchy.

Let `H` be the proposed header, D=SHA256(H[0:128]), and C=D||H[128:256]. Public
header fields, including policy and wrapper IDs, are populated before derivation.
The device provides `vault_binding` only for the active, non-destroyed enrollment:

```
vault_binding = HMAC-SHA256(device_root,
    L("FV2/vault-binding/v1") || LE32(token_slot) || volume_id || token)

bound_secret = HMAC-SHA256(vault_binding,
    L("FV2/unlock-input/v2") || C || LE32(secret_length) || user_secret)

password_key = PBKDF2-HMAC-SHA256(bound_secret,
    L("FV2/password-salt/v1") || password_salt, iterations, 32)

wrap_prk = HKDF-Extract(password_salt, password_key)
wrap_key[i] = HKDF-Expand(wrap_prk,
    L("FV2/share-wrap/v1") || C || LE32(i) || LE16(wrapper_id[i]), 32)
mac_key = HKDF-Expand(wrap_prk, L("FV2/envelope-mac/v1") || C, 32)

W[i] = KW[wrapper_id[i]](wrap_key[i], share[i])
H[256:416] = W[0] || ... || W[n-1] || zero padding to 160 bytes
H[416:448] = HMAC-SHA256(mac_key,
    L("FV2/envelope/v1") || H[0:416] || H[448:512])
```

The independent outer tag covers every serialized byte except itself, including
policy, storage descriptor, generation, wrapper IDs/order, wrapped shares and
reserved padding. It is mandatory even though KW has its own integrity check.
This avoids treating KW's internal check as authentication of external metadata.

Opening order: anchored-header selection and device-policy gate; durable attempt
charge; derivation; constant-time outer-tag check; unwrap each share into private
scratch memory and validate every KW result/length; XOR into a private candidate;
then publish the recovered VMK to session initialization. On any failure zero all
shares, candidates and intermediate keys, return one authentication failure, and
publish no session or candidate VMK. Never offer a raw unwrap oracle via USB.

## 512-byte envelope area (descriptor is unchanged)

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 128 | Immutable descriptor from main design |
| 128 | 16 | Public device ID |
| 144 | 8 | credential_generation, initially 1 |
| 152 | 2 | Credential encoding profile |
| 154 | 2 | Password KDF ID; 1 = PBKDF2-HMAC-SHA-256 |
| 156 | 4 | Iteration count, checked against trusted calibrated limits |
| 160 | 32 | Fresh random password salt |
| 192 | 2 | Construction ID, proposed 0x0101 |
| 194 | 1 | Wrapper/share count, 1–4 |
| 195 | 1 | Reserved zero |
| 196 | 2 | Wrapped-share bytes = 40 × count |
| 198 | 2 | Reserved zero |
| 200 | 4 | Device enrollment token slot, matched to local state |
| 204 | 4 | Reserved zero |
| 208 | 16 | Versioned authentication policy |
| 224 | 8 | Four LE16 wrapper IDs in storage-stack order, unused zero |
| 232 | 24 | Reserved zero |
| 256 | 160 | Concatenated 40-byte wrapped shares, unused zero |
| 416 | 32 | Full outer HMAC tag |
| 448 | 64 | Reserved zero |

Header hash in device state covers all 512 bytes including the tag. Physical
header copies are identical; no slot address enters key derivation. The portable codec serializes this exact layout. Active slots form a contiguous
prefix; all unused IDs and share bytes must be zero, rather than skipped holes.
Parsing rejects unknown construction/algorithm IDs, inconsistent counts and any
nonzero reserved bytes before expensive credential evaluation.

## What this does and does not claim

If one independent cipher family remains confidential, its undisclosed random
share prevents reconstructing the VMK from other shares, under the assumed
security of the KDF, RNG, HMAC and endpoint. This is an intended security argument,
not a formal proof against every possible catastrophic failure model. Repeated
instances of one cipher are not independent algorithm families. This does not
hedge a compromised firmware implementation, leaked wrap_prk/root/token, broken
SHA/HMAC or an RNG that exposes shares. The wrapper does not multiply key strength.

Remaining review: independent composition/integration review; production entropy;
physical device-token and destruction mechanism; platform integration. Fixed vectors
and a working portable implementation do not freeze the format for deployed data.
AES-GCM is not a production construction ID or default fallback.

## RNG dependency raised during review (2026-09-17)

The latest user decision keeps **four fixed slots** in this version. Construction
and algorithm IDs provide explicit evolution points. A future larger layout needs
a new format/version and deliberate migration; changing a C array limit alone
must never reinterpret existing media. One to four active layers are supported;
there is no variable-length list or unlimited layer count in this format.

Splitting may be expressed recursively or as n−1 random shares plus one XOR
remainder; these are equivalent. Random shares must remain unpredictable. If
all n−1 generated shares are predictable, confidentiality depends only on the
wrapper holding the final remainder. This loses the intended cipher-diversity
benefit even if the VMK was originally generated securely. Predictable VMK/root
generation is catastrophic regardless of the envelope construction.

The current project has no validated production RNG. The installed SDK's
`pico_rand` uses xoroshiro128** plus entropy mixing; its TRNG sampling path bypasses
hardware checks/decorrelators. It is not being accepted as our cryptographic RNG
merely because the chip contains a TRNG. Next specify and assess raw entropy
collection/health checks and a library cryptographic DRBG (candidate HMAC-DRBG),
with failure propagation, secret state, reseeding and separate use contexts.
Statistical tests cannot establish resistance to a malicious source or replace
entropy assessment. Do not count timers, public identifiers or the D-pad password
as sufficient entropy, or claim two oscillator sources are independent without
evidence. The portable library receives an RNG callback and aborts on failure; deterministic
sources exist only in tests. No live RNG/OTP/provisioning implementation is added.
