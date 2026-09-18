#ifndef FUSE_VAULT_ENTRY_METHOD_H
#define FUSE_VAULT_ENTRY_METHOD_H

#include <stdbool.h>
#include <stddef.h>

/* UI-only ordering. These values are not part of the persisted format. */
typedef enum {
    FV_ENTRY_METHOD_WHEELS = 0,
    FV_ENTRY_METHOD_DIRECTIONS,
    FV_ENTRY_METHOD_KEYPAD,
    FV_ENTRY_METHOD_WORD_LIST,
    FV_ENTRY_METHOD_COUNT,
} fv_entry_method_t;

static inline bool fv_entry_method_valid(fv_entry_method_t method) {
    return (unsigned)method < FV_ENTRY_METHOD_COUNT;
}

/*
 * Stable identifiers stored in canonical secret encodings and vault headers.
 * Values 1-4 are the existing v1 format and must not be renumbered.
 */
typedef enum {
    FV_SECRET_METHOD_WHEELS_V1 = 1,
    FV_SECRET_METHOD_DIRECTIONS_V1 = 2,
    FV_SECRET_METHOD_KEYPAD_V1 = 3,
    FV_SECRET_METHOD_WORD_LIST_V1 = 4,
} fv_secret_method_t;

static inline bool fv_entry_method_to_secret_method(
    fv_entry_method_t entry_method, fv_secret_method_t *secret_method) {
    if (secret_method == NULL) return false;
    switch (entry_method) {
        case FV_ENTRY_METHOD_WHEELS:
            *secret_method = FV_SECRET_METHOD_WHEELS_V1;
            return true;
        case FV_ENTRY_METHOD_DIRECTIONS:
            *secret_method = FV_SECRET_METHOD_DIRECTIONS_V1;
            return true;
        case FV_ENTRY_METHOD_KEYPAD:
            *secret_method = FV_SECRET_METHOD_KEYPAD_V1;
            return true;
        case FV_ENTRY_METHOD_WORD_LIST:
            *secret_method = FV_SECRET_METHOD_WORD_LIST_V1;
            return true;
        case FV_ENTRY_METHOD_COUNT:
            return false;
    }
    return false;
}

static inline bool fv_secret_method_to_entry_method(
    fv_secret_method_t secret_method, fv_entry_method_t *entry_method) {
    if (entry_method == NULL) return false;
    switch (secret_method) {
        case FV_SECRET_METHOD_WHEELS_V1:
            *entry_method = FV_ENTRY_METHOD_WHEELS;
            return true;
        case FV_SECRET_METHOD_DIRECTIONS_V1:
            *entry_method = FV_ENTRY_METHOD_DIRECTIONS;
            return true;
        case FV_SECRET_METHOD_KEYPAD_V1:
            *entry_method = FV_ENTRY_METHOD_KEYPAD;
            return true;
        case FV_SECRET_METHOD_WORD_LIST_V1:
            *entry_method = FV_ENTRY_METHOD_WORD_LIST;
            return true;
    }
    return false;
}

static inline bool fv_secret_method_valid(fv_secret_method_t secret_method) {
    fv_entry_method_t ignored;
    return fv_secret_method_to_entry_method(secret_method, &ignored);
}

#endif
