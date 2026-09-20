#ifndef FV_DEVICE_UI_ADAPTER_H
#define FV_DEVICE_UI_ADAPTER_H
#include "device_ui.h"
#include <stdatomic.h>
extern atomic_bool fv_ui_maintenance;
void fv_device_ui_init(void);
void fv_device_ui_format_progress(void *,uint64_t,uint64_t);
void fv_device_ui_poll(void);
void fv_device_ui_refresh(void);
void fv_device_ui_media_changed(bool unlocked);
void fv_device_ui_disconnect(void);
bool fv_device_ui_command(const char *,char *,size_t);
void fv_device_ui_execute(const fv_ui_job *,fv_ui_result *);
#endif
