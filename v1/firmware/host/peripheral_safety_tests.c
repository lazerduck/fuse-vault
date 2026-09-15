#include "fuse_vault/app.h"
#include "fuse_vault/display.h"
#include "fuse_vault/peripheral_safety.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) check((c), #c, __LINE__)
static void check(bool value, const char *text, int line) {
    if (!value) { fprintf(stderr, "%d: check failed: %s\n", line, text); exit(1); }
}

typedef struct { bool init_ok; bool present_ok; unsigned init; unsigned frames; } display_fake_t;
static bool display_init(void *context) { display_fake_t *f = context; ++f->init; return f->init_ok; }
static bool display_present(void *context, const fv_ui_view_t *view) {
    display_fake_t *f = context; ++f->frames; return f->present_ok && view->title[0] != '\0';
}
static const fv_display_ops_t display_ops = { display_init, display_present };

typedef struct { unsigned sequence[32]; unsigned count; bool disable_ok; bool inputs_ok; bool route_ok; bool a; bool c; } connector_fake_t;
static bool mux_disable(void *context) { connector_fake_t *f=context; f->sequence[f->count++]=1u; return f->disable_ok; }
static bool inputs(void *context) { connector_fake_t *f=context; f->sequence[f->count++]=2u; return f->inputs_ok; }
static bool read_a(void *context, bool *present) { connector_fake_t *f=context; f->sequence[f->count++]=3u; *present=f->a; return true; }
static bool read_c(void *context, bool *present) { connector_fake_t *f=context; f->sequence[f->count++]=4u; *present=f->c; return true; }
static bool route(void *context, fv_connector_state_t connector) { connector_fake_t *f=context; f->sequence[f->count++]=connector == FV_CONNECTOR_USB_A ? 5u : 6u; return f->route_ok; }
static const fv_connector_ops_t connector_ops = {
    .disable_mux=mux_disable, .configure_presence_inputs=inputs,
    .read_usb_a_present=read_a, .read_usb_c_present=read_c,
    .route_connector=route,
};

typedef struct { fv_app_t *app; unsigned calls; fv_command_set_t commands; } fault_sink_t;
static void app_fault(void *context) {
    fault_sink_t *sink=context; ++sink->calls;
    sink->commands |= fv_app_handle(sink->app, FV_EVENT_STORAGE_FAILED);
}

typedef struct { bool present; fv_block_result_t next; unsigned calls; uint8_t data[FV_BLOCK_SIZE]; } block_fake_t;
static fv_block_result_t raw_read(fv_block_device_t *d, uint64_t first, uint32_t count, uint8_t *out) {
    block_fake_t *f=d->context; ++f->calls; if (f->next != FV_BLOCK_OK) return f->next;
    if (first != 0u || count != 1u) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(out, f->data, sizeof(f->data)); return FV_BLOCK_OK;
}
static fv_block_result_t raw_write(fv_block_device_t *d, uint64_t first, uint32_t count, const uint8_t *in) {
    block_fake_t *f=d->context; ++f->calls; if (f->next != FV_BLOCK_OK) return f->next;
    if (first != 0u || count != 1u) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(f->data, in, sizeof(f->data)); return FV_BLOCK_OK;
}
static fv_block_result_t raw_sync(fv_block_device_t *d) { block_fake_t *f=d->context; ++f->calls; return f->next; }
static uint64_t raw_count(const fv_block_device_t *d) { (void)d; return 1u; }
static bool raw_present(const fv_block_device_t *d) { const block_fake_t *f=d->context; return f->present; }
static const fv_block_device_ops_t raw_ops = { raw_read, raw_write, raw_sync, raw_count, raw_present };
static bool detect(void *context) { return ((block_fake_t *)context)->present; }

