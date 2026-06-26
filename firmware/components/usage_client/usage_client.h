#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t today_total;
    int32_t context_percent;
    int32_t calls_today;
    int32_t errors_today;
    int32_t sessions_today;
    int32_t cache_rate;
    int32_t active_minutes;
    int64_t current_tokens;
    int64_t last_call_tokens;
    int64_t input_total;
    int64_t output_total;
    int64_t cache_total;
    int64_t thought_total;
    int64_t week_total;
    int64_t lifetime_total;
    int64_t peak_daily_total;
    int32_t age_sec;
    int32_t streak_days;
    int32_t longest_task_minutes;
    char model[24];
    char model_1[24];
    char model_2[24];
    char model_3[24];
    int32_t model_1_pct;
    int32_t model_2_pct;
    int32_t model_3_pct;
    char updated_at[16];
    uint8_t activity_levels[26][7];
    bool valid;
} usage_report_t;

#ifdef __cplusplus
}
#endif
