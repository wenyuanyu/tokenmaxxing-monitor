#include "battery_log.h"

#include "esp_log.h"
#include "esp_partition.h"

#include <string.h>

#define BATLOG_PARTITION_LABEL "batlog"
#define BATLOG_MAGIC 0xB17E
#define BATLOG_VERSION 1
#define BATLOG_SECTOR_SIZE 4096

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t seq;
    uint32_t uptime_s;
    uint16_t millivolts;
    uint8_t raw_percent;
    uint8_t shown_percent;
    int16_t temp_c_x10;
    uint16_t humidity_x10;
} battery_log_record_t;

static const char *TAG = "battery_log";
static const esp_partition_t *s_part;
static uint32_t s_next_seq;
static size_t s_next_offset;

static bool record_is_valid(const battery_log_record_t *r)
{
    return r->magic == BATLOG_MAGIC && r->version == BATLOG_VERSION;
}

static bool record_is_empty(const battery_log_record_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < sizeof(*r); i++) {
        if (p[i] != 0xFF) return false;
    }
    return true;
}

static esp_err_t erase_sector_for_offset(size_t offset)
{
    size_t sector = offset - (offset % BATLOG_SECTOR_SIZE);
    return esp_partition_erase_range(s_part, sector, BATLOG_SECTOR_SIZE);
}

esp_err_t battery_log_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                      BATLOG_PARTITION_LABEL);
    if (!s_part) {
        ESP_LOGW(TAG, "partition %s not found", BATLOG_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    battery_log_record_t r;
    uint32_t max_seq = 0;
    bool have_record = false;
    s_next_offset = 0;

    for (size_t off = 0; off + sizeof(r) <= s_part->size; off += sizeof(r)) {
        esp_err_t err = esp_partition_read(s_part, off, &r, sizeof(r));
        if (err != ESP_OK) return err;
        if (record_is_empty(&r)) {
            s_next_offset = off;
            break;
        }
        if (record_is_valid(&r) && (!have_record || r.seq >= max_seq)) {
            have_record = true;
            max_seq = r.seq;
            s_next_offset = off + sizeof(r);
        }
    }

    if (s_next_offset + sizeof(r) > s_part->size) {
        s_next_offset = 0;
    }

    if ((s_next_offset % BATLOG_SECTOR_SIZE) == 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(erase_sector_for_offset(s_next_offset));
    }

    s_next_seq = have_record ? max_seq + 1 : 0;
    ESP_LOGI(TAG, "ready size=%u next_offset=%u next_seq=%lu",
             (unsigned)s_part->size, (unsigned)s_next_offset, (unsigned long)s_next_seq);
    return ESP_OK;
}

esp_err_t battery_log_append(const battery_log_sample_t *sample)
{
    if (!s_part || !sample) return ESP_ERR_INVALID_STATE;

    if (s_next_offset + sizeof(battery_log_record_t) > s_part->size) {
        s_next_offset = 0;
    }
    if ((s_next_offset % BATLOG_SECTOR_SIZE) == 0) {
        esp_err_t err = erase_sector_for_offset(s_next_offset);
        if (err != ESP_OK) return err;
    }

    battery_log_record_t r = {
        .magic = BATLOG_MAGIC,
        .version = BATLOG_VERSION,
        .flags = sample->usb_connected ? 0x01 : 0x00,
        .seq = s_next_seq++,
        .uptime_s = sample->uptime_s,
        .millivolts = sample->millivolts,
        .raw_percent = sample->raw_percent,
        .shown_percent = sample->shown_percent,
        .temp_c_x10 = sample->temp_c_x10,
        .humidity_x10 = sample->humidity_x10,
    };

    esp_err_t err = esp_partition_write(s_part, s_next_offset, &r, sizeof(r));
    if (err == ESP_OK) {
        s_next_offset += sizeof(r);
    }
    return err;
}
