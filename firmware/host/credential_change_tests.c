#include "fuse_vault/boot_recovery.h"
#include "fuse_vault/device_runtime.h"
#include "fuse_vault/device_services.h"
#include "fuse_vault/fido_store.h"
#include "fuse_vault/input.h"
#include "fuse_vault/secret_input.h"
#include "fuse_vault/settings.h"
#include "fuse_vault/virtual_msc.h"
#include "nor_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "credential change line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define CARD_BYTES (4096u * FV_BLOCK_SIZE)
typedef struct {
    uint8_t card[CARD_BYTES], flash[8192];
    uint16_t otp[4096];
    size_t write_limit;
    bool sync_fail, read_fail, detach_fail, random_fail;
    unsigned random_value, attaches, presentations;
    bool presentation_fail;
    fv_nor_flash_t nor;
    fv_block_device_t media;
    fv_journal_flash_t journal;
    fv_device_roots_storage_t roots;
    fv_platform_services_t services;
    fv_device_services_context_t context;
    fv_app_t app;
    fv_device_runtime_t runtime;
    fv_virtual_msc_t msc;
} fixture_t;
static const fv_credential_costs_t COSTS = {1u, 1u};
static const uint8_t DEVICE_ID[16] = "password-tests";

static fv_block_result_t read_card(fv_block_device_t *d, uint64_t b, uint32_t n, uint8_t *out) {
    fixture_t *f = d->context;
    if (f->read_fail) return FV_BLOCK_ERROR_IO;
    if (b + n > 4096u) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    memcpy(out, f->card + b * FV_BLOCK_SIZE, (size_t)n * FV_BLOCK_SIZE);
    return FV_BLOCK_OK;
}
static fv_block_result_t write_card(fv_block_device_t *d, uint64_t b, uint32_t n, const uint8_t *in) {
    fixture_t *f = d->context;
    if (b + n > 4096u) return FV_BLOCK_ERROR_OUT_OF_RANGE;
    size_t size = (size_t)n * FV_BLOCK_SIZE;
    size_t written = size < f->write_limit ? size : f->write_limit;
    memcpy(f->card + b * FV_BLOCK_SIZE, in, written);
    f->write_limit -= written;
    return written == size ? FV_BLOCK_OK : FV_BLOCK_ERROR_IO;
}
static fv_block_result_t sync_card(fv_block_device_t *d) {
    return ((fixture_t *)d->context)->sync_fail ? FV_BLOCK_ERROR_IO : FV_BLOCK_OK;
}
static uint64_t count_card(const fv_block_device_t *d) { (void)d; return 4096u; }
static bool present(const fv_block_device_t *d) { (void)d; return true; }
static const fv_block_device_ops_t CARD_OPS = {read_card, write_card, sync_card, count_card, present};
static bool otp_read(fv_device_roots_storage_t *s, uint16_t first, uint16_t *out, size_t n) {
    CHECK((size_t)first + n <= 4096u);
    memcpy(out, ((fixture_t *)s->context)->otp + first, n * sizeof(*out)); return true;
}
static bool otp_write(fv_device_roots_storage_t *s, uint16_t first, const uint16_t *in, size_t n) {
    fixture_t *f = s->context; CHECK((size_t)first + n <= 4096u);
    for (size_t i = 0; i < n; ++i) f->otp[first + i] |= in[i];
    return true;
}
static const fv_device_roots_storage_ops_t ROOT_OPS = {otp_read, otp_write};
static bool flash_read(fv_journal_flash_t *j, size_t o, uint8_t *out, size_t n) {
    return fv_nor_read(j->context, o, out, n) == FV_NOR_OK;
}
static bool flash_program(fv_journal_flash_t *j, size_t o, const uint8_t *in, size_t n) {
    return fv_nor_program(j->context, o, in, n) == FV_NOR_OK;
}
static bool flash_erase(fv_journal_flash_t *j, size_t o, size_t n) {
    return fv_nor_erase(j->context, o, n) == FV_NOR_OK;
}
static const fv_journal_flash_ops_t FLASH_OPS = {flash_read, flash_program, flash_erase};
static bool random_fill(void *context, uint8_t *out, size_t n) {
    fixture_t *f = context;
    if (f->random_fail) return false;
    for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)++f->random_value;
    return true;
}
static bool attach(void *context, fv_block_device_t *d) {
    fixture_t *f = context; ++f->attaches; return fv_virtual_msc_attach(&f->msc, d);
}
static bool detach(void *context) {
    fixture_t *f = context;
    if (f->detach_fail) return false;
    fv_virtual_msc_detach(&f->msc); return true;
}
static const fv_runtime_usb_ops_t USB_OPS = {.attach_msc = attach, .detach_usb = detach};
static void event(fixture_t *f, fv_event_t e) { fv_device_runtime_handle_event(&f->runtime, e); }
static void enter(fixture_t *f, fv_entry_method_t method, bool different) {
    if (method == FV_ENTRY_METHOD_WHEELS) {
        event(f, FV_EVENT_UP);
        if (different) event(f, FV_EVENT_UP);
        event(f, FV_EVENT_SELECT);
    } else if (method == FV_ENTRY_METHOD_DIRECTIONS) {
        for (unsigned i = 0; i < 6; ++i) event(f, different ? FV_EVENT_DOWN : FV_EVENT_UP);
        event(f, FV_EVENT_SELECT);
    } else if (method == FV_ENTRY_METHOD_KEYPAD) {
        if (different) event(f, FV_EVENT_RIGHT);
        for (unsigned i = 0; i < 4; ++i) event(f, FV_EVENT_SELECT);
        if (different) event(f, FV_EVENT_LEFT);
        event(f, FV_EVENT_LEFT); event(f, FV_EVENT_UP); event(f, FV_EVENT_SELECT);
    } else {
        for (unsigned i = 0; i < 12; ++i) event(f, different ? FV_EVENT_DOWN : FV_EVENT_UP);
        event(f, FV_EVENT_SELECT);
    }
}
static bool present_work(void *context, const fv_app_t *app) {
    fixture_t *f = context;
    CHECK(app->state == FV_STATE_CHANGE_SAVING && app->session_unlocked);
    fv_ui_view_t view;
    fv_app_render(app, &view);
    CHECK(strcmp(view.title, "Saving password") == 0 && view.hide_controls);
    ++f->presentations;
    return !f->presentation_fail;
}
static fixture_t *create(void) {
    fixture_t *f = calloc(1, sizeof(*f)); CHECK(f);
    f->write_limit = SIZE_MAX;
    f->media = (fv_block_device_t){&CARD_OPS, f};
    f->roots = (fv_device_roots_storage_t){&ROOT_OPS, f, true};
    CHECK(fv_nor_init(&f->nor, f->flash, sizeof(f->flash), 4096u, FV_JOURNAL_RECORD_SIZE));
    f->journal = (fv_journal_flash_t){&FLASH_OPS, &f->nor, sizeof(f->flash), 4096u, FV_JOURNAL_RECORD_SIZE};
    CHECK(fv_device_services_init(&f->services, &f->context, &f->roots, &f->journal,
        &f->media, DEVICE_ID, random_fill, f));
    fv_virtual_msc_init(&f->msc);
    fv_app_init(&f->app, false, 0, FV_ENTRY_METHOD_WHEELS);
    CHECK(fv_device_runtime_init(&f->runtime, &f->app, &f->services, &f->media, &USB_OPS, f, &COSTS));
    fv_device_runtime_set_present(&f->runtime, present_work, f);
    event(f, FV_EVENT_BOOT_COMPLETED);
    event(f, FV_EVENT_SELECT); event(f, FV_EVENT_SELECT); event(f, FV_EVENT_SELECT);
    enter(f, FV_ENTRY_METHOD_WHEELS, false); enter(f, FV_ENTRY_METHOD_WHEELS, false);
    event(f, FV_EVENT_SELECT); event(f, FV_EVENT_SELECT);
    CHECK(f->app.state == FV_STATE_MODE_SELECT);
    return f;
}
static void boot(fixture_t *f) {
    fv_device_runtime_shutdown(&f->runtime);
    CHECK(fv_device_services_init(&f->services, &f->context, &f->roots, &f->journal,
        &f->media, DEVICE_ID, random_fill, f));
    fv_security_state_t state; fv_entry_method_t method;
    CHECK(f->services.ops->load_security_state(&f->services, &state) == FV_PERSIST_OK);
    CHECK(fv_boot_recover_entry_method(&f->services, &method) == FV_BOOT_RECOVERY_OK);
    fv_app_init(&f->app, true, state.failed_attempts, method);
    CHECK(fv_device_runtime_init(&f->runtime, &f->app, &f->services, &f->media, &USB_OPS, f, &COSTS));
    fv_device_runtime_set_present(&f->runtime, present_work, f);
    event(f, FV_EVENT_BOOT_COMPLETED);
}
static void settings(fixture_t *f, fv_entry_method_t method, bool different) {
    if (f->app.state != FV_STATE_MODE_SELECT) event(f, FV_EVENT_BACK);
    CHECK(f->app.state == FV_STATE_MODE_SELECT);
    while (f->app.selected_mode != FV_MODE_SETTINGS) event(f, FV_EVENT_DOWN);
    event(f, FV_EVENT_SELECT);
    CHECK(f->app.state == FV_STATE_VAULT_SECRET_ENTRY && !f->msc.attached);
    enter(f, method, different);
    CHECK(f->app.state == FV_STATE_SETTINGS && f->app.session_unlocked && !f->msc.attached);
}
static void draft(fixture_t *f, fv_entry_method_t method) {
    if (f->app.selected_entry_method != method) {
        event(f, FV_EVENT_DOWN); event(f, FV_EVENT_SELECT);
        CHECK(f->app.state == FV_STATE_CHANGE_METHOD);
        while (f->app.change_entry_method != method) event(f, FV_EVENT_DOWN);
        event(f, FV_EVENT_SELECT);
    } else event(f, FV_EVENT_SELECT);
    CHECK(f->app.state == FV_STATE_CHANGE_SECRET);
    enter(f, method, true);
    CHECK(f->app.state == FV_STATE_CHANGE_CONFIRM);
    enter(f, method, true);
    CHECK(f->app.state == FV_STATE_CHANGE_REVIEW);
}
static void destroy(fixture_t *f) { fv_device_runtime_shutdown(&f->runtime); free(f); }

