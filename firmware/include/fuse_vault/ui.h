#ifndef FUSE_VAULT_UI_H
#define FUSE_VAULT_UI_H

#include "fuse_vault/app.h"

#include <stdint.h>

#define FV_DISPLAY_WIDTH 160u
#define FV_DISPLAY_HEIGHT 80u

typedef uint16_t fv_pixel_t;

typedef struct {
    fv_pixel_t pixels[FV_DISPLAY_HEIGHT][FV_DISPLAY_WIDTH];
} fv_framebuffer_t;

void fv_ui_draw(const fv_app_t *app, fv_framebuffer_t *framebuffer);
void fv_ui_draw_view(const fv_ui_view_t *view, fv_framebuffer_t *framebuffer);

#endif
