#include "ble_app.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_app";
static const char *DEVICE_NAME = "QwenToken";

static ble_data_cb_t s_data_cb;
static uint8_t s_own_addr_type;
static char s_last_payload[256] = "3|0|0|0|0|0|-- --:--|--|0|--|0|--|0|0|0";

static const ble_uuid128_t SVC_UUID =
    BLE_UUID128_INIT(0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
                     0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00);
static const ble_uuid128_t DATA_UUID =
    BLE_UUID128_INIT(0x01, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
                     0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00);

static int clamp_pct(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

static bool next_i64(char **save, int64_t *out)
{
    char *field = strtok_r(NULL, "|", save);
    if (!field) return false;
    *out = atoll(field);
    return true;
}

static bool next_i32(char **save, int32_t *out)
{
    char *field = strtok_r(NULL, "|", save);
    if (!field) return false;
    *out = atoi(field);
    return true;
}

static bool next_str(char **save, char *out, size_t out_size)
{
    char *field = strtok_r(NULL, "|", save);
    if (!field) return false;
    strncpy(out, field, out_size - 1);
    out[out_size - 1] = '\0';
    return true;
}

static bool parse_payload_v3(char *line, usage_report_t *out)
{
    char *save = NULL;
    char *version = strtok_r(line, "|", &save);
    if (!version || strcmp(version, "3") != 0) return false;

    memset(out, 0, sizeof(*out));
    if (!next_i64(&save, &out->today_total)) return false;
    if (!next_i32(&save, &out->sessions_today)) return false;
    if (!next_i64(&save, &out->cache_total)) return false;
    if (!next_i32(&save, &out->cache_rate)) return false;
    if (!next_i32(&save, &out->active_minutes)) return false;
    if (!next_str(&save, out->updated_at, sizeof(out->updated_at))) return false;
    if (!next_str(&save, out->model_1, sizeof(out->model_1))) return false;
    if (!next_i32(&save, &out->model_1_pct)) return false;
    if (!next_str(&save, out->model_2, sizeof(out->model_2))) return false;
    if (!next_i32(&save, &out->model_2_pct)) return false;
    if (!next_str(&save, out->model_3, sizeof(out->model_3))) return false;
    if (!next_i32(&save, &out->model_3_pct)) return false;
    /* v3 extended fields (optional for backward compat) */
    next_i32(&save, &out->errors_today);
    next_i32(&save, &out->age_sec);
    next_i64(&save, &out->output_total);
    next_i64(&save, &out->week_total);
    next_i64(&save, &out->input_total);

    out->cache_rate = clamp_pct(out->cache_rate);
    out->model_1_pct = clamp_pct(out->model_1_pct);
    out->model_2_pct = clamp_pct(out->model_2_pct);
    out->model_3_pct = clamp_pct(out->model_3_pct);
    if (out->active_minutes < 0) out->active_minutes = 0;
    if (out->errors_today < 0) out->errors_today = 0;
    if (out->age_sec < 0) out->age_sec = 0;
    out->valid = true;
    return true;
}

static bool parse_payload_v2(char *line, usage_report_t *out)
{
    char *save = NULL;
    char *version = strtok_r(line, "|", &save);
    if (!version || strcmp(version, "2") != 0) return false;

    memset(out, 0, sizeof(*out));
    if (!next_i64(&save, &out->today_total)) return false;
    if (!next_i32(&save, &out->context_percent)) return false;
    if (!next_i32(&save, &out->calls_today)) return false;
    if (!next_i32(&save, &out->errors_today)) return false;
    if (!next_i64(&save, &out->current_tokens)) return false;
    if (!next_i64(&save, &out->last_call_tokens)) return false;
    if (!next_i64(&save, &out->input_total)) return false;
    if (!next_i64(&save, &out->output_total)) return false;
    if (!next_i64(&save, &out->cache_total)) return false;
    if (!next_i64(&save, &out->thought_total)) return false;

    char *model = strtok_r(NULL, "|", &save);
    char *updated_at = strtok_r(NULL, "|", &save);
    if (!model || !updated_at) return false;
    strncpy(out->model, model, sizeof(out->model) - 1);
    strncpy(out->updated_at, updated_at, sizeof(out->updated_at) - 1);
    if (!next_i32(&save, &out->age_sec)) return false;

    out->context_percent = clamp_pct(out->context_percent);
    if (out->age_sec < 0) out->age_sec = 0;
    out->valid = true;
    return true;
}

static bool parse_payload_v1(const char *line, usage_report_t *out)
{
    long long today = 0;
    long long session = 0;
    long long input = 0;
    long long output = 0;
    int cost = 0;
    int ctx = 0;
    int sessions = 0;
    int cache = 0;
    int n = sscanf(line, "%lld,%d,%d,%d,%d,%lld,%lld,%lld",
                   &today, &cost, &ctx, &sessions, &cache,
                   &session, &input, &output);
    if (n != 8) return false;

    memset(out, 0, sizeof(*out));
    out->today_total = today;
    out->context_percent = clamp_pct(ctx);
    out->calls_today = sessions;
    out->current_tokens = session;
    out->last_call_tokens = session;
    out->input_total = input;
    out->output_total = output;
    out->cache_total = input * cache / 100;
    strncpy(out->model, "qwen", sizeof(out->model) - 1);
    strncpy(out->updated_at, "-- --:--", sizeof(out->updated_at) - 1);
    (void)cost;
    out->valid = true;
    return true;
}

static bool parse_payload(const char *line, usage_report_t *out)
{
    char copy[sizeof(s_last_payload)];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    if (parse_payload_v3(copy, out)) return true;

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    return parse_payload_v2(copy, out) || parse_payload_v1(line, out);
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void) conn_handle;
    (void) attr_handle;
    (void) arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_last_payload, strlen(s_last_payload)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len >= sizeof(s_last_payload)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char payload[sizeof(s_last_payload)];
    int rc = ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload) - 1, &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    payload[len] = '\0';

    usage_report_t report;
    if (!parse_payload(payload, &report)) {
        ESP_LOGW(TAG, "invalid payload: %s", payload);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    strncpy(s_last_payload, payload, sizeof(s_last_payload) - 1);
    s_last_payload[sizeof(s_last_payload) - 1] = '\0';
    ESP_LOGI(TAG, "rx: %s", s_last_payload);

    if (s_data_cb) {
        s_data_cb(&report);
    }
    return 0;
}

static const struct ble_gatt_svc_def GATT_SVCS[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &DATA_UUID.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as %s", DEVICE_NAME);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void) arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "connected");
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected");
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr infer failed rc=%d", rc);
        return;
    }
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "reset reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_app_init(ble_data_cb_t cb)
{
    s_data_cb = cb;

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed rc=%d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);

    rc = ble_gatts_count_cfg(GATT_SVCS);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_add_svcs(GATT_SVCS);
    if (rc != 0) return ESP_FAIL;

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