static void methods_and_data(void) {
    fixture_t *f = create();
    event(f, FV_EVENT_SELECT); enter(f, FV_ENTRY_METHOD_WHEELS, false);
    CHECK(f->msc.attached);
    uint8_t data[512] = "Preserve files through all entry methods", out[512];
    CHECK(fv_virtual_msc_write10(&f->msc, 5, 1, data) == FV_MSC_OK);
    fv_volume_master_key_t original = f->runtime.authentication.vmk;
    fv_fido_store_t *fido = calloc(1, sizeof(*fido)); CHECK(fido);
    CHECK(fv_fido_store_open(fido, &f->services, &f->media, &f->runtime.media_layout,
        &original, &f->runtime.encrypted.pipeline.descriptor));
    fido->image[0] = 0x42;
    CHECK(fv_fido_store_commit(fido, fido->image, FV_FIDO_STORE_BYTES));
    uint8_t fido_key[32]; memcpy(fido_key, fido->root_key, 32);
    fv_fido_store_close(fido);
    for (unsigned i = 0; i < FV_ENTRY_METHOD_COUNT + 1u; ++i) {
        fv_entry_method_t method = (fv_entry_method_t)(i % FV_ENTRY_METHOD_COUNT);
        settings(f, f->app.selected_entry_method, i != 0);
        draft(f, method);
        event(f, FV_EVENT_SELECT);
        CHECK(f->app.state == FV_STATE_CHANGE_SAVED && !f->msc.attached);
        CHECK(!f->runtime.authentication.vmk_valid && !f->app.session_unlocked);
        fv_secret_entry_t zero = {0};
        CHECK(memcmp(&f->app.change_secret_entry, &zero, sizeof(zero)) == 0);
        boot(f);
        CHECK(f->app.selected_entry_method == method);
        event(f, FV_EVENT_SELECT);
        enter(f, method, false); /* old/different secret must fail */
        CHECK(f->app.state == FV_STATE_VAULT_SECRET_ENTRY && f->app.failed_attempts == 1);
        enter(f, method, true);
        CHECK(f->msc.attached);
        CHECK(memcmp(original.bytes, f->runtime.authentication.vmk.bytes, 32) == 0);
        CHECK(fv_virtual_msc_read10(&f->msc, 5, 1, out) == FV_MSC_OK);
        CHECK(memcmp(data, out, 512) == 0);
        CHECK(fv_fido_store_open(fido, &f->services, &f->media, &f->runtime.media_layout,
            &f->runtime.authentication.vmk, &f->runtime.encrypted.pipeline.descriptor));
        CHECK(fido->image[0] == 0x42 && memcmp(fido_key, fido->root_key, 32) == 0);
        fv_fido_store_close(fido);
    }
    free(fido); destroy(f);
}

