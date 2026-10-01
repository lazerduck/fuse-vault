#include "vault_filesystem.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>

/* FatFs is already supplied by pico_sd. Its disk callbacks are wrapped only
 * during f_mkfs: all partition/FAT writes must pass through vault encryption.
 * FatFs reuses its const write buffers; vault writes consume their input. */
static fv_vault *formatting;
static uint8_t *copy_buffer;
static UINT copy_sectors;

DSTATUS __real_disk_initialize(BYTE);
DSTATUS __real_disk_status(BYTE);
DRESULT __real_disk_read(BYTE, BYTE *, LBA_t, UINT);
DRESULT __real_disk_write(BYTE, const BYTE *, LBA_t, UINT);
DRESULT __real_disk_ioctl(BYTE, BYTE, void *);

DSTATUS __wrap_disk_initialize(BYTE drive) {
    if (!formatting) return __real_disk_initialize(drive);
    return drive == 0 && formatting->unlocked ? 0 : STA_NOINIT;
}
DSTATUS __wrap_disk_status(BYTE drive) {
    if (!formatting) return __real_disk_status(drive);
    return drive == 0 && formatting->unlocked ? 0 : STA_NOINIT;
}
DRESULT __wrap_disk_read(BYTE drive, BYTE *out, LBA_t lba, UINT count) {
    if (!formatting) return __real_disk_read(drive, out, lba, count);
    if (drive || !out || !count) return RES_PARERR;
    while (count) {
        UINT batch = count < copy_sectors ? count : copy_sectors;
        if (fv_vault_read(formatting, lba, batch, copy_buffer) != FV_BLOCK_OK) return RES_ERROR;
        memcpy(out, copy_buffer, batch * 512u);
        out += batch * 512u; lba += batch; count -= batch;
    }
    return RES_OK;
}
DRESULT __wrap_disk_write(BYTE drive, const BYTE *in, LBA_t lba, UINT count) {
    if (!formatting) return __real_disk_write(drive, in, lba, count);
    if (drive || !in || !count) return RES_PARERR;
    while (count) {
        UINT batch = count < copy_sectors ? count : copy_sectors;
        memcpy(copy_buffer, in, batch * 512u);
        if (fv_vault_write(formatting, lba, batch, copy_buffer) != FV_BLOCK_OK) return RES_ERROR;
        in += batch * 512u; lba += batch; count -= batch;
    }
    return RES_OK;
}
DRESULT __wrap_disk_ioctl(BYTE drive, BYTE command, void *out) {
    if (!formatting) return __real_disk_ioctl(drive, command, out);
    if (drive || !formatting->unlocked) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC:
        return formatting->platform->sd->ops->sync(formatting->platform->sd) == FV_BLOCK_OK ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!out) return RES_PARERR;
        *(LBA_t *)out = formatting->config.volume.logical_blocks; return RES_OK;
    case GET_SECTOR_SIZE:
        if (!out) return RES_PARERR;
        *(WORD *)out = 512; return RES_OK;
    case GET_BLOCK_SIZE:
        if (!out) return RES_PARERR;
        *(DWORD *)out = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

int fv_vault_format_fat32(fv_vault *vault, uint8_t *workspace, size_t bytes) {
    fv_device_state state;
    if (formatting || !vault || !vault->unlocked || !workspace || ((uintptr_t)workspace & 3) ||
        bytes < 1024 || bytes > 65536 || !vault->platform || !vault->platform->authority.load ||
        vault->platform->authority.load(vault->platform->authority.context, &state) ||
        state.status != FV_ENROLLMENT_EMPTY) return -1;
    UINT work_bytes = (UINT)(bytes / 1024) * 512;
    formatting = vault; copy_buffer = workspace + work_bytes; copy_sectors = work_bytes / 512;
    /* No FM_SFD: create a partition table as well as a FAT32 filesystem.
     * Automatic cluster size; two FAT copies. FatFs rejects undersized media. */
    const MKFS_PARM options = {.fmt = FM_FAT32, .n_fat = 2};
    FRESULT result = f_mkfs("0:", &options, workspace, work_bytes);
    if (result == FR_OK) {
        FATFS fs;
        result = f_mount(&fs, "0:", 1);
        if (result == FR_OK && fs.fs_type != FS_FAT32) result = FR_NO_FILESYSTEM;
        (void)f_mount(NULL, "0:", 0);
    }
    formatting = NULL; copy_buffer = NULL; copy_sectors = 0;
    memset(workspace, 0, bytes);
    return result == FR_OK ? 0 : -1;
}
