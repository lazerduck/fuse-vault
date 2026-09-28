#include "scene.h"
#include "drawing.h"
#include "widgets/menu.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static unsigned ui_passkey_pages(const fv_ui *u) {
    size_t site = strlen(u->device.passkey_site);
    size_t account = strlen(u->device.passkey_account);
    size_t length = site > account ? site : account;
    return length ? (unsigned)((length + 49) / 50) : 1;
}

static void passkeys_enter(fv_ui *u) {
    /* Require button release before accepting input in a new passkey prompt. */
    ++u->fido_generation;
}

static void page_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_LEFT && u->passkey_page)
        --u->passkey_page;
    if (key == UI_RIGHT && u->passkey_page + 1 < ui_passkey_pages(u))
        ++u->passkey_page;
}

static void passkeys_key(fv_ui *u, fv_ui_key key) {
    if (key == UI_BACK) {
        ui_settings_return(u, UI_SETTING_PASSKEYS);
    } else if (u->device.passkey_count) {
        if (key == UI_UP || key == UI_DOWN) {
            unsigned count = u->device.passkey_count;
            u->job.passkey_index =
                (uint16_t)((u->device.passkey_index + (key == UI_UP ? count - 1 : 1)) % count);
            ui_submit(u, UI_PASSKEY_LIST);
        } else if (key == UI_SELECT) {
            ui_scene_show(u, UI_PASSKEY_DELETE_CONFIRM);
        } else {
            page_key(u, key);
        }
    }
}

static void delete_passkey(fv_ui *u) {
    ui_submit(u, UI_PASSKEY_DELETE);
}
static void cancel_delete(fv_ui *u) {
    ui_scene_show(u, UI_PASSKEYS);
}
static void delete_key(fv_ui *u, fv_ui_key key) {
    page_key(u, key);
    ui_confirmation_key(u, key, delete_passkey, cancel_delete);
}

static void render_passkeys(fv_ui *u) {
    char b[40];
    bool deleting = u->screen == UI_PASSKEY_DELETE_CONFIRM;
    snprintf(b, sizeof(b), "%u/%u", u->device.passkey_count ? u->device.passkey_index + 1 : 0,
             u->device.passkey_count);
    ui_draw_header(u, deleting ? "DELETE PASSKEY?" : "PASSKEYS", b);
    if (!u->device.passkey_count) {
        ui_draw_text_at(u, 22, 33, "NO STORED PASSKEYS", 1, UI_COLOUR_TEXT);
        ui_draw_footer(u, "\005 BACK", NULL);
        return;
    }
    const char *values[] = {u->device.passkey_site, u->device.passkey_account};
    for (unsigned v = 0; v < 2; v++)
        for (unsigned row = 0; row < 2; row++) {
            size_t length = strlen(values[v]);
            unsigned last = length ? (unsigned)((length - 1) / 50) : 0;
            unsigned page = u->passkey_page < last ? u->passkey_page : last;
            unsigned off = page * 50 + row * 25;
            char part[26] = {0};
            if (length > off)
                strncpy(part, values[v] + off, 25);
            ui_draw_text_at(u, 4, 15 + (v * 2 + row) * (deleting ? 8 : 10), part, 1, UI_COLOUR_TEXT);
        }
    if (deleting)
        ui_draw_confirm_choices(u, "DELETE");
    else {
        unsigned pages = ui_passkey_pages(u);
        if (pages > 1) {
            snprintf(b, sizeof(b), "\003\001 TEXT %u/%u", u->passkey_page + 1, pages);
            ui_draw_text_at(u, 4, 57, b, 1, UI_COLOUR_TEXT);
        }
        ui_draw_footer(u, "\005 BACK", "\006 DELETE");
    }
}

const ui_scene ui_passkeys_scene = {
    .enter = passkeys_enter, .key = passkeys_key, .render = render_passkeys};
const ui_scene ui_passkey_delete_scene = {
    .enter = passkeys_enter, .key = delete_key, .render = render_passkeys};
