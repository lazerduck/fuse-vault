#ifndef FV_ACCESS_MODE_H
#define FV_ACCESS_MODE_H
#include <stdbool.h>

/* Volatile session authorization, independent of whether vault keys are loaded.
 * Unknown values deny every operation. Never serialize this into vault headers. */
typedef enum {
    FV_ACCESS_LOCKED = 0,
    FV_ACCESS_FULL,
    FV_ACCESS_PASSKEYS_ONLY,
    FV_ACCESS_FILES_READ_ONLY
} fv_access_mode;

static inline bool fv_access_valid(fv_access_mode mode) {
    return mode == FV_ACCESS_FULL || mode == FV_ACCESS_PASSKEYS_ONLY ||
           mode == FV_ACCESS_FILES_READ_ONLY;
}
static inline bool fv_access_files(fv_access_mode mode) {
    return mode == FV_ACCESS_FULL || mode == FV_ACCESS_FILES_READ_ONLY;
}
static inline bool fv_access_writable(fv_access_mode mode) { return mode == FV_ACCESS_FULL; }
static inline bool fv_access_fido(fv_access_mode mode) {
    return mode == FV_ACCESS_FULL || mode == FV_ACCESS_PASSKEYS_ONLY;
}
static inline bool fv_access_admin(fv_access_mode mode) { return mode == FV_ACCESS_FULL; }
#endif
