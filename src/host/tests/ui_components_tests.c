#include "button_input.h"
#include "device_ui.h"
#include "drawing.h"
#include "startup.h"
#include "scene.h"
#include "widgets/menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void debounce_tests(void) {
    fv_button_input input;
    fv_button_input_init(&input);
    CHECK(!fv_button_input_sample(&input, 1, 0));
    CHECK(!fv_button_input_sample(&input, 0, 1000));
    CHECK(!fv_button_input_sample(&input, 1, 2000));
    CHECK(!fv_button_input_sample(&input, 1, 26999));
    CHECK(fv_button_input_sample(&input, 1, 27000) == 1);
    CHECK(!fv_button_input_sample(&input, 1, 100000)); /* A held key never repeats. */
    CHECK(!fv_button_input_sample(&input, 0, 101000));
    CHECK(!fv_button_input_sample(&input, 0, 126000));
    CHECK(!fv_button_input_sample(&input, 1, 127000));
    CHECK(fv_button_input_sample(&input, 1, 152000) == 1);

    /* A held Select must not approve a second prompt. A bouncing release is
     * insufficient: require a complete stable release followed by a new press. */
    fv_button_input_require_release(&input, 153000);
    CHECK(!fv_button_input_sample(&input, 1, 200000));
    CHECK(!fv_button_input_sample(&input, 0, 201000));
    CHECK(!fv_button_input_sample(&input, 1, 202000));
    CHECK(!fv_button_input_sample(&input, 1, 227000));
    CHECK(!fv_button_input_sample(&input, 0, 228000));
    CHECK(!fv_button_input_sample(&input, 0, 253000));
    CHECK(!fv_button_input_sample(&input, 1, 254000));
    CHECK(fv_button_input_sample(&input, 1, 279000) == 1);

    fv_button_input_init(&input);
    CHECK(!fv_button_input_sample(&input, 3, 0));
    CHECK(!fv_button_input_sample(&input, 3, 25000)); /* Ignore simultaneous keys. */
    CHECK(!fv_button_input_sample(&input, 1, 26000));
    CHECK(!fv_button_input_sample(&input, 1, 51000)); /* Releasing half a chord isn't a press. */
    CHECK(!fv_button_input_sample(&input, 0, 52000));
    CHECK(!fv_button_input_sample(&input, 0, 77000));
    CHECK(!fv_button_input_sample(&input, 32, 78000));
    CHECK(fv_button_input_sample(&input, 32, 103000) == 32);
}

static void select_first(fv_ui *u) {
    u->error = 10;
}
static void select_second(fv_ui *u) {
    u->error = 20;
}

static void menu_tests(void) {
    static const ui_menu_item items[] = {{"FIRST", select_first}, {"SECOND", select_second}};
    const ui_menu menu = {items, 2, 15, 12, 9, 1, 2};
    fv_ui a = {0}, b = {0};
    ui_menu_key(&a, &menu, UI_UP);
    CHECK(a.cursor == 1 && !b.cursor);
    ui_menu_key(&a, &menu, UI_SELECT);
    CHECK(a.error == 20 && !b.error);
    ui_menu_key(&b, &menu, UI_SELECT);
    CHECK(b.error == 10);
    ui_menu_key(&a, &menu, UI_DOWN);
    ui_menu_key(&a, &menu, UI_SELECT);
    CHECK(!a.cursor && a.error == 10);
    ui_menu_render(&a, &menu);
    CHECK(a.error == 10 && !a.cursor); /* Rendering cannot invoke actions. */
    ui_menu empty = {0};
    ui_menu_key(&a, &empty, UI_SELECT);
    CHECK(a.error == 10);
}

static void scene_tests(void) {
    fv_ui u;
    fv_ui_init(&u);
    for (unsigned i = 0; i < UI_SCREEN_COUNT; ++i)
        CHECK(ui_scene_get((fv_ui_screen)i));
    CHECK(!ui_scene_get((fv_ui_screen)-1));
    CHECK(!ui_scene_get(UI_SCREEN_COUNT));

    fv_ui_complete(&u, (fv_ui_result){.status = 2, .profile = 3, .unlocked = true});
    u.job.profile = 3;
    u.entry.values[0] = 91;
    ui_scene_show(&u, UI_SECRET);
    CHECK(!u.entry.values[0] && u.entry.profile == 3);
    fv_ui_keypress(&u, UI_UP);
    CHECK(u.entry.values[0] == 1);
    fv_ui_render(&u);
    CHECK(u.entry.values[0] == 1); /* Drawing must not re-enter/reset the scene. */
    ui_scene_show(&u, UI_CONFIRM);
    CHECK(!u.entry.values[0]);
    u.cursor = 1;
    ui_scene_show(&u, UI_ERASE_CONFIRM);
    CHECK(!u.cursor); /* Destructive scenes always enter on Cancel. */
    fv_ui_keypress(&u, UI_SELECT);
    CHECK(u.screen == UI_SETTINGS && !u.pending);

    /* FIDO-disabled settings cannot navigate to or submit hidden FIDO actions. */
    ui_scene_show(&u, UI_SETTINGS);
    fv_ui_keypress(&u, UI_UP);
    fv_ui_keypress(&u, UI_UP);
    CHECK(u.cursor == 2);
    fv_ui_keypress(&u, UI_SELECT);
    CHECK(u.screen == UI_SECRET && u.flow == UI_FLOW_CHANGE && !u.pending);
    fv_ui_keypress(&u, UI_BACK);
    CHECK(u.screen == UI_DASHBOARD && u.flow == UI_FLOW_SETUP);
    CHECK(!memcmp(u.job.secret, (uint8_t[FV_UI_SECRET_MAX]){0}, FV_UI_SECRET_MAX));
}

