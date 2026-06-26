#include "lvgl.h"
#include "ui_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static constexpr int kWidth = 400;
static constexpr int kHeight = 300;

static std::vector<uint8_t> g_frame(kWidth * kHeight * 3, 0xFF);

static void rgb565_to_rgb888(uint16_t v, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
    g = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
    b = (uint8_t)((v & 0x1F) * 255 / 31);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    const uint16_t *src = reinterpret_cast<const uint16_t *>(px_map);

    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(src[(y - area->y1) * w + (x - area->x1)], r, g, b);
            size_t off = (y * kWidth + x) * 3;
            g_frame[off + 0] = r;
            g_frame[off + 1] = g;
            g_frame[off + 2] = b;
        }
    }

    lv_display_flush_ready(disp);
}

static void save_ppm(const char *path)
{
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        std::exit(1);
    }
    std::fprintf(f, "P6\n%d %d\n255\n", kWidth, kHeight);
    std::fwrite(g_frame.data(), 1, g_frame.size(), f);
    std::fclose(f);
}

static usage_report_t dummy_report()
{
    usage_report_t r{};
    r.today_total = 7842318;
    r.context_percent = 63;
    r.calls_today = 42;
    r.errors_today = 0;
    r.sessions_today = 18;
    r.cache_rate = 41;
    r.active_minutes = 342;
    r.current_tokens = 256000;
    r.last_call_tokens = 9200;
    r.input_total = 5820000;
    r.output_total = 2020000;
    r.cache_total = 2386200;
    r.thought_total = 0;
    r.week_total = 28400000;
    r.lifetime_total = 183400000;
    r.peak_daily_total = 48300000;
    r.age_sec = 1;
    r.streak_days = 4;
    r.longest_task_minutes = 36;
    std::snprintf(r.model, sizeof(r.model), "codex-5");
    std::snprintf(r.model_1, sizeof(r.model_1), "codex-5");
    std::snprintf(r.model_2, sizeof(r.model_2), "qwen3-coder-plus");
    std::snprintf(r.model_3, sizeof(r.model_3), "claude-sonnet-4");
    r.model_1_pct = 62;
    r.model_2_pct = 25;
    r.model_3_pct = 13;
    std::snprintf(r.updated_at, sizeof(r.updated_at), "06-26 22:48");
    const uint32_t recent_tokens[5][7] = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 420000, 0, 0, 0, 0},
        {0, 0, 5800000, 0, 750000, 0, 0},
        {900000, 36000000, 0, 7200000, 8800000, 280000, 0},
        {6400000, 135000000, 108000000, 124000000, 5200000, 0, 0},
    };
    auto level_for_tokens = [](uint32_t tokens) {
        if (tokens == 0) return 0;
        if (tokens < 1000000U) return 1;
        if (tokens < 10000000U) return 2;
        if (tokens < 100000000U) return 3;
        return 4;
    };
    for (int col = 0; col < 26; col++) {
        for (int row = 0; row < 7; row++) {
            uint32_t tokens = 0;
            if (col > 20) tokens = recent_tokens[col - 21][row];
            else if ((col == 2 && row == 6) || (col == 3 && row == 0) ||
                     (col == 6 && row == 3) || (col == 12 && row == 2) ||
                     (col == 17 && row == 4) || (col == 19 && row == 1)) {
                tokens = 360000;
            }
            r.activity_levels[col][row] = level_for_tokens(tokens);
        }
    }
    r.valid = true;
    return r;
}

static void render(lv_display_t *disp)
{
    std::fill(g_frame.begin(), g_frame.end(), 0xFF);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
}

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : ".";

    lv_init();

    static uint16_t draw_buf[kWidth * kHeight];
    lv_display_t *disp = lv_display_create(kWidth, kHeight);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_FULL);

    ui_app_init();
    usage_report_t report = dummy_report();
    ui_app_update(&report);
    ui_app_set_env(25.8f, 54.0f, true);
    ui_app_set_battery(95, true, false);

    render(disp);
    std::string dashboard = std::string(out_dir) + "/dashboard.ppm";
    save_ppm(dashboard.c_str());

    ui_app_toggle_activity();
    render(disp);
    std::string activity = std::string(out_dir) + "/activity.ppm";
    save_ppm(activity.c_str());

    std::printf("wrote %s and %s\n", dashboard.c_str(), activity.c_str());
    return 0;
}