static void cancellation_and_faults(void) {
    fixture_t *f = create(); settings(f, FV_ENTRY_METHOD_WHEELS, false);
    fv_security_state_t before, after;
    CHECK(f->services.ops->load_security_state(&f->services, &before) == FV_PERSIST_OK);
    event(f, FV_EVENT_SELECT); enter(f, FV_ENTRY_METHOD_WHEELS, true);
    enter(f, FV_ENTRY_METHOD_WHEELS, false);
    CHECK(f->app.state == FV_STATE_CHANGE_MISMATCH);
    CHECK(f->services.ops->load_security_state(&f->services, &after) == FV_PERSIST_OK);
    CHECK(before.sequence == after.sequence && after.failed_attempts == 0);
    event(f, FV_EVENT_BACK);
    draft(f, FV_ENTRY_METHOD_DIRECTIONS);
    event(f, FV_EVENT_BACK);
    CHECK(f->app.state == FV_STATE_SETTINGS && f->app.selected_entry_method == FV_ENTRY_METHOD_WHEELS);
    CHECK(f->services.ops->load_security_state(&f->services, &after) == FV_PERSIST_OK);
    CHECK(before.sequence == after.sequence);
    const fv_state_t states[] = {FV_STATE_CHANGE_METHOD, FV_STATE_CHANGE_SECRET,
        FV_STATE_CHANGE_CONFIRM, FV_STATE_CHANGE_MISMATCH, FV_STATE_CHANGE_REVIEW};
    for (unsigned i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
        f->app.state = states[i]; f->app.session_unlocked = true;
        f->app.change_secret_entry.state.wheels.values[0] = 55;
        event(f, FV_EVENT_STORAGE_FAILED);
        CHECK(f->app.state == FV_STATE_FAULT && !f->runtime.authentication.vmk_valid);
        fv_secret_entry_t zero = {0};
        CHECK(memcmp(&zero, &f->app.change_secret_entry, sizeof(zero)) == 0);
    }
    boot(f); settings(f, FV_ENTRY_METHOD_WHEELS, false); draft(f, FV_ENTRY_METHOD_WHEELS);
    f->detach_fail = true; event(f, FV_EVENT_SELECT); f->detach_fail = false;
    CHECK(f->app.state == FV_STATE_FAULT);
    CHECK(f->services.ops->load_security_state(&f->services, &after) == FV_PERSIST_OK);
    CHECK(after.header_sequence == 0);
    destroy(f);
}

