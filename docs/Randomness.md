# Randomness

Fuse Vault needs unpredictable values when creating secrets. If an attacker could predict the VMK or enrollment token, they could undermine the protection described in [Key handling](KeyHandling.md), even if the encryption itself worked correctly.

We generate randomness on the device using the chip's hardware random source and a cryptographic random generator in software. This supplies vault setup, credential changes and FIDO operations that need fresh random values.

## What uses it

The main uses are:

- Generating the VMK, enrollment tokens and the permanent device root used by the current implementation.
- Generating the vault identifier and the salt used when protecting the VMK. The salt is a public random value that makes credential derivation specific to that envelope.
- Generating the random shares used to split the VMK between multiple wrapping algorithms.
- Supplying the FIDO engine's key generation and other cryptographic operations that require randomness.

Changing the vault credential generates a fresh salt and, where applicable, fresh wrapping shares while preserving the VMK. Normal file reads and writes use the existing derived keys and sector addresses, so they do not need a fresh random value for each sector.

The user's credential is chosen by the user. Its unpredictability therefore depends on that choice as well as the available entry method.

## From hardware to random bytes

The RP2354 has the RP2350 family's hardware true random number generator, usually abbreviated to TRNG. Its physical source uses a ring oscillator. The peripheral collects results into 192-bit blocks, or 24 bytes. The hardware and its controls are described in the [RP2350 datasheet, section 12.12](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

Our adapter uses oscillator-chain selection 0 and a sampling interval of 100 system clocks, with the normal hardware conditioning and health checks enabled. These are the current development settings and still need qualification on the actual board.

Those blocks feed Mbed TLS's AES-256 CTR-DRBG. A DRBG is a deterministic random bit generator: it uses a secret internal state and a cryptographic algorithm to produce the requested bytes. The unpredictability comes from the hardware input used to seed and refresh that state.

We collect 48 bytes for initialization and another 48 bytes before every generation request. Refreshing the state this way is called reseeding. The generator's software self-test runs during initialization.

```text
Hardware random source -> checked blocks -> AES-256 random generator -> requested bytes
```

This randomness path is shared by the supported encryption stacks. Selecting Camellia or SM4 for storage still uses the AES-based random generator. Confidence in generated keys therefore includes confidence in this generator and the hardware source, alongside the selected storage algorithms.

## Detecting failures

The hardware adapter checks for source errors and allows up to one second to collect each block. It also checks that the source configuration and health-check settings have not unexpectedly changed.

The software rejects blocks that are all zeros, all ones, or identical to the immediately preceding block in that generator instance. These checks catch some obvious stuck-source failures. Establishing how much unpredictability the source provides requires separate assessment.

If collection or generation fails, the generator returns an error and clears the affected output. The failed instance refuses further generation until explicitly reinitialized. Operations depending on it must fail, and a randomness failure also faults the FIDO engine. There is no fallback to timestamps, device identifiers or fixed seeds.

Diagnostic output reports status, timings and failure counts. It does not expose the generated secret bytes.

## Remaining validation

The implementation and desktop failure tests cover software behaviour, including rejecting bad input, propagating failures and clearing generator state. Board smoke tests show that collection works with the tested setup.

The remaining work is to assess the source across the intended temperature, clock and supply conditions, and confirm suitable sampling settings. Collecting 48 bytes describes the amount of input; the actual unpredictability depends on the source's quality.

We also need to validate failure recovery on hardware, including when reinitialization is appropriate and how an interrupted operation is presented to the user. Production confidence depends on this assessment together with the hardware design and enabled checks.
