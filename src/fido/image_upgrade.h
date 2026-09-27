#ifndef FV_FIDO_IMAGE_UPGRADE_H
#define FV_FIDO_IMAGE_UPGRADE_H
#include "fuse_vault/fido_engine.h"
/* Authenticated v1 image in the first 64 KiB. Validate before relocating the
 * two file lists; never interpret a malformed list as an empty store. */
bool fv_fido_image_upgrade(uint8_t image[FV_FIDO_STORE_BYTES]);
#endif
