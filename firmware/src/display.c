#include "fuse_vault/display.h"

#include <string.h>

bool fv_display_init(fv_display_t *display, const fv_display_ops_t *ops,
                     void *context, uint32_t minimum_interval_ms) {
    if (display == NULL || ops == NULL || ops->initialize == NULL ||
        ops->present == NULL) return false;
    memset(display, 0, sizeof(*display));
    display->ops = ops;
    display->context = context;
    display->minimum_interval_ms = minimum_interval_ms;
    display->initialized = ops->initialize(context);
    return display->initialized;
}

bool fv_display_render(fv_display_t *display, const fv_app_t *app,
                       uint32_t now_ms) {
    if (display == NULL || app == NULL || !display->initialized) return false;
    fv_ui_view_t next;
    fv_app_render(app, &next);
    if (display->has_presented &&
        memcmp(&next, &display->last_view, sizeof(next)) == 0) return true;
    if (display->has_presented &&
        (uint32_t)(now_ms - display->last_present_ms) <
            display->minimum_interval_ms) return true;
    if (!display->ops->present(display->context, &next)) {
        display->initialized = false;
        return false;
    }
    display->last_view = next;
    display->last_present_ms = now_ms;
    display->has_presented = true;
    return true;
}