/* Exercise actual NOR writes at every byte boundary of both journal records,
 * and torn SD header sectors. Reboot uses production services and boot recovery. */
static void interrupted_change(bool flash_failure) {
    fixture_t *f = create(); settings(f, FV_ENTRY_METHOD_WHEELS, false);
    draft(f, FV_ENTRY_METHOD_DIRECTIONS);
    uint8_t *card = malloc(CARD_BYTES); CHECK(card); memcpy(card, f->card, CARD_BYTES);
    uint8_t flash[8192]; memcpy(flash, f->flash, sizeof(flash));
    fv_app_t app = f->app;
    fv_authentication_session_t session = f->runtime.authentication;
    for (size_t limit = 0; limit <= 512; ++limit) {
        memcpy(f->card, card, CARD_BYTES); memcpy(f->flash, flash, sizeof(flash));
        f->app = app; f->runtime.authentication = session;
        f->write_limit = SIZE_MAX; fv_nor_disable_failure(&f->nor);
        if (flash_failure) fv_nor_fail_after(&f->nor, limit);
        else f->write_limit = limit;
        event(f, FV_EVENT_SELECT);
        CHECK(f->app.state == FV_STATE_FAULT || f->app.state == FV_STATE_CHANGE_SAVED);
        f->write_limit = SIZE_MAX; fv_nor_disable_failure(&f->nor);
        boot(f);
        fv_entry_method_t committed = f->app.selected_entry_method;
        CHECK(committed == FV_ENTRY_METHOD_WHEELS || committed == FV_ENTRY_METHOD_DIRECTIONS);
        if (!flash_failure && limit < 512) CHECK(committed == FV_ENTRY_METHOD_WHEELS);
        if (flash_failure && limit < 448) CHECK(committed == FV_ENTRY_METHOD_WHEELS);
        event(f, FV_EVENT_SELECT);
        enter(f, committed, committed == FV_ENTRY_METHOD_DIRECTIONS);
        CHECK(f->msc.attached);
    }
    free(card); destroy(f);
}
static void replay_and_retry(void) {
    fixture_t *f = create(); settings(f, FV_ENTRY_METHOD_WHEELS, false);
    draft(f, FV_ENTRY_METHOD_DIRECTIONS);
    uint8_t *old = malloc(CARD_BYTES), *staged = malloc(CARD_BYTES), *good = malloc(CARD_BYTES);
    CHECK(old && staged && good); memcpy(old, f->card, CARD_BYTES);
    /* Baseline anchor persists, new header persists, final journal append fails. */
    fv_nor_fail_after(&f->nor, FV_JOURNAL_RECORD_SIZE);
    event(f, FV_EVENT_SELECT); CHECK(f->app.state == FV_STATE_FAULT);
    memcpy(staged, f->card, CARD_BYTES);
    fv_nor_disable_failure(&f->nor); boot(f);
    CHECK(f->app.selected_entry_method == FV_ENTRY_METHOD_WHEELS);
    settings(f, FV_ENTRY_METHOD_WHEELS, false);
    draft(f, FV_ENTRY_METHOD_KEYPAD); event(f, FV_EVENT_SELECT);
    CHECK(f->app.state == FV_STATE_CHANGE_SAVED);
    memcpy(good, f->card, CARD_BYTES);
    fv_vault_header_t header;
    memcpy(f->card, old, CARD_BYTES);
    CHECK(f->services.ops->load_vault_header(&f->services, &header) == FV_PERSIST_INVALID);
    memcpy(f->card, staged, CARD_BYTES);
    CHECK(f->services.ops->load_vault_header(&f->services, &header) == FV_PERSIST_INVALID);
    memcpy(f->card, good, CARD_BYTES);
    boot(f); event(f, FV_EVENT_SELECT); enter(f, FV_ENTRY_METHOD_KEYPAD, true);
    CHECK(f->msc.attached);
    fv_security_state_t state;
    CHECK(f->services.ops->load_security_state(&f->services, &state) == FV_PERSIST_OK);
    ++state.sequence; state.header_sequence = 0;
    CHECK(f->services.ops->store_security_state(&f->services, &state) == FV_PERSIST_INVALID);
    free(old); free(staged); free(good); destroy(f);
}
static void platform_failures(void) {
    for (unsigned failure = 0; failure < 4; ++failure) {
        fixture_t *f = create();
        settings(f, FV_ENTRY_METHOD_WHEELS, false);
        draft(f, FV_ENTRY_METHOD_KEYPAD);
        f->random_fail = failure == 0;
        f->sync_fail = failure == 1;
        f->read_fail = failure == 2;
        f->presentation_fail = failure == 3;
        event(f, FV_EVENT_SELECT);
        CHECK(f->presentations == 1 && f->app.state == FV_STATE_FAULT);
        CHECK(!f->app.session_unlocked && !f->runtime.authentication.vmk_valid);
        f->random_fail = f->sync_fail = f->read_fail = f->presentation_fail = false;
        boot(f);
        CHECK(f->app.selected_entry_method == FV_ENTRY_METHOD_WHEELS);
        event(f, FV_EVENT_SELECT); enter(f, FV_ENTRY_METHOD_WHEELS, false);
        CHECK(f->msc.attached);
        destroy(f);
    }
}
int main(void) {
    methods_and_data(); cancellation_and_faults(); replay_and_retry(); platform_failures();
    interrupted_change(false); interrupted_change(true);
    puts("Password UI, all methods, data/FIDO preservation, replay and power-cut tests passed.");
    return 0;
}
