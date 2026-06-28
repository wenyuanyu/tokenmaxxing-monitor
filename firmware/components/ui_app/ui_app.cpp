#include "ui_app.h"

#include "esp_timer.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RLCD_GREETING_NAME
#define RLCD_GREETING_NAME ""
#endif

#ifndef RLCD_TZ_OFFSET_MINUTES
#define RLCD_TZ_OFFSET_MINUTES 480
#endif

LV_FONT_DECLARE(font_amt14);
LV_FONT_DECLARE(font_zh18);

#define INK   lv_color_black()
#define WHITE lv_color_white()
#define GRAY1 lv_color_hex(0xC0C0C0)
#define GRAY2 lv_color_hex(0x888888)
#define GRAY3 lv_color_hex(0x505050)

static lv_obj_t *lbl_greeting;
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_env;
static lv_obj_t *lbl_today;
static lv_obj_t *lbl_today_unit;
static lv_obj_t *lbl_model_1;
static lv_obj_t *lbl_model_2;
static lv_obj_t *lbl_model_3;
static lv_obj_t *lbl_model_1_pct;
static lv_obj_t *lbl_model_2_pct;
static lv_obj_t *lbl_model_3_pct;
static lv_obj_t *lbl_sessions;
static lv_obj_t *lbl_active;
static lv_obj_t *lbl_input;
static lv_obj_t *lbl_output;
static lv_obj_t *lbl_rate;
static lv_obj_t *lbl_week;
static lv_obj_t *lbl_battery_pct;
static lv_obj_t *battery_bolt;
static lv_obj_t *bar_goal;
static lv_obj_t *lbl_goal;
static lv_obj_t *page_activity;
static lv_obj_t *lbl_activity_greeting;
static lv_obj_t *lbl_activity_time;
static lv_obj_t *lbl_activity_env;
static lv_obj_t *lbl_activity_lifetime;
static lv_obj_t *lbl_activity_peak;
static lv_obj_t *lbl_activity_streak;
static lv_obj_t *lbl_activity_longest_task;
static lv_obj_t *lbl_activity_battery_pct;
static lv_obj_t *activity_battery_bolt;
static lv_obj_t *activity_month_labels[8];
static lv_obj_t *activity_cells[26][7];
static bool s_activity_visible;
static bool s_clock_valid;
static int s_clock_year;
static int s_clock_month;
static int s_clock_day;
static int s_clock_hour;
static int s_clock_min;
static int s_clock_sec;
static int64_t s_clock_anchor_sec;
static char s_clock_rendered[16];

#define GOAL_TARGET 100000000LL  /* 一个亿 */

static void fmt_tok(char *o, size_t n, int64_t t)
{
    if (t < 0) t = 0;
    char raw[24];
    snprintf(raw, sizeof(raw), "%lld", (long long)t);
    int len = (int)strlen(raw);
    int commas = (len - 1) / 3;
    int total = len + commas;
    if (total >= (int)n) { snprintf(o, n, "%lld", (long long)t); return; }
    o[total] = '\0';
    int si = len - 1, di = total - 1, g = 0;
    while (si >= 0) {
        o[di--] = raw[si--];
        if (++g == 3 && si >= 0) { o[di--] = ','; g = 0; }
    }
}

static void fmt_tok_short(char *o, size_t n, int64_t t)
{
    if      (t >= 1000000000LL) snprintf(o, n, "%.1fB", t / 1e9);
    else if (t >= 10000000LL)   snprintf(o, n, "%.0fM", t / 1e6);
    else if (t >= 1000000LL)    snprintf(o, n, "%.1fM", t / 1e6);
    else if (t >= 1000LL)       snprintf(o, n, "%.0fk", t / 1e3);
    else                        snprintf(o, n, "%lld", (long long)t);
}