static void dashboard_tests(void) {
    fv_ui u;
    fv_ui_init(&u);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true});
    CHECK(u.screen==UI_DASHBOARD && !u.dashboard_bars);
    fv_ui_activity(&u,1024,2048,500000);
    CHECK(u.read_kib_tenths==20 && u.write_kib_tenths==40 && u.history_count==1);
    fv_ui_activity(&u,1,1,0);CHECK(u.history_count==1);
    for(unsigned i=0;i<70;i++)fv_ui_activity(&u,i*1024,0,500000);
    CHECK(u.history_count==60 && u.history_next==11 && u.read_kib_tenths==1380);
    fv_ui_keypress(&u,UI_RIGHT);CHECK(!u.dashboard_bars && u.flipped);
    fv_ui_keypress(&u,UI_LEFT);CHECK(!u.flipped);
    ui_scene_show(&u,UI_DASHBOARD_SETTING);
    fv_ui_keypress(&u,UI_DOWN);fv_ui_keypress(&u,UI_BACK);
    CHECK(!u.dashboard_bars && u.screen==UI_SETTINGS);
    fv_ui_keypress(&u,UI_SELECT);CHECK(u.screen==UI_DASHBOARD_SETTING);
    fv_ui_keypress(&u,UI_DOWN);fv_ui_keypress(&u,UI_SELECT);
    CHECK(u.dashboard_bars && u.screen==UI_SETTINGS);
    ui_scene_show(&u,UI_DASHBOARD);
    fv_ui_keypress(&u,UI_SELECT);CHECK(u.screen==UI_SETTINGS && !u.pending);
    fv_ui_keypress(&u,UI_BACK);CHECK(u.screen==UI_DASHBOARD && !u.pending);
    fv_ui_keypress(&u,UI_BACK);CHECK(u.screen==UI_WAIT && u.job.op==UI_LOCK && u.pending);
    fv_ui_complete(&u,(fv_ui_result){.status=2});
    CHECK(u.screen==UI_HOME && !u.history_count && !u.read_kib_tenths && u.dashboard_bars);
    fv_ui_activity(&u,1024,1024,500000);CHECK(!u.history_count);
    fv_ui_complete(&u,(fv_ui_result){.status=2,.unlocked=true});
    CHECK(u.screen==UI_DASHBOARD && u.dashboard_bars);
    fv_ui_activity(&u,UINT32_MAX,UINT32_MAX,1);
    CHECK(u.read_kib_tenths==UINT32_MAX);
    fv_ui_keypress(&u,UI_LEFT);CHECK(u.dashboard_bars && u.flipped);
}

static void approval_text_tests(void) {
    fv_ui u;fv_ui_init(&u);
    char label[128];memset(label,'A',127);label[127]=0;
    fv_ui_fido_begin(&u,false,0,label);
    uint8_t before[FV_SCREEN_BYTES];memcpy(before,u.framebuffer,sizeof(before));
    label[126]='B';fv_ui_fido_begin(&u,false,0,label);
    CHECK(memcmp(before,u.framebuffer,sizeof(before))); /* The end must remain visible. */
    CHECK(!u.fido_done && !u.fido_approved);
    fv_ui_keypress(&u,UI_BACK);CHECK(u.fido_done && !u.fido_approved);
}

int main(void) {
    dashboard_tests();
    fv_ui colour_ui;
    fv_ui_init(&colour_ui);
    memset(colour_ui.framebuffer, 0, sizeof(colour_ui.framebuffer));
    for(unsigned i=0;i<256;i++)ui_draw_pixel(&colour_ui,i%160,i/160,(uint8_t)i);
    for(unsigned i=0;i<256;i++)CHECK(colour_ui.framebuffer[i]==i);
    ui_draw_pixel(&colour_ui,160,0,255);
    ui_draw_pixel(&colour_ui,0,80,255);
    CHECK(colour_ui.framebuffer[160]==160);
    ui_draw_text_at(&colour_ui,0,10,"A",1,UI_COLOUR_ERROR);
    CHECK(colour_ui.framebuffer[10*160+1]==UI_COLOUR_ERROR);
    for(unsigned frame=0;frame<FV_STARTUP_FRAMES;frame++) {
        ui_draw_splash(&colour_ui,frame);
        for(unsigned pixel=0;pixel<FV_SCREEN_BYTES;pixel++)
            CHECK(colour_ui.framebuffer[pixel]==fv_startup_pixel(frame,pixel));
    }
    CHECK(fv_startup_pixel(FV_STARTUP_FRAMES,0)==0);
    CHECK(fv_startup_pixel(0,FV_SCREEN_BYTES)==0);
    approval_text_tests();
    debounce_tests();
    menu_tests();
    scene_tests();
    puts("Button debounce, release gating, menu callbacks and scene lifecycle checks passed");
    return 0;
}
