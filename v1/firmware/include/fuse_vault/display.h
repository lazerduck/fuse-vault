#ifndef FUSE_VAULT_DISPLAY_H
#define FUSE_VAULT_DISPLAY_H

#include "fuse_vault/app.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool (*initialize)(void *context);
    bool (*present)(void *context, const fv_ui_view_t *view);
} fv_display_ops_t;

typedef struct {
    const fv_display_ops_t *ops;
    void *context;
    fv_ui_view_t last_view;
    uint32_t minimum_interval_ms;
    uint32_t last_present_ms;
    bool initialized;
    bool has_presented;
} fv_display_t;

bool fv_display_init(fv_display_t *display, const fv_display_ops_t *ops,
                     void *context, uint32_t minimum_interval_ms);
bool fv_display_render(fv_display_t *display, const fv_app_t *app,
                       uint32_t now_ms);

#endif
