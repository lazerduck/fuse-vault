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
    u->busy_frame = 0;
    u->job.op = operation;
    u->pending = true;
    ui_scene_show(u, UI_WAIT);
}

void ui_go_home(fv_ui *u) {
    ui_clear_input(u);
    ui_scene_show(u, u->device.unlocked ? UI_DASHBOARD : UI_HOME);
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
    if (!result.unlocked || !u->device.unlocked) {
        memset(u->read_history, 0, sizeof(u->read_history));
        memset(u->write_history, 0, sizeof(u->write_history));
        u->history_next = u->history_count = 0;
        u->read_kib_tenths = u->write_kib_tenths = 0;
        u->read_active = u->write_active = false;
    }
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
        ui_scene_show(u, result.unlocked ? UI_DASHBOARD : UI_HOME);
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

void fv_ui_render(fv_ui *u) {
    memset(u->framebuffer, 0, sizeof(u->framebuffer));
    const ui_scene *scene = ui_scene_get(u->screen);
    if (scene && scene->render)
        scene->render(u);
    if (u->flipped) {
        /* A 180-degree rotation reverses pixel order, preserving colour. */
        for (unsigned i = 0; i < FV_SCREEN_BYTES / 2; ++i) {
            unsigned opposite = FV_SCREEN_BYTES - 1 - i;
            uint8_t first = u->framebuffer[i];
            u->framebuffer[i] = u->framebuffer[opposite];
            u->framebuffer[opposite] = first;
        }
    }
}

void fv_ui_activity(fv_ui *u, uint32_t reads, uint32_t writes, uint64_t elapsed_us) {
    if (!u->device.unlocked || !elapsed_us) return;
    /* Divide before the KiB conversion to avoid elapsed_us * 1024 overflow. */
    uint64_t read_rate = ((uint64_t)reads * 10000000 / elapsed_us) / 1024;
    uint64_t write_rate = ((uint64_t)writes * 10000000 / elapsed_us) / 1024;
    u->read_kib_tenths = read_rate > UINT32_MAX ? UINT32_MAX : (uint32_t)read_rate;
    u->write_kib_tenths = write_rate > UINT32_MAX ? UINT32_MAX : (uint32_t)write_rate;
    u->read_active = reads != 0;
    u->write_active = writes != 0;
    u->read_history[u->history_next] = u->read_kib_tenths;
    u->write_history[u->history_next] = u->write_kib_tenths;
    u->history_next = (u->history_next + 1) % FV_UI_HISTORY_SAMPLES;
    if (u->history_count < FV_UI_HISTORY_SAMPLES) ++u->history_count;
    if (u->screen == UI_DASHBOARD || u->screen == UI_HOME) fv_ui_render(u);
}

void fv_ui_animate(fv_ui *u, uint32_t milliseconds) {
    unsigned frame = (milliseconds / 100) % 12;
    if (u->screen != UI_WAIT || frame == u->busy_frame) return;
    u->busy_frame = frame;
    fv_ui_render(u);
}
