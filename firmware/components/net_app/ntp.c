#include "ntp.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_sntp.h"

void ntp_start(void)
{
    setenv("TZ", "CST-8", 1);   // UTC+8, no DST
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    esp_sntp_init();
}

bool ntp_now_hm(char *buf, int n)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < (2024 - 1900)) {   // not synced yet
        snprintf(buf, n, "--:--");
        return false;
    }
    snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return true;
}
