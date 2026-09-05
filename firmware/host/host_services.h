#ifndef FUSE_VAULT_HOST_SERVICES_H
#define FUSE_VAULT_HOST_SERVICES_H

#include "fuse_vault/persistence.h"

#include <stdbool.h>

#define FV_HOST_PATH_CAPACITY 4096u

typedef struct {
    char directory[FV_HOST_PATH_CAPACITY];
} fv_host_services_context_t;

bool fv_host_services_init(fv_platform_services_t *services,
                           fv_host_services_context_t *context,
                           const char *directory);

#endif
