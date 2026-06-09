#include "ui_app.h"

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

#ifndef RLCD_GREETING_NAME
#define RLCD_GREETING_NAME ""
#endif

LV_FONT_DECLARE(font_amt14);
LV_FONT_DECLARE(font_zh18);

#define INK   lv_color_black()
#define WHITE lv_color_white()

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
static lv_obj_t *bar_goal;
static lv_obj_t *lbl_goal;

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
    if (h >= 5 && h < 12)  period = "\xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd\xef\xbd\x9e";  /* 早上好～ */
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
    label(s, 12, 46, &lv_font_montserrat_36, "QWEN CODE");

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
    label(s, 12, 264, &lv_font_montserrat_12, "QwenLM/qwen-code");

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

    label(s, 282, 234, &lv_font_montserrat_12, "Last 7 Days");
    lbl_week = label(s, 282, 248, &lv_font_montserrat_16, "--");
}

void ui_app_update(const usage_report_t *r)
{
    if (!r || !r->valid) return;

    char b[48];
    char n[32];

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

    /* header: greeting + time */
    if (r->updated_at[0]) {
        lv_label_set_text(lbl_time, r->updated_at);
        lv_label_set_text(lbl_greeting, greeting_for_stamp(r->updated_at));
    }

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
}

void ui_app_set_time(const char *hm)
{
    if (hm && hm[0]) lv_label_set_text(lbl_time, hm);
}

void ui_app_mark_stale(void)
{
    lv_label_set_text(lbl_active, "stale");
}
