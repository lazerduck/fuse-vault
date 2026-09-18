# AES timing investigation

Date: 2026-09-16. Status: repeatability confirmed; cause still under investigation. Whole-firmware RAM diagnostic did not enumerate on USB after flashing or a full power cycle. AES-only RAM diagnostic now boots and passes both authenticated benchmark cases; timings implicate code/memory placement, with a separate Camellia regression in this layout.

## What the numbers mean

At 32 KiB batches, authenticated AES throughput improved from 359 to 368 KiB/s write and 402 to 473 KiB/s read. The reported regression concerns the **cipher stage**, not overall authenticated throughput:

- Software-HMAC historical build: AES encryption about 704 ms per MiB.
- Hardware-HMAC current build: AES encryption about 1,054 ms per MiB.
- HMAC itself fell from about 469 to 73 ms per MiB.

Thus roughly 396 ms of HMAC savings is offset by roughly 350 ms of additional cipher time. The modest overall write gain is consistent with the stage timings.

## Evidence

The engine times the complete pipeline encryption loop before entering authenticated storage. HMAC and SD operations occur outside that timer. No key scheduling occurs in this loop. The clock request remains 150 MHz.

The [fresh per-batch run](v5-aes-investigation.json) verified all payload bytes. Its 32 encryption batches each took 32.758–33.077 ms (median 32.9235 ms), totalling 1,053.796 ms per MiB. Decryption batches took 22.291–22.358 ms. This is consistent overhead across the run, not one long SD stall. Previous [authentication-disabled](v5-sha-aes-baseline-check.json) and [authenticated repeat](v5-sha-aes-auth-repeat.json) runs reproduced the encryption time.

Comparing existing protocol-5 hardware-SHA and software-SHA build artifacts:

- The unrelocated AES object `.text` bytes are identical (SHA-256 `6ad287e98e48db854494575b07445622e082c2d18e5d44bbb7521c3cd7a18542`). This comparison is between the two protocol-5 builds, not a recovered protocol-4 ELF.
- AES encryption links at `0x1000df28` with hardware SHA, versus `0x1000d99c` with software SHA. Decryption links at `0x1000e3a4` versus `0x1000de18`.
- Generated AES forward tables occupy RAM `0x20004000` through `0x20004fff` in both builds; reverse tables occupy `0x20005000` through `0x20005fff`.
- Both cores execute code from flash in the current build; core 0 polls USB and response queues while core 1 encrypts.

The RP2350 uses an XIP cache for flash execution ([Raspberry Pi datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), XIP cache section). Changed code placement and cache conflicts are a plausible explanation for unchanged AES code taking different time. No cache counters have been captured, so this remains a hypothesis.

## Prepared experiment

`build-pico-ram/fuse_vault_v2_bench.uf2` uses the SDK's `copy_to_ram` binary type. Hardware SHA remains enabled, and the benchmark protocol, algorithm implementation and SD layout are unchanged. The normal `build-pico` artifact is preserved.

Build commands:

```sh
cmake -S firmware/bench -B build-pico-ram \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DFV_HMAC_PICO=ON -DCMAKE_BUILD_TYPE=Release \
  -DPICO_DEFAULT_BINARY_TYPE=copy_to_ram
cmake --build build-pico-ram -j4
v1/firmware/build/_deps/picotool/picotool uf2 convert \
  build-pico-ram/fuse_vault_v2_bench.elf \
  build-pico-ram/fuse_vault_v2_bench.uf2 \
  --family rp2350-arm-s --platform rp2350
```

The firmware builds and links successfully. The link map places AES encryption at `0x2000ded0`, decryption at `0x2000e34c`, and hardware HMAC at `0x2000c930` (SRAM). Text is 99,600 bytes and BSS is 73,464 bytes; the linked BSS ends at `0x200279e8`, below the stack limit `0x20080000`.

Diagnostic UF2 SHA-256: `4604a8b2d4be2e2b3b5ce26557ebd69cb4ccea776408ad9f0c164fbe839c49e2`.

After manually flashing, run:

```sh
python3 tools/board_bench.py \
  --port /dev/serial/by-id/usb-Fuse_Vault_Fuse_Vault_V2_PIPELINE_317741A1459A6F94-if00 \
  --allow-scratch-write --integrity --stacks aes camellia \
  --batch-bytes 32768 --total-bytes 1048576 --log-batches \
  --label 'RAM execution diagnostic' \
  --firmware build-pico-ram/fuse_vault_v2_bench.uf2 \
  --output results/v5-ram-diagnostic.json
```

