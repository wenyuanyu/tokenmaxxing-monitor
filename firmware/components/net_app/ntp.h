#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

// Start SNTP (call after WiFi is up). TZ defaults to CST-8 (UTC+8).
void ntp_start(void);
// Write current local time "HH:MM" into buf (>=6 bytes). Returns false if
// time not yet synced (shows "--:--").
bool ntp_now_hm(char *buf, int n);

#ifdef __cplusplus
}
#endif