static void fmt_tok_m(char *o, size_t n, int64_t t)
{
    if      (t >= 1000000000LL) snprintf(o, n, "%.1fB", t / 1e9);
    else if (t >= 100000000LL)  snprintf(o, n, "%.0fM", t / 1e6);
    else if (t >= 1000000LL)    snprintf(o, n, "%.1fM", t / 1e6);
    else if (t >= 100000LL)     snprintf(o, n, "%.2fM", t / 1e6);
    else                        snprintf(o, n, "%.3fM", t / 1e6);
}

static int hour_from_stamp(const char *s)
{
    const char *p = s ? strchr(s, ' ') : NULL;
    if (!p || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9') {
        return 8;
    }
    return (p[1] - '0') * 10 + (p[2] - '0');
}

static char s_greeting[64];

static const char *greeting_for_stamp(const char *s)
{
    int h = hour_from_stamp(s);
    const char *period;
    if (h < 5)              period = "\xe5\x87\x8c\xe6\x99\xa8\xe5\xa5\xbd\xef\xbd\x9e";  /* 凌晨好～ */
    else if (h < 12)        period = "\xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd\xef\xbd\x9e";  /* 早上好～ */
    else if (h < 18)        period = "\xe4\xb8\x8b\xe5\x8d\x88\xe5\xa5\xbd\xef\xbd\x9e";  /* 下午好～ */
    else                    period = "\xe6\x99\x9a\xe4\xb8\x8a\xe5\xa5\xbd\xef\xbd\x9e";  /* 晚上好～ */
    const char *name = RLCD_GREETING_NAME;
    if (name[0]) {
        snprintf(s_greeting, sizeof(s_greeting), "%s\xef\xbc\x8c%s", name, period); /* name，period */
    } else {
        snprintf(s_greeting, sizeof(s_greeting), "%s", period);
    }
    return s_greeting;
}

static int64_t uptime_sec(void)
{
    return esp_timer_get_time() / 1000000LL;
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 31;
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

static int64_t floor_div_i64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) q--;
    return q;
}

static int positive_mod_i64(int64_t a, int b)
{
    int r = (int)(a % b);
    return r < 0 ? r + b : r;
}

static void civil_from_days(int64_t z, int *year, int *month, int *day)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

static bool parse_unix_stamp(const char *s, int offset_minutes,
                             int *year, int *month, int *day,
                             int *hour, int *min, int *sec)
{
    if (!s || !s[0]) return false;
    char *end = NULL;
    int64_t utc_sec = strtoll(s, &end, 10);
    if (end == s || *end != '\0' || utc_sec <= 0) return false;

    int64_t local_sec = utc_sec + (int64_t)offset_minutes * 60;
    int64_t days = floor_div_i64(local_sec, 86400);
    int sod = positive_mod_i64(local_sec, 86400);
    civil_from_days(days, year, month, day);
    *hour = sod / 3600;
    *min = (sod % 3600) / 60;
    *sec = sod % 60;
    return true;
}

static bool parse_bridge_stamp(const char *s, int *month, int *day,
                               int *hour, int *min, int *sec)
{
    int mo = 0, da = 0, hh = 0, mm = 0, ss = 0;
    if (!s || sscanf(s, "%2d-%2d %2d:%2d:%2d", &mo, &da, &hh, &mm, &ss) != 5) {
        return false;
    }
    if (mo < 1 || mo > 12 || da < 1 || da > days_in_month(2026, mo) ||
        hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        return false;
    }
    *month = mo;
    *day = da;
    *hour = hh;
    *min = mm;
    *sec = ss;
    return true;
}

static void advance_local_day(int *year, int *month, int *day)
{
    (*day)++;
    if (*day <= days_in_month(*year, *month)) return;
    *day = 1;
    (*month)++;
    if (*month > 12) {
        *month = 1;
        (*year)++;
    }
}

static void retreat_local_day(int *year, int *month, int *day)
{
    (*day)--;
    if (*day >= 1) return;
    (*month)--;
    if (*month < 1) {
        *month = 12;
        (*year)--;
    }
    *day = days_in_month(*year, *month);
}