The device has no running-firmware reboot/flash command, and no debug probe is connected; this step requires a manual flash. The diagnostic runs the *whole firmware* from RAM, so it also changes other code/data placement and USB timing. Compare the cipher-stage timing, not only throughput. A large improvement would implicate execution/memory placement, but would not by itself prove a particular cache conflict. If confirmed, narrow the change to the cipher hot path or measure cache misses before choosing a permanent fix. Do not treat this RAM build as a validated production optimization yet.

## RAM diagnostic startup outcome and narrower replacement

The user flashed the whole-firmware RAM diagnostic. Linux recorded departure from BOOTSEL but no benchmark USB enumeration, including after a complete power cycle. No SD benchmark ran and no new performance result exists. The generated UF2 payload matches the build's BIN file; the underlying startup failure remains unlocated without a debug probe.

A narrower diagnostic is now available at `build-pico-aes-ram/fuse_vault_v2_bench.uf2`. It uses normal flash startup and the opt-in `FV_AES_RAM=ON` build option. Only the upstream AES block encrypt/decrypt routines are assigned to the SDK's `.time_critical` SRAM sections through a source-specific forced declaration header; vendor source and algorithms are unchanged. The option defaults OFF and requires a Pico SDK build.

Verified in the linked image: `main` remains at `0x100001e0`, USB polling at `0x10000354`, AES encryption at `0x20001430` (1,148 bytes), and AES decryption at `0x200018ac` (1,144 bytes). The firmware builds successfully. Board startup, round-trip correctness and timing still need verification after flashing this replacement.

```sh
cmake -S firmware/bench -B build-pico-aes-ram \
  -DPICO_SDK_PATH=/home/adam/pico-sdk -DPICO_NO_PICOTOOL=1 \
  -DFV_HMAC_PICO=ON -DFV_AES_RAM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico-aes-ram -j4
v1/firmware/build/_deps/picotool/picotool uf2 convert \
  build-pico-aes-ram/fuse_vault_v2_bench.elf \
  build-pico-aes-ram/fuse_vault_v2_bench.uf2 \
  --family rp2350-arm-s --platform rp2350
```

Use the benchmark command above with firmware path `build-pico-aes-ram/fuse_vault_v2_bench.uf2`, label `AES-only RAM diagnostic` and output `results/v5-aes-ram-diagnostic.json`. Firmware SHA-256: `d7cecb7b5612570f2b54ce307fb3313ae3bdf685c50681ce9c280bfdda023a30`.

## AES-only RAM board results

The replacement enumerated successfully and reported hardware SHA with a passing HMAC self-check. Both 1 MiB authenticated cases (32 KiB batches) passed full payload verification. [Raw results](v5-aes-ram-diagnostic.json).

| Cipher / direction | Previous cipher time (ms/MiB) | AES-RAM build cipher time (ms/MiB) | Previous throughput (KiB/s) | AES-RAM throughput (KiB/s) |
|---|---:|---:|---:|---:|
| AES write | 1054.3 | 633.9 | 367.9 | 435.5 |
| AES read | 714.3 | 636.1 | 472.6 | 490.9 |
| Camellia write | 925.7 | 1509.9 | 375.7 | 311.0 |
| Camellia read | 927.0 | 1510.6 | 430.4 | 345.6 |

AES encryption time fell about 40%, and is now below the historical software-HMAC build's 704 ms. Complete HMAC time remains about 74 ms/MiB; data SD times remain close to previous measurements. Camellia, which still executes from flash, became slower in both directions after the relink. Moving AES also shifts other linked code and data, so this is strong evidence of execution/memory placement sensitivity, not proof of a specific cache conflict. It does not indicate a change in the AES algorithm or an inherent cost of hardware HMAC.

Per-batch cipher timings (32 batches per direction):
- aes write: min 19.800, median 19.806, max 19.854 ms.
- aes read: min 19.873, median 19.880, max 19.891 ms.
- camellia write: min 46.899, median 47.181, max 47.639 ms.
- camellia read: min 46.849, median 47.174, max 47.567 ms.

The diagnostic is not a final performance fix: AES improves but Camellia regresses. Next isolate the remaining flash-resident cipher hot paths and/or capture XIP cache counters. The whole-firmware RAM startup failure remains a separate unresolved diagnostic issue.

## Combined SRAM follow-up

Both cipher hot paths and Camellia tables now reside in SRAM. All 30 cases
passed in the [three-pass matrix](v5-ciphers-ram-benchmark.md). AES encryption
held at about 634 ms/MiB and Camellia at 918 ms/MiB; the large slowdowns seen
in the earlier layouts are absent in this build. This validates the practical
placement change on the tested workload without proving the precise cache
mechanism. The full-RAM startup issue remains separate and unresolved.
