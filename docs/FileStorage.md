# File storage

Fuse Vault uses the SD card to hold the encrypted files, FIDO2 credentials and the metadata needed to read them. Once the VMK has been unlocked, we derive the working keys and build the user's selected XTS encryption pipeline. Data is then encrypted and decrypted as it is needed. Unlocking the device does not decrypt the whole card or create a plaintext copy on it.

The key derivation and protection of the VMK are covered in [Key handling](KeyHandling.md). This describes the current storage implementation for newly created vaults.

## Dividing up the SD card

We use the physical card as a whole and divide its capacity between several regions. These are managed by the firmware rather than presented to the computer as separate partitions.

| Region, in order | Size | Purpose |
| --- | --- | --- |
| Headers and reserved space | 8 KiB | Vault configuration and copies of the protected VMK envelope |
| FIDO2 storage reservation | 1 MiB | The private encrypted FIDO2 store |
| Initialization bitmap | Depends on card capacity | Tracks which regions of sector metadata have been initialized |
| Sector metadata | Depends on content capacity | HMAC tags and written/unset flags |
| Encrypted content | Remaining usable capacity | The disk exposed to the computer over USB |

The FIDO2 reservation is the space set aside on the card, not a claim that all of it is available for passkey records. Its internal format and recovery copies take their own space.

The computer only sees the content region as a normal disk while unlocked. The partition table, FAT32 filesystem, directories and files all live inside this encrypted region. The host cannot address the physical card's vault headers, FIDO2 reservation or sector metadata through USB mass storage.

## Reading and writing data

The computer deals with files, but the device receives requests for numbered 512-byte sectors. A request may cover many sectors. We process it in batches of up to 64 sectors, or 32 KiB. This is a processing limit rather than a maximum file size or USB command size.

When writing, each sector passes through the selected XTS algorithms in order, using a separate derived key for each layer. The sector address supplies the XTS tweak, which ties the encryption to its location. We then calculate an HMAC over the ciphertext and its context and write the ciphertext and metadata to the SD card.

When reading, we retrieve the ciphertext and check its HMAC before decrypting it. If the check succeeds, we apply the encryption layers in reverse order and return the plaintext to the computer. A failed integrity check returns an error rather than releasing the affected batch's contents.

```text
Write: host plaintext -> XTS layers -> HMAC -> SD
Read:  SD ciphertext -> HMAC verification -> reverse XTS layers -> host plaintext
```

All of this happens on demand. The working keys remain available during the unlocked session, so each request does not need to repeat the password derivation. Once the disk is unlocked, the host can read its files; encryption at rest does not hide those files from the computer using it.

## Buffering

The normal USB storage pipeline has two 32 KiB buffers, using 64 KiB of RAM in total. One belongs to the USB transfer path and the other holds the batch being processed by the storage worker. The same two buffers are used for reads and writes.

On a write, USB can receive the next batch while the worker encrypts, authenticates and writes the current batch. On a read, USB can send a completed batch while the worker reads, authenticates and decrypts the next one.

| Direction | USB buffer | Worker buffer |
| --- | --- | --- |
| Write | Receiving batch 2 | Encrypting and writing batch 1 |
| Read | Sending batch 1 | Reading and decrypting batch 2 |

This overlaps USB traffic with storage processing. Within the worker, SD access and cryptographic processing still happen sequentially; the next SD read does not run alongside decryption of the current batch. Small requests that fit in a single batch do not benefit from overlap between batches.

The device processes one storage command at a time, either reading or writing. The host can alternate between them, so copying files in both directions is supported, but there are not two independent read and write streams running simultaneously.

There is also a separate 32 KiB workspace used for setup and diagnostics, along with metadata caches and FIDO2 buffers. These are separate from the two buffers in the normal USB file-transfer pipeline.

## Integrity and rollback

XTS encrypts the content but does not by itself detect tampering. We use HMAC-SHA-256 with a separate VMK-derived key to authenticate each written sector. The tag covers the ciphertext, vault identity and logical sector address. Changing the ciphertext or moving a valid sector to another address will therefore fail verification unless the attacker can produce a valid tag.

This does not prevent rollback. An attacker can restore an older ciphertext sector and its matching tag to the same location in the same vault. They do not learn the plaintext, but they can undo changes. They could also mix valid sectors from different points in time, which may leave an inconsistent filesystem rather than a usable older snapshot.

The device's internal security state separately records which vault header is accepted. Restoring an old SD image does not simply restore an old credential or reset the attempt count. Restoring the device's internal flash is a separate attack, outside the agreed protection scope.

The intended boundary is that someone controlling the SD card cannot read the encrypted contents or manufacture arbitrary new plaintext that passes authentication, assuming the keys and cryptographic functions remain secure. They can still delete, corrupt or restore older data. There is also an explicit zeroing case in the unset-sector handling below.

## Unwritten sectors and metadata

We do not encrypt and write the entire card during setup. Instead, sectors that have not been written are treated as unset and read as zeros. A small bitmap tells us which regions of metadata have been initialized, avoiding the need to clear the whole metadata area first. Setup then writes the filesystem through the encrypted storage path.

One 512-byte metadata sector holds the tags and state for 15 content sectors. Metadata therefore uses roughly one additional sector for every 15 sectors of content, plus the much smaller initialization bitmap. This is why the disk presented to the host is smaller than the physical card.

The bitmap and written/unset flags are not authenticated. An attacker can clear them to make affected content read as zeros. We accept this as deletion or corruption; it does not expose the old plaintext or allow the attacker to supply arbitrary replacement contents. Setting a sector to written still requires its ciphertext to pass HMAC verification.

## Completing writes

The worker synchronizes ciphertext and metadata before reporting a successful write. USB may already be receiving another batch, but the device does not report the whole write command as successful until its final batch completes. There is no write-back cache carrying unfinished writes between commands.

This does not make a whole batch or file an atomic update. If power is lost during a write, some sectors or their metadata may have changed and others may not. The result can include unreadable sectors, unset sectors reading as zeros, or filesystem damage. Integrity checks protect against returning modified ciphertext as trusted data; they do not guarantee recovery of interrupted file updates. The host can also have its own unwritten cache, so files should be flushed and the disk ejected before locking or disconnecting.
