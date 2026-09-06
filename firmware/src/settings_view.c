#include "fuse_vault/settings.h"
#include "fuse_vault/secret_input.h"
#include <stdio.h>
#include <string.h>

void fv_settings_render(const fv_app_t *app, fv_ui_view_t *view) {
    /* Four rows, with a scrolling three-item window and a persistent action row.
     * Labels stay within the physical display's 25-character text width. */
    memset(view, 0, sizeof(*view));
    switch (app->state) {
        case FV_STATE_SETTINGS: {
            snprintf(view->title, sizeof(view->title), "Settings");
            size_t first = app->selected_setting < 3u ? 0u : app->selected_setting - 2u;
            for (size_t row = 0; row < 3u; ++row) {
                const fv_setting_item_t *item = fv_settings_item(first + row);
                if (item) snprintf(view->lines[row], sizeof(view->lines[row]), "%s %.23s",
                    first + row == app->selected_setting ? ">" : " ", item->label);
            }
            snprintf(view->lines[3], sizeof(view->lines[3]), "Select: open Back: lock");
            break;
        }
        case FV_STATE_CHANGE_METHOD:
            snprintf(view->title, sizeof(view->title), "New entry method");
            snprintf(view->lines[0], sizeof(view->lines[0]), "%.25s",
                     fv_secret_method_name(app->change_entry_method));
            snprintf(view->lines[1], sizeof(view->lines[1]), "Then set a new password");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Up/down; Select: choose");
            break;
        case FV_STATE_CHANGE_SECRET:
        case FV_STATE_CHANGE_CONFIRM:
            snprintf(view->title, sizeof(view->title), "%s",
                app->state == FV_STATE_CHANGE_SECRET ? "New password" : "Confirm new password");
            fv_secret_entry_render(&app->secret_entry, view);
            if (strcmp(view->select_action, "Done") == 0)
                snprintf(view->select_action, sizeof(view->select_action), "%s",
                    app->state == FV_STATE_CHANGE_SECRET ? "Next" : "Confirm");
            break;
        case FV_STATE_CHANGE_MISMATCH:
            snprintf(view->title, sizeof(view->title), "Passwords differ");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Nothing was changed");
            snprintf(view->lines[1], sizeof(view->lines[1]), "No unlock attempt used");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Select: retry Back: cancel");
            break;
        case FV_STATE_CHANGE_REVIEW:
            snprintf(view->title, sizeof(view->title), "Save new password?");
            snprintf(view->lines[0], sizeof(view->lines[0]), "%.25s",
                     fv_secret_method_name(app->change_entry_method));
            snprintf(view->lines[1], sizeof(view->lines[1]), "Files and passkeys kept");
            snprintf(view->lines[2], sizeof(view->lines[2]), "Device locks after saving");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Select: save Back: cancel");
            break;
        case FV_STATE_CHANGE_SAVING:
            view->hide_controls = true;
            snprintf(view->title, sizeof(view->title), "Saving password");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Do not remove power");
            break;
        case FV_STATE_CHANGE_SAVED:
            snprintf(view->title, sizeof(view->title), "Password changed");
            snprintf(view->lines[0], sizeof(view->lines[0]), "Device locked");
            snprintf(view->lines[1], sizeof(view->lines[1]), "Use your new password");
            snprintf(view->lines[3], sizeof(view->lines[3]), "Select: continue");
            break;
        default: break;
    }
}