static void add_local_days(int *year, int *month, int *day, int delta)
{
    while (delta > 0) {
        advance_local_day(year, month, day);
        delta--;
    }
    while (delta < 0) {
        retreat_local_day(year, month, day);
        delta++;
    }
}

static int day_of_week(int year, int month, int day)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

static const char *month_name(int month)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (month < 1 || month > 12) return "";
    return names[month - 1];
}

static void format_clock_stamp(char *out, size_t out_size,
                               int month, int day, int hour, int min, int sec)
{
    (void)sec;
    snprintf(out, out_size, "%02d-%02d %02d:%02d",
             month, day, hour, min);
}

static void sync_clock_from_stamp(const char *stamp, int offset_minutes)
{
    int yr = 0, mo = 0, da = 0, hh = 0, mm = 0, ss = 0;
    if (!parse_unix_stamp(stamp, offset_minutes, &yr, &mo, &da, &hh, &mm, &ss)) {
        yr = 2026;
        if (!parse_bridge_stamp(stamp, &mo, &da, &hh, &mm, &ss)) return;
    }
    s_clock_year = yr;
    s_clock_month = mo;
    s_clock_day = da;
    s_clock_hour = hh;
    s_clock_min = mm;
    s_clock_sec = ss;
    s_clock_anchor_sec = uptime_sec();
    s_clock_valid = true;
    s_clock_rendered[0] = '\0';
}

static void apply_clock_labels(const char *stamp)
{
    if (!stamp || !stamp[0] || strcmp(s_clock_rendered, stamp) == 0) return;
    strncpy(s_clock_rendered, stamp, sizeof(s_clock_rendered) - 1);
    s_clock_rendered[sizeof(s_clock_rendered) - 1] = '\0';

    lv_label_set_text(lbl_time, s_clock_rendered);
    lv_label_set_text(lbl_greeting, greeting_for_stamp(s_clock_rendered));
    if (lbl_activity_time) lv_label_set_text(lbl_activity_time, s_clock_rendered);
    if (lbl_activity_greeting) {
        lv_label_set_text(lbl_activity_greeting, greeting_for_stamp(s_clock_rendered));
    }
}

static void fmt_minutes(char *o, size_t n, int32_t minutes)
{
    if (minutes < 60) {
        snprintf(o, n, "%dm", (int)minutes);
    } else if (minutes < 6000) {
        snprintf(o, n, "%dh%02d", (int)(minutes / 60), (int)(minutes % 60));
    } else {
        snprintf(o, n, "%dh", (int)(minutes / 60));
    }
}