static void test_display(void) {
    fv_app_t app; fv_app_init(&app, false, 0u, FV_ENTRY_METHOD_WHEELS);
    display_fake_t fake = {.init_ok=false, .present_ok=true}; fv_display_t display;
    CHECK(!fv_display_init(&display, &display_ops, &fake, 10u));
    fake.init_ok=true; CHECK(fv_display_init(&display, &display_ops, &fake, 10u));
    CHECK(fv_display_render(&display, &app, 0u)); CHECK(fake.frames == 1u);
    CHECK(fv_display_render(&display, &app, 20u)); CHECK(fake.frames == 1u);
    (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    CHECK(fv_display_render(&display, &app, 5u)); CHECK(fake.frames == 1u);
    CHECK(fv_display_render(&display, &app, 10u)); CHECK(fake.frames == 2u);
    fake.present_ok=false; (void)fv_app_handle(&app, FV_EVENT_SELECT);
    CHECK(!fv_display_render(&display, &app, 20u)); CHECK(!display.initialized);
}

static void test_connector_order_and_fault(void) {
    fv_app_t app; fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS); (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    fault_sink_t sink={.app=&app}; connector_fake_t fake={.disable_ok=true,.inputs_ok=true,.route_ok=true,.a=true};
    fv_connector_safety_t safety;
    CHECK(fv_connector_safety_init(&safety, &connector_ops, &fake, app_fault, &sink));
    CHECK(fake.sequence[0] == 1u && fake.sequence[1] == 2u);
    CHECK(safety.state == FV_CONNECTOR_USB_A);
    CHECK(fv_connector_safety_route(&safety));
    CHECK(safety.routed && safety.routed_state == FV_CONNECTOR_USB_A);
    CHECK(fake.sequence[4] == 1u && fake.sequence[5] == 5u);
    CHECK(!fv_connector_safety_route(&safety));
    fake.c=true; CHECK(!fv_connector_safety_poll(&safety));
    CHECK(fake.sequence[fake.count - 1u] == 1u);
    CHECK(sink.calls == 1u && app.state == FV_STATE_FAULT);
    CHECK((sink.commands & FV_COMMAND_USB_DETACH) != 0u);
    CHECK((sink.commands & FV_COMMAND_ERASE_SESSION_KEYS) != 0u);

    connector_fake_t failed={.disable_ok=false,.inputs_ok=true}; sink.calls=0u;
    CHECK(!fv_connector_safety_init(&safety, &connector_ops, &failed, app_fault, &sink));
    CHECK(failed.sequence[0] == 1u && failed.count == 2u); /* retry stays disabled */

    connector_fake_t no_connector={.disable_ok=true,.inputs_ok=true,.route_ok=true};
    sink.calls=0u;
    CHECK(fv_connector_safety_init(&safety, &connector_ops, &no_connector,
                                   app_fault, &sink));
    CHECK(!fv_connector_safety_route(&safety));
    CHECK(!safety.faulted && sink.calls == 0u);

    connector_fake_t route_failed={.disable_ok=true,.inputs_ok=true,
        .route_ok=false,.c=true};
    CHECK(fv_connector_safety_init(&safety, &connector_ops, &route_failed,
                                   app_fault, &sink));
    CHECK(!fv_connector_safety_route(&safety));
    CHECK(safety.faulted && sink.calls == 1u);
    fv_connector_safety_fault(&safety);
    CHECK(sink.calls == 1u);
}

static void test_removal_and_io_failure(void) {
    fv_app_t app; fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS); (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    fault_sink_t sink={.app=&app}; block_fake_t fake={.present=true,.next=FV_BLOCK_OK};
    fv_block_device_t raw={.ops=&raw_ops,.context=&fake}; fv_removable_block_t guard;
    CHECK(fv_removable_block_init(&guard,&raw,detect,&fake,app_fault,&sink));
    uint8_t block[FV_BLOCK_SIZE]={7u};
    CHECK(guard.interface.ops->write(&guard.interface,0u,1u,block) == FV_BLOCK_OK);
    fake.present=false; fv_removable_block_poll(&guard);
    CHECK(sink.calls == 1u && app.state == FV_STATE_FAULT);
    CHECK(guard.interface.ops->read(&guard.interface,0u,1u,block) == FV_BLOCK_ERROR_NOT_READY);
    CHECK(sink.calls == 1u);

    fv_app_init(&app, true, 0u, FV_ENTRY_METHOD_WHEELS); (void)fv_app_handle(&app, FV_EVENT_BOOT_COMPLETED);
    sink.calls=0u; sink.commands=0u; fake.present=true; fake.next=FV_BLOCK_ERROR_IO;
    CHECK(fv_removable_block_init(&guard,&raw,detect,&fake,app_fault,&sink));
    CHECK(guard.interface.ops->sync(&guard.interface) == FV_BLOCK_ERROR_IO);
    CHECK(sink.calls == 1u && app.state == FV_STATE_FAULT && guard.faulted);
}

int main(void) { test_display(); test_connector_order_and_fault(); test_removal_and_io_failure(); puts("peripheral safety tests passed"); }
