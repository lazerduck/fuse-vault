#include "fuse_vault/app.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    DISPLAY_WIDTH = 160,
    DISPLAY_HEIGHT = 80,
    DISPLAY_SCALE = 4,
};

typedef struct {
    fv_app_t app;
    fv_command_set_t last_commands;
    GtkWidget *display;
    GtkWidget *status;
} simulator_t;

static void draw_text(cairo_t *cr, double x, double y, double size,
                      const char *text, bool selected) {
    if (selected) {
        cairo_set_source_rgb(cr, 0.10, 0.75, 0.70);
        cairo_rectangle(cr, 2.0, y - size, DISPLAY_WIDTH - 4.0, size + 4.0);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.01, 0.04, 0.06);
    } else {
        cairo_set_source_rgb(cr, 0.88, 0.94, 0.95);
    }
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

static gboolean draw_display(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    simulator_t *simulator = data;
    fv_ui_view_t view;
    fv_app_render(&simulator->app, &view);

    cairo_scale(cr, DISPLAY_SCALE, DISPLAY_SCALE);
    cairo_set_source_rgb(cr, 0.01, 0.04, 0.06);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 7.0);
    draw_text(cr, 4.0, 10.0, 7.0, view.title, false);

    cairo_set_source_rgb(cr, 0.10, 0.75, 0.70);
    cairo_rectangle(cr, 4.0, 14.0, 152.0, 1.0);
    cairo_fill(cr);

    cairo_set_font_size(cr, 6.0);
    for (size_t index = 0u; index < FV_UI_LINE_COUNT; ++index) {
        const double y = 25.0 + (double)index * 11.0;
        const bool selected = view.lines[index][0] == '>';
        const char *line = selected ? &view.lines[index][1] : view.lines[index];
        draw_text(cr, selected ? 6.0 : 4.0, y, 6.0, line, selected);
    }

    cairo_set_source_rgb(cr, 0.35, 0.42, 0.44);
    cairo_rectangle(cr, 4.0, 68.0, 152.0, 1.0);
    cairo_fill(cr);
    cairo_set_font_size(cr, 5.0);
    draw_text(cr, 4.0, 76.0, 5.0, "ARROWS  ENTER  BACK", false);
    return FALSE;
}

static void refresh(simulator_t *simulator) {
    char text[160];
    (void)snprintf(text, sizeof(text),
                   "State: %s    Commands: 0x%08x\n"
                   "Test injection: Y success, X failure, C counter stored, "
                   "O provisioning complete, E eject, K lock, F fault",
                   fv_state_name(simulator->app.state),
                   (unsigned)simulator->last_commands);
    gtk_label_set_text(GTK_LABEL(simulator->status), text);
    gtk_widget_queue_draw(simulator->display);
}

static bool key_to_event(guint key, fv_event_t *event) {
    switch (key) {
        case GDK_KEY_Up:        *event = FV_EVENT_UP; return true;
        case GDK_KEY_Down:      *event = FV_EVENT_DOWN; return true;
        case GDK_KEY_Left:      *event = FV_EVENT_LEFT; return true;
        case GDK_KEY_Right:     *event = FV_EVENT_RIGHT; return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_space:     *event = FV_EVENT_SELECT; return true;
        case GDK_KEY_BackSpace:
        case GDK_KEY_Escape:    *event = FV_EVENT_BACK; return true;
        case GDK_KEY_y:
        case GDK_KEY_Y:         *event = FV_EVENT_AUTH_SUCCEEDED; return true;
        case GDK_KEY_x:
        case GDK_KEY_X:         *event = FV_EVENT_AUTH_FAILED; return true;
        case GDK_KEY_c:
        case GDK_KEY_C:         *event = FV_EVENT_ATTEMPT_COUNTER_STORED; return true;
        case GDK_KEY_o:
        case GDK_KEY_O:         *event = FV_EVENT_PROVISIONING_SUCCEEDED; return true;
        case GDK_KEY_e:
        case GDK_KEY_E:         *event = FV_EVENT_USB_EJECTED; return true;
        case GDK_KEY_k:
        case GDK_KEY_K:         *event = FV_EVENT_LOCK_REQUESTED; return true;
        case GDK_KEY_f:
        case GDK_KEY_F:         *event = FV_EVENT_STORAGE_FAILED; return true;
        default: return false;
    }
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *key_event,
                             gpointer data) {
    (void)widget;
    simulator_t *simulator = data;
    fv_event_t event;
    if (!key_to_event(key_event->keyval, &event)) {
        return FALSE;
    }
    simulator->last_commands = fv_app_handle(&simulator->app, event);
    refresh(simulator);
    return TRUE;
}

int main(int argc, char **argv) {
    bool provisioned = true;
    if (argc == 2 && strcmp(argv[1], "--unprovisioned") == 0) {
        provisioned = false;
    }

    gtk_init(&argc, &argv);
    simulator_t simulator = {0};
    fv_app_init(&simulator.app, provisioned, 0u);
    simulator.last_commands = fv_app_handle(&simulator.app,
                                             FV_EVENT_BOOT_COMPLETED);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Fuse Vault display simulator");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 16u);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), layout);

    simulator.display = gtk_drawing_area_new();
    gtk_widget_set_size_request(simulator.display,
                                DISPLAY_WIDTH * DISPLAY_SCALE,
                                DISPLAY_HEIGHT * DISPLAY_SCALE);
    gtk_box_pack_start(GTK_BOX(layout), simulator.display, FALSE, FALSE, 0u);

    simulator.status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(simulator.status), 0.0F);
    gtk_box_pack_start(GTK_BOX(layout), simulator.status, FALSE, FALSE, 0u);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),
                     &simulator);
    g_signal_connect(simulator.display, "draw", G_CALLBACK(draw_display),
                     &simulator);

    refresh(&simulator);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
