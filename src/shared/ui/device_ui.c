#include "device_ui.h"
#include "scene.h"
#include <stdio.h>
#include <string.h>

void fv_ui_wipe(void *p, size_t size) {
    volatile uint8_t *bytes = p;
    while (size--)
        *bytes++ = 0;
}

void ui_clear_input(fv_ui *u) {
    u->flow = UI_FLOW_SETUP;
    fv_ui_wipe(u->job.secret, sizeof(u->job.secret));
    fv_ui_wipe(u->job.current, sizeof(u->job.current));
    fv_ui_wipe(u->confirmation, sizeof(u->confirmation));
    fv_ui_wipe(&u->entry, sizeof(u->entry));
    u->job.length = 0;
    u->job.current_length = 0;
    u->confirmation_length = 0;
}

void ui_submit(fv_ui *u, fv_ui_operation operation) {
    u->format_done = u->format_total = u->format_milliseconds = 0;
    u->job.op = operation;
    u->pending = true;
    ui_scene_show(u, UI_WAIT);
}

void ui_go_home(fv_ui *u) {
    ui_clear_input(u);
    ui_scene_show(u, UI_HOME);
}

void fv_ui_init(fv_ui *u) {
    memset(u, 0, sizeof(*u));
    ui_submit(u, UI_STATUS);
    fv_ui_render(u);
}

void fv_ui_cancel(fv_ui *u) {
    ui_clear_input(u);
    u->cursor = 0;
    /* An accepted worker job must finish; cancellation must not resubmit it. */
    if (u->screen != UI_WAIT || u->pending)
        ui_submit(u, UI_STATUS);
    fv_ui_render(u);
}

void fv_ui_complete(fv_ui *u, fv_ui_result result) {
    ui_clear_input(u);
    u->pending = false;
    u->device = result;
    u->error = result.result;
    if (result.result) {
        ui_scene_show(u, UI_ERROR);
    } else if (u->job.op == UI_PASSKEY_LIST || u->job.op == UI_PASSKEY_DELETE) {
        u->passkey_page = 0;
        u->job.passkey_index = result.passkey_index;
        memcpy(u->job.passkey_id, result.passkey_id, sizeof(u->job.passkey_id));
        ui_scene_show(u, UI_PASSKEYS);
    } else {
        ui_scene_show(u, UI_HOME);
    }
    fv_ui_render(u);
}

void fv_ui_fido_begin(fv_ui *u, bool secret, uint16_t profile, const char *label) {
    ui_clear_input(u);
    ++u->fido_generation;
    u->pending = false;
    u->fido_modal = true;
    u->fido_done = false;
    u->fido_approved = false;
    u->job.profile = profile;
    u->error = 0;
    snprintf(u->fido_label, sizeof(u->fido_label), "%s", label);
    ui_scene_show(u, secret ? UI_SECRET : UI_FIDO_APPROVE);
    fv_ui_render(u);
}

void fv_ui_fido_end(fv_ui *u) {
    ui_clear_input(u);
    ++u->fido_generation;
    u->fido_modal = false;
    u->fido_done = false;
    u->fido_approved = false;
    fv_ui_wipe(u->fido_label, sizeof(u->fido_label));
    ui_submit(u, UI_STATUS);
    fv_ui_render(u);
}

void fv_ui_keypress(fv_ui *u, fv_ui_key key) {
    if (key < UI_UP || key > UI_BACK || u->screen == UI_WAIT || u->fido_done)
        return;
    /* Back always rejects an active FIDO request, including credential entry. */
    if (u->fido_modal && key == UI_BACK) {
        u->fido_done = true;
        u->fido_approved = false;
    } else {
        if (u->flipped) {
            static const fv_ui_key rotated[] = {0,       UI_DOWN,   UI_UP,  UI_RIGHT,
                                                UI_LEFT, UI_SELECT, UI_BACK};
            key = rotated[key];
        }
        const ui_scene *scene = ui_scene_get(u->screen);
        if (scene && scene->key)
            scene->key(u, key);
    }
    fv_ui_render(u);
}

static uint8_t reverse_bits(uint8_t byte) {
    uint8_t reversed = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        reversed = (uint8_t)((reversed << 1) | (byte & 1));
        byte >>= 1;
    }
    return reversed;
}

void fv_ui_render(fv_ui *u) {
    memset(u->framebuffer, 0, sizeof(u->framebuffer));
    const ui_scene *scene = ui_scene_get(u->screen);
    if (scene && scene->render)
        scene->render(u);
    if (u->flipped) {
        /* A 180-degree rotation reverses both byte order and bit order. */
        for (unsigned i = 0; i < FV_SCREEN_BYTES / 2; ++i) {
            unsigned opposite = FV_SCREEN_BYTES - 1 - i;
            uint8_t first = u->framebuffer[i];
            u->framebuffer[i] = reverse_bits(u->framebuffer[opposite]);
            u->framebuffer[opposite] = reverse_bits(first);
        }
    }
}
