#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "usage_client.h"

void ui_app_init(void);                       // build screen (hold Lvgl_lock)
void ui_app_update(const usage_report_t *r);  // data from bridge (hold lock)
void ui_app_set_env(float temp_c, float humidity, bool ok);  // SHTC3 (hold lock)
void ui_app_set_time(const char *hm);         // "14:30" (hold lock)
void ui_app_mark_stale(void);                 // bridge unreachable (hold lock)

#ifdef __cplusplus
}
#endif