static lv_obj_t *label(lv_obj_t *p, int x, int y, const lv_font_t *font,
                       const char *text)
{
    lv_obj_t *obj = lv_label_create(p);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, INK, 0);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *surface(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(p);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, WHITE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *aligned(lv_obj_t *p, int x, int y, int w,
                         lv_text_align_t align, const lv_font_t *font,
                         const char *text)
{
    lv_obj_t *obj = label(p, x, y, font, text);
    lv_obj_set_width(obj, w);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}

static void divider(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(p);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, INK, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, bool fill)
{
    lv_obj_t *obj = lv_obj_create(p);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_border_color(obj, INK, 0);
    lv_obj_set_style_border_width(obj, fill ? 0 : 2, 0);
    lv_obj_set_style_bg_color(obj, fill ? INK : WHITE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_color_t activity_color_for_level(int level)
{
    static const lv_color_t colors[] = {
        WHITE,
        lv_color_make(0x11, 0x00, 0x11),
        lv_color_make(0x22, 0x00, 0x22),
        lv_color_make(0x33, 0x00, 0x33),
        lv_color_make(0x44, 0x00, 0x44),
    };
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    return colors[level];
}

static void set_heat_cell_level(int col, int row, int level)
{
    if (col < 0 || col >= 26 || row < 0 || row >= 7) return;
    if (!activity_cells[col][row]) return;
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    lv_obj_set_style_bg_color(activity_cells[col][row], activity_color_for_level(level), 0);
}

static int activity_col_x(int col)
{
    return 52 + col * 12;
}

static void heat_cell(lv_obj_t *p, int col, int row, int level)
{
    lv_obj_t *obj = lv_obj_create(p);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, activity_col_x(col), 124 + row * 12);
    lv_obj_set_size(obj, 10, 10);
    lv_obj_set_style_bg_color(obj, activity_color_for_level(level), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, GRAY3, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    activity_cells[col][row] = obj;
}

static lv_obj_t *bolt(lv_obj_t *p, int x, int y)
{
    static const lv_point_precise_t pts[] = {
        { 8, 0 }, { 2, 10 }, { 8, 10 }, { 4, 20 },
    };
    lv_obj_t *obj = lv_line_create(p);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, 12, 22);
    lv_line_set_points(obj, pts, sizeof(pts) / sizeof(pts[0]));
    lv_obj_set_style_line_color(obj, INK, 0);
    lv_obj_set_style_line_width(obj, 2, 0);
    lv_obj_set_style_line_rounded(obj, false, 0);
    return obj;
}

static void hide_activity_months(void)
{
    const int label_count = (int)(sizeof(activity_month_labels) / sizeof(activity_month_labels[0]));
    for (int i = 0; i < label_count; i++) {
        if (activity_month_labels[i]) lv_obj_add_flag(activity_month_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_activity_months_for_date(int year, int month, int day)
{
    hide_activity_months();
    const int label_count = (int)(sizeof(activity_month_labels) / sizeof(activity_month_labels[0]));
    int start_year = year;
    int start_month = month;
    int start_day = day;
    add_local_days(&start_year, &start_month, &start_day,
                   -day_of_week(year, month, day) - 25 * 7);

    int previous_month = 0;
    int label_idx = 0;
    for (int col = 0; col < 26 && label_idx < label_count; col++) {
        int week_year = start_year;
        int week_month = start_month;
        int week_day = start_day;
        add_local_days(&week_year, &week_month, &week_day, col * 7);

        int label_month = (col == 0) ? week_month : 0;
        for (int row = 0; row < 7; row++) {
            int cell_year = week_year;
            int cell_month = week_month;
            int cell_day = week_day;
            add_local_days(&cell_year, &cell_month, &cell_day, row);
            if (cell_day == 1) {
                label_month = cell_month;
                break;
            }
        }

        if (label_month > 0 && label_month != previous_month) {
            lv_obj_t *lbl = activity_month_labels[label_idx++];
            lv_obj_set_x(lbl, activity_col_x(col) - 6);
            lv_label_set_text(lbl, month_name(label_month));
            lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            previous_month = label_month;
        }
    }
}

static int activity_level_for_tokens(uint64_t tokens)
{
    if (tokens == 0) return 0;
    if (tokens < 1000000ULL) return 1;
    if (tokens < 10000000ULL) return 2;
    if (tokens < 100000000ULL) return 3;
    return 4;
}

static int activity_level_for_cell(int col, int row)
{
    if (col > 20) {
        static const uint32_t recent_tokens[5][7] = {
            {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 420000, 0, 0, 0, 0},
            {0, 0, 5800000, 0, 750000, 0, 0},
            {900000, 36000000, 0, 7200000, 8800000, 280000, 0},
            {6400000, 135000000, 108000000, 124000000, 5200000, 0, 0},
        };
        return activity_level_for_tokens(recent_tokens[col - 21][row]);
    }
    if ((col == 2 && row == 6) || (col == 3 && row == 0) || (col == 6 && row == 3)) {
        return activity_level_for_tokens(360000);
    }
    if ((col == 12 && row == 2) || (col == 17 && row == 4) || (col == 19 && row == 1)) {
        return activity_level_for_tokens(780000);
    }
    return activity_level_for_tokens(0);
}

static void create_activity_page(lv_obj_t *s)
{
    page_activity = surface(s, 0, 0, 400, 300);
    lv_obj_add_flag(page_activity, LV_OBJ_FLAG_HIDDEN);

    lbl_activity_greeting = aligned(page_activity, 12, 8, 150, LV_TEXT_ALIGN_LEFT,
                                    &font_zh18, greeting_for_stamp(NULL));
    lbl_activity_time = aligned(page_activity, 162, 10, 100, LV_TEXT_ALIGN_CENTER,
                                &lv_font_montserrat_14, "-- --:--");
    lbl_activity_env = aligned(page_activity, 272, 10, 118, LV_TEXT_ALIGN_RIGHT,
                               &lv_font_montserrat_14, "--.-\xC2\xB0""C --%");
    divider(page_activity, 10, 34, 380, 2);

    label(page_activity, 12, 49, &lv_font_montserrat_16, "Token activity");
    label(page_activity, 165, 51, &lv_font_montserrat_14, "last 6 months");
    label(page_activity, 12, 74, &lv_font_montserrat_14, "Lifetime");
    lbl_activity_lifetime = label(page_activity, 86, 74, &font_amt14, "--");
    label(page_activity, 158, 74, &lv_font_montserrat_14, "Peak");
    lbl_activity_peak = label(page_activity, 210, 74, &font_amt14, "--");
    label(page_activity, 284, 74, &lv_font_montserrat_14, "Streak");
    lbl_activity_streak = label(page_activity, 354, 74, &font_amt14, "--");
    lbl_activity_longest_task = label(page_activity, 12, 94, &lv_font_montserrat_14,
                                      "Longest task --");

    for (int i = 0; i < (int)(sizeof(activity_month_labels) / sizeof(activity_month_labels[0])); i++) {
        activity_month_labels[i] = label(page_activity, 46, 110, &lv_font_montserrat_12, "");
        lv_obj_add_flag(activity_month_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    static const char *days[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    for (int row = 0; row < 7; row++) {
        aligned(page_activity, 12, 123 + row * 12, 28, LV_TEXT_ALIGN_RIGHT,
                &lv_font_montserrat_12, days[row]);
    }

    for (int col = 0; col < 26; col++) {
        for (int row = 0; row < 7; row++) {
            heat_cell(page_activity, col, row, activity_level_for_cell(col, row));
        }
    }

    rect(page_activity, 282, 270, 66, 22, false);
    rect(page_activity, 350, 276, 4, 10, true);
    lbl_activity_battery_pct = aligned(page_activity, 286, 274, 58, LV_TEXT_ALIGN_CENTER,
                                       &lv_font_montserrat_14, "--%");
    activity_battery_bolt = bolt(page_activity, 364, 270);
    lv_obj_add_flag(activity_battery_bolt, LV_OBJ_FLAG_HIDDEN);
}

void ui_app_init(void)
{
    lv_obj_t *s = lv_screen_active();
    lv_obj_set_style_bg_color(s, WHITE, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);

    /* ── header: greeting | time | env ── */
    lbl_greeting = aligned(s, 12, 8, 150, LV_TEXT_ALIGN_LEFT,
                           &font_zh18, greeting_for_stamp(NULL));
    lbl_time = aligned(s, 162, 10, 100, LV_TEXT_ALIGN_CENTER,
                       &lv_font_montserrat_14, "-- --:--");
    lbl_env = aligned(s, 272, 10, 118, LV_TEXT_ALIGN_RIGHT,
                      &lv_font_montserrat_14, "--.-\xC2\xB0""C --%");
    divider(s, 10, 34, 380, 2);

    /* ── left column (x 12..254) ── */
    label(s, 12, 48, &lv_font_montserrat_28, "TokenMaxxing");

    label(s, 12, 94, &lv_font_montserrat_14, "DAILY SMALL GOAL: 100M");
    lbl_today = label(s, 12, 112, &lv_font_montserrat_28, "--");
    lbl_today_unit = label(s, 12, 126, &lv_font_montserrat_14, "Tokens");

    /* ── progress bar (width 200, aligned with left column) ── */
    bar_goal = lv_bar_create(s);
    lv_obj_set_pos(bar_goal, 12, 146);
    lv_obj_set_size(bar_goal, 200, 14);
    lv_bar_set_range(bar_goal, 0, 1000);
    lv_bar_set_value(bar_goal, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_goal, WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_goal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar_goal, INK, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_goal, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_goal, INK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_goal, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_goal, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_goal, 3, LV_PART_INDICATOR);
    lbl_goal = label(s, 218, 146, &lv_font_montserrat_14, "0.0%");

    label(s, 12, 170, &lv_font_montserrat_14, "Today Top3 Models");
    lbl_model_1 = aligned(s, 12, 190, 190, LV_TEXT_ALIGN_LEFT,
                          &lv_font_montserrat_14, "--");
    lbl_model_1_pct = aligned(s, 206, 190, 48, LV_TEXT_ALIGN_RIGHT,
                              &font_amt14, "--");
    lbl_model_2 = aligned(s, 12, 210, 190, LV_TEXT_ALIGN_LEFT,
                          &lv_font_montserrat_14, "--");
    lbl_model_2_pct = aligned(s, 206, 210, 48, LV_TEXT_ALIGN_RIGHT,
                              &font_amt14, "--");
    lbl_model_3 = aligned(s, 12, 230, 190, LV_TEXT_ALIGN_LEFT,
                          &lv_font_montserrat_14, "--");
    lbl_model_3_pct = aligned(s, 206, 230, 48, LV_TEXT_ALIGN_RIGHT,
                              &font_amt14, "--");

    divider(s, 12, 256, 244, 1);
    label(s, 12, 264, &lv_font_montserrat_12, "QwenCode/Codex/Claude/Qoder/Claw");

    /* ── vertical divider ── */
    divider(s, 268, 42, 2, 234);

    /* ── right column (x 282..388), vertical stack ── */
    label(s, 282, 52, &lv_font_montserrat_12, "Today Sessions");
    lbl_sessions = label(s, 282, 66, &lv_font_montserrat_16, "--");

    label(s, 282, 82, &lv_font_montserrat_12, "Active Time");
    lbl_active = label(s, 282, 96, &lv_font_montserrat_16, "--");

    label(s, 282, 118, &lv_font_montserrat_12, "Input Tokens");
    lbl_input = label(s, 282, 132, &lv_font_montserrat_16, "--");

    label(s, 282, 154, &lv_font_montserrat_12, "Output Tokens");
    lbl_output = label(s, 282, 168, &lv_font_montserrat_16, "--");

    label(s, 282, 190, &lv_font_montserrat_12, "Cache Rate");
    lbl_rate = label(s, 282, 204, &lv_font_montserrat_16, "--");

    /* ── horizontal divider ── */
    divider(s, 282, 226, 100, 1);

    label(s, 282, 232, &lv_font_montserrat_12, "Last 7 Days");
    lbl_week = label(s, 282, 246, &lv_font_montserrat_16, "--");

    rect(s, 282, 270, 66, 22, false);
    rect(s, 350, 276, 4, 10, true);
    lbl_battery_pct = aligned(s, 286, 274, 58, LV_TEXT_ALIGN_CENTER,
                              &lv_font_montserrat_14, "--%");
    battery_bolt = bolt(s, 364, 270);
    lv_obj_add_flag(battery_bolt, LV_OBJ_FLAG_HIDDEN);

    create_activity_page(s);
}

void ui_app_update(const usage_report_t *r)
{
    if (!r || !r->valid) return;

    char b[48];
    char n[32];

    /* header clock is volatile; heartbeats can update it without repainting data. */
    if (r->updated_at[0]) {
        sync_clock_from_stamp(r->updated_at, r->timezone_offset_minutes);
        ui_app_refresh_clock();
    }

    static bool have_rendered_report;
    static usage_report_t last_rendered_report;
    usage_report_t comparable_report = *r;
    memset(comparable_report.updated_at, 0, sizeof(comparable_report.updated_at));
    comparable_report.timezone_offset_minutes = 0;
    comparable_report.age_sec = 0;
    if (have_rendered_report &&
        memcmp(&last_rendered_report, &comparable_report, sizeof(comparable_report)) == 0) {
        return;
    }
    last_rendered_report = comparable_report;
    have_rendered_report = true;

    /* today token — number + unit */
    fmt_tok(n, sizeof(n), r->today_total);
    lv_label_set_text(lbl_today, n);
    lv_obj_update_layout(lbl_today);
    lv_obj_set_x(lbl_today_unit, lv_obj_get_x(lbl_today) + lv_obj_get_width(lbl_today) + 6);

    /* 一个亿小目标 progress */
    int64_t clamped = r->today_total > GOAL_TARGET ? GOAL_TARGET : r->today_total;
    int32_t permille = (int32_t)(clamped * 1000 / GOAL_TARGET);
    lv_bar_set_value(bar_goal, permille, LV_ANIM_OFF);
    double pct = r->today_total * 100.0 / GOAL_TARGET;
    if (pct > 100.0) pct = 100.0;
    snprintf(b, sizeof(b), "%.1f%%", pct);
    lv_label_set_text(lbl_goal, b);

    /* top 3 models */
    lv_label_set_text(lbl_model_1, r->model_1[0] ? r->model_1 : "--");
    lv_label_set_text(lbl_model_2, r->model_2[0] ? r->model_2 : "--");
    lv_label_set_text(lbl_model_3, r->model_3[0] ? r->model_3 : "--");
    snprintf(b, sizeof(b), "%d%%", (int)r->model_1_pct);
    lv_label_set_text(lbl_model_1_pct, b);
    snprintf(b, sizeof(b), "%d%%", (int)r->model_2_pct);
    lv_label_set_text(lbl_model_2_pct, b);
    snprintf(b, sizeof(b), "%d%%", (int)r->model_3_pct);
    lv_label_set_text(lbl_model_3_pct, b);

    /* right column */
    snprintf(b, sizeof(b), "%d", (int)r->sessions_today);
    lv_label_set_text(lbl_sessions, b);

    fmt_minutes(b, sizeof(b), r->active_minutes);
    lv_label_set_text(lbl_active, b);

    fmt_tok_m(n, sizeof(n), r->input_total);
    lv_label_set_text(lbl_input, n);

    fmt_tok_m(n, sizeof(n), r->output_total);
    lv_label_set_text(lbl_output, n);

    snprintf(b, sizeof(b), "%d%%", (int)r->cache_rate);
    lv_label_set_text(lbl_rate, b);

    fmt_tok_m(n, sizeof(n), r->week_total);
    lv_label_set_text(lbl_week, n);

    if (lbl_activity_lifetime) {
        fmt_tok_short(n, sizeof(n), r->lifetime_total);
        lv_label_set_text(lbl_activity_lifetime, n);
    }
    if (lbl_activity_peak) {
        fmt_tok_short(n, sizeof(n), r->peak_daily_total);
        lv_label_set_text(lbl_activity_peak, n);
    }
    if (lbl_activity_streak) {
        snprintf(b, sizeof(b), "%dd", (int)r->streak_days);
        lv_label_set_text(lbl_activity_streak, b);
    }
    if (lbl_activity_longest_task) {
        fmt_minutes(n, sizeof(n), r->longest_task_minutes);
        snprintf(b, sizeof(b), "Longest task %s", n);
        lv_label_set_text(lbl_activity_longest_task, b);
    }
    for (int col = 0; col < 26; col++) {
        for (int row = 0; row < 7; row++) {
            set_heat_cell_level(col, row, r->activity_levels[col][row]);
        }
    }
}

void ui_app_set_env(float temp_c, float humidity, bool ok)
{
    char b[40];
    if (ok) {
        snprintf(b, sizeof(b), "env %.1f\xC2\xB0""C %.0f%%", temp_c, humidity);
    } else {
        snprintf(b, sizeof(b), "env --.-C --%%");
    }
    lv_label_set_text(lbl_env, b);
    if (lbl_activity_env) lv_label_set_text(lbl_activity_env, b);
}

void ui_app_set_battery(int percent, bool ok, bool charging)
{
    char b[8];
    if (ok) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        snprintf(b, sizeof(b), "%d%%", percent);
    } else {
        snprintf(b, sizeof(b), "--%%");
    }
    lv_label_set_text(lbl_battery_pct, b);
    if (lbl_activity_battery_pct) lv_label_set_text(lbl_activity_battery_pct, b);

    if (charging) {
        lv_obj_remove_flag(battery_bolt, LV_OBJ_FLAG_HIDDEN);
        if (activity_battery_bolt) {
            lv_obj_remove_flag(activity_battery_bolt, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(battery_bolt, LV_OBJ_FLAG_HIDDEN);
        if (activity_battery_bolt) {
            lv_obj_add_flag(activity_battery_bolt, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_app_set_time(const char *hm)
{
    if (hm && hm[0]) lv_label_set_text(lbl_time, hm);
    if (hm && hm[0] && lbl_activity_time) lv_label_set_text(lbl_activity_time, hm);
}

void ui_app_refresh_clock(void)
{
    if (!s_clock_valid) return;

    int64_t elapsed = uptime_sec() - s_clock_anchor_sec;
    if (elapsed < 0) elapsed = 0;

    int year = s_clock_year;
    int month = s_clock_month;
    int day = s_clock_day;
    int hour = s_clock_hour;
    int min = s_clock_min;
    int sec = s_clock_sec + (int)(elapsed % 60);
    int carry_min = (int)(elapsed / 60);

    if (sec >= 60) {
        sec -= 60;
        carry_min++;
    }
    min += carry_min % 60;
    int carry_hour = carry_min / 60;
    if (min >= 60) {
        min -= 60;
        carry_hour++;
    }
    hour += carry_hour % 24;
    int carry_day = carry_hour / 24;
    if (hour >= 24) {
        hour -= 24;
        carry_day++;
    }

    while (carry_day-- > 0) {
        advance_local_day(&year, &month, &day);
    }

    char stamp[16];
    format_clock_stamp(stamp, sizeof(stamp), month, day, hour, min, sec);
    apply_activity_months_for_date(year, month, day);
    apply_clock_labels(stamp);
}

uint32_t ui_app_clock_delay_ms(void)
{
    if (!s_clock_valid) return 60000;

    int64_t elapsed = uptime_sec() - s_clock_anchor_sec;
    if (elapsed < 0) elapsed = 0;

    int sec = (s_clock_sec + (int)(elapsed % 60)) % 60;
    uint32_t delay = (uint32_t)(60 - sec) * 1000;
    if (delay < 1000) delay = 1000;
    return delay;
}

void ui_app_mark_stale(void)
{
    lv_label_set_text(lbl_active, "stale");
}

void ui_app_toggle_activity(void)
{
    if (!page_activity) return;
    s_activity_visible = !s_activity_visible;
    if (s_activity_visible) {
        lv_obj_remove_flag(page_activity, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(page_activity);
    } else {
        lv_obj_add_flag(page_activity, LV_OBJ_FLAG_HIDDEN);
    }
}
