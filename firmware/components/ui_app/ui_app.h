#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "usage_client.h"

void ui_app_init(void);                       // build screen (hold Lvgl_lock)
void ui_app_update(const usage_report_t *r);  // data from bridge (hold lock)
void ui_app_set_env(float temp_c, float humidity, bool ok);  // SHTC3 (hold lock)
void ui_app_set_battery(int percent, bool ok, bool charging);  // battery status (hold lock)
void ui_app_set_time(const char *hm);         // "14:30" (hold lock)
void ui_app_refresh_clock(void);              // advance cached bridge time (hold lock)
uint32_t ui_app_clock_delay_ms(void);         // next useful clock refresh delay
void ui_app_mark_stale(void);                 // bridge unreachable (hold lock)
void ui_app_toggle_activity(void);            // switch dashboard/activity (hold lock)

#ifdef __cplusplus
}
#endif
